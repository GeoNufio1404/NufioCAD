#include <nuf/document/Features.hpp>

#include <nuf/document/Expression.hpp>

#include <nuf/geometry/Exchange.hpp>
#include <nuf/sketch/Solver.hpp>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <numbers>
#include <map>
#include <set>
#include "FeaturesCommon.hpp"

namespace nuf::document {

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
    const sketch::Plane plane =
        resolvePlane(state, attachment_, "del sketch", "se mantiene en su última posición", warning);
    state.sketches[id()] = {updatedSketch(state, plane, warning), plane};
    return warning;
}

namespace {

constexpr const char* kDimensionPrefix = "c";

bool isDimensionKey(const std::string& key)
{
    return key.size() > 1 && key[0] == kDimensionPrefix[0] &&
           std::ranges::all_of(key.begin() + 1, key.end(), [](char c) { return c >= '0' && c <= '9'; });
}

bool hasSolvableValue(const sketch::Constraint& c)
{
    return sketch::isDimensional(c.type) && c.driving;
}

} // namespace

std::vector<Feature::NumericField> SketchFeature::numericFields()
{
    std::vector<NumericField> fields;
    for (const sketch::Constraint& c : sketch_->constraints()) {
        if (!hasSolvableValue(c)) {
            continue;
        }
        const sketch::ConstraintId cid = c.id;
        const bool angle = c.type == sketch::ConstraintType::Angle;
        const double factor = angle ? std::numbers::pi / 180.0 : 1.0;
        NumericField field;
        field.key = kDimensionPrefix + std::to_string(cid);
        field.label = "Cota " + std::to_string(cid) + " del sketch";
        field.scale = factor;
        field.get = [this, cid, factor] {
            const sketch::Constraint* now = sketch_->constraint(cid);
            return now != nullptr ? now->value / factor : 0.0;
        };
        const bool positive = c.type != sketch::ConstraintType::DistanceX && c.type != sketch::ConstraintType::DistanceY &&
                              c.type != sketch::ConstraintType::Angle;
        field.set = [this, cid, factor, positive](double written) {
            if (positive && written <= 0.0) {
                throw ExpressionError("el valor debe ser mayor que cero");
            }
            auto copy = std::make_shared<sketch::Sketch>(*sketch_);
            if (sketch::Constraint* target = copy->constraint(cid)) {
                // Se conserva el sentido (signo) de la medida, como al editar la cota a mano.
                const double value = written * factor;
                target->value = (target->value < 0.0 && value > 0.0) ? -value : value;
            }
            sketch_ = std::move(copy);
        };
        fields.push_back(std::move(field));
    }
    return fields;
}

sketch::Sketch SketchFeature::forEditing(const sketch::Sketch& computed) const
{
    sketch::Sketch result = computed;
    for (const auto& [key, text] : expressions) {
        if (!isDimensionKey(key)) {
            continue;
        }
        if (sketch::Constraint* c = result.constraint(static_cast<sketch::ConstraintId>(std::stoul(key.substr(1))))) {
            c->expression = text;
        }
    }
    return result;
}

void SketchFeature::setSketchFromEditing(sketch::Sketch sketch)
{
    std::erase_if(expressions, [](const auto& entry) { return isDimensionKey(entry.first); });
    std::vector<sketch::ConstraintId> ids;
    for (const sketch::Constraint& c : sketch.constraints()) {
        ids.push_back(c.id);
    }
    for (const sketch::ConstraintId id : ids) {
        sketch::Constraint& c = *sketch.constraint(id);
        if (!c.expression.empty() && hasSolvableValue(c)) {
            expressions[kDimensionPrefix + std::to_string(id)] = c.expression;
        }
        c.expression.clear();
    }
    setSketch(std::move(sketch));
}

std::shared_ptr<const sketch::Sketch> SketchFeature::updatedSketch(const ModelState& state, const sketch::Plane& plane,
                                                                   std::optional<std::string>& warning) const
{
    const auto& entities = sketch_->entities();
    const bool bound = std::ranges::any_of(expressions, [](const auto& entry) { return isDimensionKey(entry.first); });
    if (!bound && std::ranges::none_of(entities, [](const sketch::Entity& e) { return e.link.has_value(); })) {
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
    const sketch::SolveResult solved = sketch::makePlaneGcsSolver()->solve(*updated);
    if (bound && solved.status != sketch::SolveStatus::Solved) {
        throw FeatureError(solved.status == sketch::SolveStatus::Conflicting
                               ? "Con los valores de los parámetros, las cotas del sketch entran en conflicto"
                               : "Con los valores de los parámetros, el sketch no tiene solución");
    }
    if (lost > 0 || changedType > 0) {
        const std::string note = "Algunas aristas proyectadas ya no existen o cambiaron; se mantienen como estaban.";
        warning = warning ? *warning + " " + note : note;
    }
    return updated;
}

std::vector<FeatureId> SketchFeature::dependencies() const
{
    std::vector<FeatureId> ids;
    addAttachmentDependencies(ids, attachment_);
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

std::string newBodyName(ModelState& state, const Feature& feature)
{
    ++state.bodiesCreated;
    std::string label;
    switch (feature.kind()) {
    case FeatureKind::Extrude: label = "Extrusión"; break;
    case FeatureKind::Revolve: label = "Revolución"; break;
    case FeatureKind::Import: label = "Importado"; break;
    case FeatureKind::Box: label = "Caja"; break;
    case FeatureKind::Cylinder: label = "Cilindro"; break;
    case FeatureKind::Thread: label = "Rosca"; break;
    case FeatureKind::Sweep: label = "Barrido"; break;
    case FeatureKind::Loft: label = "Loft"; break;
    case FeatureKind::Move: label = "Copia"; break;
    case FeatureKind::Pattern: label = "Patrón"; break;
    case FeatureKind::Mirror: label = "Espejo"; break;
    case FeatureKind::Split: label = "Parte"; break;
    case FeatureKind::Combine: label = "Unión"; break;
    case FeatureKind::Gear: {
        label = "Engranaje";
        if (const auto* gear = dynamic_cast<const GearFeature*>(&feature)) {
            switch (gear->spec.type) {
            case geometry::GearType::Internal: label = "Corona"; break;
            case geometry::GearType::Rack: label = "Cremallera"; break;
            case geometry::GearType::Worm: label = "Tornillo sin fin"; break;
            case geometry::GearType::Bevel: label = "Engranaje cónico"; break;
            case geometry::GearType::External: break;
            }
        }
        break;
    }
    default: label = "Cuerpo"; break;
    }
    return label + " " + std::to_string(++state.bodyNameCounts[label]);
}

std::optional<std::string> applyOperation(ModelState& state, const Feature& feature, Operation operation,
                                          const geometry::Shape& tool)
{
    if (tool.isNull()) {
        throw FeatureError("La operación no produce ningún sólido");
    }
    const auto newBody = [&] {
        state.bodies.push_back({feature.tag(), newBodyName(state, feature), tool});
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
        std::vector<geometry::Shape> parts;
        parts.push_back(tool);
        for (std::size_t i : touched) {
            parts.push_back(state.bodies[i].shape);
        }
        state.bodies[touched.front()].shape = geometry::fuseBalanced(std::move(parts));
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
    case AxisRef::Kind::Construction: {
        const auto it = state.axes.find(axis.reference);
        if (it == state.axes.end()) {
            throw FeatureError("El eje de construcción ya no existe");
        }
        return {it->second.point, it->second.direction};
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

geometry::Axis axisOf(const ModelState& state, const AxisRef& axis)
{
    return resolveAxis(state, axis);
}

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
    if (axis.kind == AxisRef::Kind::Construction) {
        addUnique(ids, axis.reference);
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
        state.bodies[*bodyIndex].shape = closed ? geometry::shellClosed(tag(), state.bodies[*bodyIndex].shape, thickness)
                                                : geometry::shell(tag(), state.bodies[*bodyIndex].shape, indices, thickness, outward);
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

// --- Espejo --------------------------------------------------------------------

std::optional<std::string> MirrorFeature::compute(ModelState& state) const
{
    const auto source = std::ranges::find_if(state.bodies, [&](const Body& b) { return b.key == body; });
    if (source == state.bodies.end()) {
        throw FeatureError("El cuerpo que se iba a reflejar ya no existe");
    }
    // El plano: de origen, o la cara (si ya no es plana o no existe, el último
    // que tuvo, con un aviso).
    std::optional<std::string> warning;
    const sketch::Plane mirrorPlane =
        resolvePlane(state, plane, "del plano de simetría", "se usa su última posición", warning);
    // Todos los cuerpos a reflejar (el principal y los demás), tal como estaban antes de empezar: reflejar uno con «Unir» cambia
    // la lista de cuerpos, así que se toman las formas de antemano.
    std::vector<std::pair<std::string, geometry::Shape>> sources = {{body, source->shape}};
    int missing = 0;
    for (const std::string& key : alsoBodies) {
        const auto other = std::ranges::find_if(state.bodies, [&](const Body& b) { return b.key == key; });
        if (other == state.bodies.end()) {
            ++missing;
        } else if (key != body) {
            sources.emplace_back(key, other->shape);
        }
    }
    try {
        std::size_t index = 0;
        for (const auto& [key, shape] : sources) {
            // Cada copia lleva su número en el nombre de las caras, para que no se repitan entre cuerpos.
            const std::string name = index == 0 ? tag() : tag() + ":c" + std::to_string(index);
            ++index;
            const geometry::Shape mirrored = geometry::mirror(name, shape, mirrorPlane);
            if (auto extra = applyOperation(state, *this, operation, mirrored)) {
                warning = warning ? *warning + " " + *extra : *extra;
            }
        }
    } catch (const geometry::GeometryError& e) {
        throw FeatureError(e.what());
    }
    if (missing > 0) {
        const std::string text = missing == 1 ? "Un cuerpo de la lista ya no existe." : "Algunos cuerpos de la lista ya no existen.";
        warning = warning ? *warning + " " + text : text;
    }
    return warning;
}

std::optional<std::string> SplitFeature::compute(ModelState& state) const
{
    const auto source = std::ranges::find_if(state.bodies, [&](const Body& b) { return b.key == body; });
    if (source == state.bodies.end()) {
        throw FeatureError("El cuerpo que se iba a dividir ya no existe");
    }
    std::optional<std::string> warning;
    sketch::Plane cut = resolvePlane(state, plane, "del plano de corte", "se usa su última posición", warning);
    cut.origin = {cut.origin.x + cut.normal.x * offset, cut.origin.y + cut.normal.y * offset, cut.origin.z + cut.normal.z * offset};
    try {
        auto [front, back] = geometry::splitBody(tag(), source->shape, cut);
        const std::size_t index = std::size_t(source - state.bodies.begin());
        state.bodies[index].shape = std::move(front);
        state.bodies.push_back({tag(), newBodyName(state, *this), std::move(back)});
    } catch (const geometry::GeometryError& e) {
        throw FeatureError(e.what());
    }
    return warning;
}

std::vector<FeatureId> SplitFeature::dependencies() const
{
    std::vector<FeatureId> ids;
    addAll(ids, geometry::ElementRef{body + ":", 0});
    addAttachmentDependencies(ids, plane);
    return ids;
}

std::vector<FeatureId> MirrorFeature::dependencies() const
{
    std::vector<FeatureId> ids;
    addAll(ids, geometry::ElementRef{body + ":", 0});  // la operación que creó el cuerpo
    for (const std::string& other : alsoBodies) {
        addAll(ids, geometry::ElementRef{other + ":", 0});
    }
    addAttachmentDependencies(ids, plane);
    return ids;
}

// --- Plano de construcción -----------------------------------------------------

std::optional<std::string> ConstructionPlaneFeature::compute(ModelState& state) const
{
    std::optional<std::string> warning;
    sketch::Plane plane = resolvePlane(state, base, "base del plano", "se usa su última posición", warning);
    if (std::abs(angle) > 1e-12) {
        const sketch::Vec3 axis = angleAxis == 0 ? plane.xAxis : plane.yAxis;
        plane.xAxis = rotateAbout(plane.xAxis, axis, angle);
        plane.yAxis = rotateAbout(plane.yAxis, axis, angle);
        plane.normal = rotateAbout(plane.normal, axis, angle);
    }
    plane.origin = {plane.origin.x + plane.normal.x * offset, plane.origin.y + plane.normal.y * offset,
                    plane.origin.z + plane.normal.z * offset};
    state.planes[id()] = plane;
    return warning;
}

std::vector<FeatureId> ConstructionPlaneFeature::dependencies() const
{
    std::vector<FeatureId> ids;
    addAttachmentDependencies(ids, base);
    return ids;
}

} // namespace nuf::document
