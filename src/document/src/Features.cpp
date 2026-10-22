#include <nuf/document/Features.hpp>

#include <algorithm>
#include <cmath>
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
    state.sketches[id()] = {sketch_, plane};
    return warning;
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

} // namespace nuf::document
