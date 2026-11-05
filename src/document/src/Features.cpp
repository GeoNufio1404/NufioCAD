#include <nuf/document/Features.hpp>

#include <nuf/geometry/Exchange.hpp>
#include <nuf/sketch/Solver.hpp>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <map>
#include <set>

namespace nuf::document {

namespace {

void addUnique(std::vector<FeatureId>& ids, FeatureId id)
{
    if (id != 0 && std::ranges::find(ids, id) == ids.end()) {
        ids.push_back(id);
    }
}

void addAll(std::vector<FeatureId>& ids, const geometry::ElementRef& ref)
{
    for (FeatureId id : featuresIn(ref)) {
        addUnique(ids, id);
    }
}

std::optional<std::string> joinWarnings(const std::vector<std::string>& warnings)
{
    if (warnings.empty()) {
        return std::nullopt;
    }
    std::string joined;
    for (const std::string& w : warnings) {
        joined += (joined.empty() ? "" : " ") + w;
    }
    return joined;
}

// Los perfiles elegidos, agrupados por sketch y ya en su plano.
std::vector<geometry::SketchRegion> regionsOf(const ModelState& state, const std::vector<ProfileRef>& refs,
                                              std::vector<std::string>& warnings)
{
    std::map<FeatureId, geometry::SketchRegion> bySketch;
    std::map<FeatureId, std::vector<sketch::Profile>> found;
    int lost = 0;
    for (const ProfileRef& ref : refs) {
        const auto out = state.sketches.find(ref.sketch);
        if (out == state.sketches.end()) {
            throw FeatureError("Falta el sketch de los perfiles");
        }
        auto [it, fresh] = found.try_emplace(ref.sketch);
        if (fresh) {
            it->second = sketch::findProfiles(*out->second.sketch);
        }
        const auto profile = resolveProfile(it->second, ref);
        if (!profile) {
            ++lost;
            continue;
        }
        auto& region = bySketch[ref.sketch];
        region.sketch = out->second.sketch.get();
        region.plane = out->second.plane;
        region.profiles.push_back(*profile);
    }
    if (bySketch.empty()) {
        throw FeatureError("Los perfiles elegidos ya no existen en el sketch");
    }
    if (lost > 0) {
        warnings.push_back(lost == 1 ? "Un perfil elegido ya no existe." : "Algunos perfiles elegidos ya no existen.");
    }
    std::vector<geometry::SketchRegion> regions;
    for (auto& [id, region] : bySketch) {
        regions.push_back(std::move(region));
    }
    return regions;
}

// Une los sólidos de varios sketches en uno.
geometry::Shape fuseTools(const std::vector<geometry::Shape>& tools)
{
    geometry::Shape result = tools.front();
    for (std::size_t i = 1; i < tools.size(); ++i) {
        result = geometry::boolean(geometry::BooleanOp::Fuse, result, tools[i]);
    }
    return result;
}

void addProfileDependencies(std::vector<FeatureId>& ids, const std::vector<ProfileRef>& profiles)
{
    for (const ProfileRef& p : profiles) {
        addUnique(ids, p.sketch);
    }
}

} // namespace

// --- Sketch ----------------------------------------------------------------

SketchFeature::SketchFeature(sketch::Sketch sketch, SketchAttachment attachment)
    : sketch_(std::make_shared<sketch::Sketch>(std::move(sketch)))
    , attachment_(std::move(attachment))
{
}

void SketchFeature::setSketch(sketch::Sketch sketch)
{
    sketch_ = std::make_shared<sketch::Sketch>(std::move(sketch));
}

std::optional<std::string> SketchFeature::compute(ModelState& state) const
{
    std::optional<std::string> warning;
    sketch::Plane plane = attachment_.lastPlane;
    if (attachment_.origin) {
        switch (*attachment_.origin) {
        case OriginPlane::XY:
            plane = sketch::Plane::xy();
            break;
        case OriginPlane::XZ:
            plane = sketch::Plane::xz();
            break;
        case OriginPlane::YZ:
            plane = sketch::Plane::yz();
            break;
        }
    } else if (const auto hit = state.findFace(attachment_.face)) {
        if (const auto facePlane = state.bodies[hit->body].shape.facePlane(hit->face)) {
            plane = *facePlane;
        } else {
            warning = "La cara del sketch ya no es plana; se mantiene en su última posición.";
        }
    } else {
        warning = "La cara del sketch ya no existe; se mantiene en su última posición.";
    }
    state.sketches[id()] = {updatedSketch(state, plane, warning), plane};
    return warning;
}

std::shared_ptr<const sketch::Sketch> SketchFeature::updatedSketch(const ModelState& state, const sketch::Plane& plane,
                                                                   std::optional<std::string>& warning) const
{
    const auto& entities = sketch_->entities();
    if (std::ranges::none_of(entities, [](const sketch::Entity& e) { return e.link.has_value(); })) {
        return sketch_;
    }
    // Lo proyectado se vuelve a proyectar desde la pieza actual y el resto
    // del sketch se ajusta a ello con el solver.
    auto updated = std::make_shared<sketch::Sketch>(*sketch_);
    int lost = 0;
    int changedType = 0;
    for (const sketch::Entity& e : entities) {
        if (!e.link) {
            continue;
        }
        const auto hit = state.findEdge({e.link->edge, e.link->occurrence});
        if (!hit) {
            ++lost;
            continue;
        }
        const std::vector<sketch::Geometry> pieces =
            state.bodies[hit->body].shape.projectEdge(hit->edge, plane);
        if (e.link->piece >= int(pieces.size())) {
            ++lost;
            continue;
        }
        const sketch::Geometry& fresh = pieces[std::size_t(e.link->piece)];
        if (fresh.index() != e.geometry.index()) {
            ++changedType;  // la arista cambió de tipo (p. ej. de recta a curva)
            continue;
        }
        updated->entity(e.id)->geometry = fresh;
    }
    sketch::makePlaneGcsSolver()->solve(*updated);
    if (lost > 0 || changedType > 0) {
        const std::string note = "Algunas aristas proyectadas ya no existen o cambiaron; se mantienen como estaban.";
        warning = warning ? *warning + " " + note : note;
    }
    return updated;
}

std::vector<FeatureId> SketchFeature::dependencies() const
{
    std::vector<FeatureId> ids;
    if (!attachment_.origin) {
        addAll(ids, attachment_.face);
    }
    return ids;
}

// --- Perfiles ----------------------------------------------------------------

ProfileRef profileRef(FeatureId sketch, const sketch::Profile& profile)
{
    ProfileRef ref{sketch, {}};
    for (const sketch::ProfileEdge& e : profile.outer.edges) {
        ref.outer.push_back(e.entity);
    }
    std::ranges::sort(ref.outer);
    ref.outer.erase(std::unique(ref.outer.begin(), ref.outer.end()), ref.outer.end());
    return ref;
}

std::optional<sketch::Profile> resolveProfile(const std::vector<sketch::Profile>& profiles, const ProfileRef& ref)
{
    const sketch::Profile* best = nullptr;
    std::size_t bestShared = 0;
    for (const sketch::Profile& profile : profiles) {
        const ProfileRef candidate = profileRef(ref.sketch, profile);
        if (candidate.outer == ref.outer) {
            return profile;
        }
        std::vector<sketch::EntityId> shared;
        std::ranges::set_intersection(candidate.outer, ref.outer, std::back_inserter(shared));
        if (shared.size() > bestShared) {
            bestShared = shared.size();
            best = &profile;
        }
    }
    if (best != nullptr && 2 * bestShared >= ref.outer.size()) {
        return *best;
    }
    return std::nullopt;
}

// --- Aplicar a los cuerpos ---------------------------------------------------

std::optional<std::string> applyOperation(ModelState& state, const Feature& feature, Operation operation,
                                          const geometry::Shape& tool)
{
    if (tool.isNull()) {
        throw FeatureError("La operación no produce ningún sólido");
    }
    const auto newBody = [&] {
        state.bodies.push_back({feature.tag(), "Cuerpo" + std::to_string(++state.bodiesCreated), tool});
    };
    std::vector<std::size_t> touched;
    if (operation != Operation::NewBody) {
        for (std::size_t i = 0; i < state.bodies.size(); ++i) {
            if (geometry::touches(state.bodies[i].shape, tool)) {
                touched.push_back(i);
            }
        }
    }

    switch (operation) {
    case Operation::NewBody:
        newBody();
        return std::nullopt;
    case Operation::Join: {
        if (touched.empty()) {
            newBody();  // como Fusion: si no toca nada, es un cuerpo nuevo
            return std::nullopt;
        }
        // Todo se une en el primer cuerpo tocado; los demás desaparecen.
        geometry::Shape joined = tool;
        for (std::size_t i : touched) {
            joined = geometry::boolean(geometry::BooleanOp::Fuse, state.bodies[i].shape, joined);
        }
        state.bodies[touched.front()].shape = joined;
        for (auto it = touched.rbegin(); it != touched.rend() - 1; ++it) {
            state.bodies.erase(state.bodies.begin() + std::ptrdiff_t(*it));
        }
        return std::nullopt;
    }
    case Operation::Cut:
    case Operation::Intersect: {
        if (touched.empty()) {
            return operation == Operation::Cut ? "No corta ningún cuerpo." : "No se cruza con ningún cuerpo.";
        }
        const auto op = operation == Operation::Cut ? geometry::BooleanOp::Cut : geometry::BooleanOp::Common;
        for (auto it = touched.rbegin(); it != touched.rend(); ++it) {
            Body& body = state.bodies[*it];
            body.shape = geometry::boolean(op, body.shape, tool);
            if (body.shape.isNull()) {
                state.bodies.erase(state.bodies.begin() + std::ptrdiff_t(*it));
            }
        }
        return std::nullopt;
    }
    }
    return std::nullopt;
}

// --- Extruir -----------------------------------------------------------------

geometry::Shape ExtrudeFeature::tool(const ModelState& state, std::vector<std::string>* warnings) const
{
    std::vector<std::string> local;
    std::vector<std::string>& notes = warnings ? *warnings : local;
    try {
        if (!profiles.empty()) {
            std::vector<geometry::Shape> tools;
            for (const geometry::SketchRegion& region : regionsOf(state, profiles, notes)) {
                tools.push_back(geometry::extrude(tag(), region, spec, state.shapes()));
            }
            return fuseTools(tools);
        }
        if (!face.empty()) {
            const auto hit = state.findFace(face);
            if (!hit) {
                throw FeatureError("La cara a extruir ya no existe");
            }
            return geometry::extrudeFace(tag(), state.bodies[hit->body].shape, hit->face, spec, state.shapes());
        }
    } catch (const geometry::GeometryError& e) {
        throw FeatureError(e.what());
    }
    throw FeatureError("No hay perfiles ni cara que extruir");
}

std::optional<std::string> ExtrudeFeature::compute(ModelState& state) const
{
    std::vector<std::string> warnings;
    const geometry::Shape solid = tool(state, &warnings);
    try {
        if (auto w = applyOperation(state, *this, operation, solid)) {
            warnings.push_back(*w);
        }
    } catch (const geometry::GeometryError& e) {
        throw FeatureError(e.what());
    }
    return joinWarnings(warnings);
}

std::vector<FeatureId> ExtrudeFeature::dependencies() const
{
    std::vector<FeatureId> ids;
    addProfileDependencies(ids, profiles);
    addAll(ids, face);
    for (const geometry::ExtrudeSide* side : {&spec.side1, &spec.side2}) {
        if (side->extent == geometry::Extent::ToFace) {
            addAll(ids, side->face);
        }
    }
    return ids;
}

// --- Revolución --------------------------------------------------------------

namespace {

geometry::Axis resolveAxis(const ModelState& state, const AxisRef& axis)
{
    switch (axis.kind) {
    case AxisRef::Kind::OriginX:
        return {{0, 0, 0}, {1, 0, 0}};
    case AxisRef::Kind::OriginY:
        return {{0, 0, 0}, {0, 1, 0}};
    case AxisRef::Kind::OriginZ:
        return {{0, 0, 0}, {0, 0, 1}};
    case AxisRef::Kind::SketchLine: {
        const auto out = state.sketches.find(axis.sketch);
        const sketch::Entity* e = out == state.sketches.end() ? nullptr : out->second.sketch->entity(axis.entity);
        const auto* line = e ? std::get_if<sketch::Line>(&e->geometry) : nullptr;
        if (line == nullptr) {
            throw FeatureError("La línea del eje ya no existe");
        }
        const sketch::Vec3 a = out->second.plane.toWorld(line->start);
        const sketch::Vec3 b = out->second.plane.toWorld(line->end);
        const sketch::Vec3 d{b.x - a.x, b.y - a.y, b.z - a.z};
        const double len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
        if (len < 1e-9) {
            throw FeatureError("La línea del eje no tiene longitud");
        }
        return {a, {d.x / len, d.y / len, d.z / len}};
    }
    case AxisRef::Kind::Edge: {
        const auto hit = state.findEdge(axis.edge);
        if (!hit) {
            throw FeatureError("La arista del eje ya no existe");
        }
        const auto line = state.bodies[hit->body].shape.edgeLine(hit->edge);
        if (!line) {
            throw FeatureError("La arista del eje no es recta");
        }
        return {line->first, line->second};
    }
    }
    throw FeatureError("Eje no válido");
}

} // namespace

geometry::Shape RevolveFeature::tool(const ModelState& state, std::vector<std::string>* warnings) const
{
    std::vector<std::string> local;
    std::vector<std::string>& notes = warnings ? *warnings : local;
    try {
        const geometry::Axis ax = resolveAxis(state, axis);
        std::vector<geometry::Shape> tools;
        for (const geometry::SketchRegion& region : regionsOf(state, profiles, notes)) {
            tools.push_back(geometry::revolve(tag(), region, ax, spec));
        }
        return fuseTools(tools);
    } catch (const geometry::GeometryError& e) {
        throw FeatureError(e.what());
    }
}

std::optional<std::string> RevolveFeature::compute(ModelState& state) const
{
    std::vector<std::string> warnings;
    const geometry::Shape solid = tool(state, &warnings);
    try {
        if (auto w = applyOperation(state, *this, operation, solid)) {
            warnings.push_back(*w);
        }
    } catch (const geometry::GeometryError& e) {
        throw FeatureError(e.what());
    }
    return joinWarnings(warnings);
}

std::vector<FeatureId> RevolveFeature::dependencies() const
{
    std::vector<FeatureId> ids;
    addProfileDependencies(ids, profiles);
    if (axis.kind == AxisRef::Kind::SketchLine) {
        addUnique(ids, axis.sketch);
    }
    if (axis.kind == AxisRef::Kind::Edge) {
        addAll(ids, axis.edge);
    }
    return ids;
}

// --- Redondeo y chaflán --------------------------------------------------------

std::optional<std::string> EdgeFeature::compute(ModelState& state) const
{
    if (edges.empty()) {
        throw FeatureError("No hay aristas elegidas");
    }
    // Todas las aristas deben ser del mismo cuerpo.
    std::optional<std::size_t> bodyIndex;
    std::vector<int> indices;
    int lost = 0;
    for (const geometry::ElementRef& ref : edges) {
        const auto hit = state.findEdge(ref);
        if (!hit) {
            ++lost;
            continue;
        }
        if (bodyIndex && *bodyIndex != hit->body) {
            throw FeatureError("Las aristas elegidas son de cuerpos distintos");
        }
        bodyIndex = hit->body;
        if (std::ranges::find(indices, hit->edge) == indices.end()) {
            indices.push_back(hit->edge);
        }
    }
    if (!bodyIndex) {
        throw FeatureError("Las aristas elegidas ya no existen");
    }
    try {
        state.bodies[*bodyIndex].shape = apply(tag(), state.bodies[*bodyIndex].shape, indices);
    } catch (const geometry::GeometryError& e) {
        throw FeatureError(e.what());
    }
    if (lost > 0) {
        return lost == 1 ? "Una arista elegida ya no existe." : "Algunas aristas elegidas ya no existen.";
    }
    return std::nullopt;
}

std::vector<FeatureId> EdgeFeature::dependencies() const
{
    std::vector<FeatureId> ids;
    for (const geometry::ElementRef& ref : edges) {
        addAll(ids, ref);
    }
    return ids;
}

// --- Vaciado -----------------------------------------------------------------

std::optional<std::string> ShellFeature::compute(ModelState& state) const
{
    if (faces.empty()) {
        throw FeatureError("No hay caras elegidas");
    }
    std::optional<std::size_t> bodyIndex;
    std::vector<int> indices;
    int lost = 0;
    for (const geometry::ElementRef& ref : faces) {
        const auto hit = state.findFace(ref);
        if (!hit) {
            ++lost;
            continue;
        }
        if (bodyIndex && *bodyIndex != hit->body) {
            throw FeatureError("Las caras elegidas son de cuerpos distintos");
        }
        bodyIndex = hit->body;
        if (std::ranges::find(indices, hit->face) == indices.end()) {
            indices.push_back(hit->face);
        }
    }
    if (!bodyIndex) {
        throw FeatureError("Las caras elegidas ya no existen");
    }
    try {
        state.bodies[*bodyIndex].shape = geometry::shell(tag(), state.bodies[*bodyIndex].shape, indices, thickness, outward);
    } catch (const geometry::GeometryError& e) {
        throw FeatureError(e.what());
    }
    if (lost > 0) {
        return lost == 1 ? "Una cara elegida ya no existe." : "Algunas caras elegidas ya no existen.";
    }
    return std::nullopt;
}

std::vector<FeatureId> ShellFeature::dependencies() const
{
    std::vector<FeatureId> ids;
    for (const geometry::ElementRef& ref : faces) {
        addAll(ids, ref);
    }
    return ids;
}

// --- Importar ----------------------------------------------------------------

std::optional<std::string> ImportFeature::compute(ModelState& state) const
{
    if (!brep || brep->empty()) {
        throw FeatureError("La operación no tiene sólido guardado");
    }
    std::vector<geometry::Shape> shapes;
    try {
        shapes = geometry::shapesFromBrep(*brep, tag());
    } catch (const geometry::GeometryError& e) {
        throw FeatureError(e.what());
    }
    std::vector<std::string> warnings = notes;
    for (std::size_t i = 0; i < shapes.size(); ++i) {
        if (!shapes[i].isValid()) {
            warnings.push_back("Un sólido importado tiene errores topológicos; puede fallar al modelar con él.");
            break;
        }
    }
    for (std::size_t i = 0; i < shapes.size(); ++i) {
        const std::string key = shapes.size() == 1 ? tag() : tag() + ":c" + std::to_string(i + 1);
        state.bodies.push_back({key, "Cuerpo" + std::to_string(++state.bodiesCreated), shapes[i]});
    }
    return joinWarnings(warnings);
}

} // namespace nuf::document
