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

// --- Campos ligables a parámetros ---------------------------------------------

namespace {

constexpr double kRadiansPerDegree = 3.14159265358979323846 / 180.0;

// Un número de la operación; get devuelve el valor en las unidades del usuario.
Feature::NumericField field(std::string key, std::string label, double& value, double scale = 1.0)
{
    return {std::move(key), std::move(label), scale, [&value, scale] { return value / scale; },
            [&value, scale](double v) { value = v * scale; }};
}

} // namespace

std::vector<Feature::NumericField> ExtrudeFeature::numericFields()
{
    return {field("distance", "Distancia", spec.side1.distance), field("distance2", "Distancia 2", spec.side2.distance),
            field("startOffset", "Desfase inicial", spec.startOffset)};
}

std::vector<Feature::NumericField> RevolveFeature::numericFields()
{
    return {field("angle", "Ángulo", spec.angle1, kRadiansPerDegree),
            field("angle2", "Ángulo 2", spec.angle2, kRadiansPerDegree)};
}

std::vector<Feature::NumericField> EdgeFeature::numericFields()
{
    std::vector<NumericField> fields = {field("size", "Tamaño", size)};
    if (kind() == FeatureKind::Chamfer) {
        fields.push_back(field("size2", "Segunda distancia", size2));
        fields.push_back(field("angle", "Ángulo", angle, kRadiansPerDegree));
    } else if (variable) {
        fields.push_back(field("size2", "Radio del otro extremo", size2));
        fields.push_back(field("middle", "Radio en el centro", middle));
    }
    return fields;
}

std::vector<Feature::NumericField> SweepFeature::numericFields()
{
    return {{"endScale", "Escala al final", 1.0, [this] { return spec.endScale; }, [this](double v) { spec.endScale = v; }}};
}

std::vector<Feature::NumericField> SplitFeature::numericFields()
{
    return {field("offset", "Posición del corte", offset)};
}

std::vector<Feature::NumericField> ShellFeature::numericFields()
{
    return {field("thickness", "Espesor", thickness)};
}

std::vector<Feature::NumericField> HoleFeature::numericFields()
{
    std::vector<NumericField> fields = {field("diameter", "Diámetro", spec.diameter), field("depth", "Profundidad", spec.depth),
                                        field("headDiameter", "Diámetro de la cabeza", spec.headDiameter),
                                        field("headDepth", "Profundidad de la cabeza", spec.headDepth),
                                        field("headAngle", "Ángulo del cono", spec.headAngle, kRadiansPerDegree)};
    if (onFace) {
        fields.push_back(field("x", "Posición X", x));
        fields.push_back(field("y", "Posición Y", y));
    }
    return fields;
}

std::vector<FeatureId> HoleFeature::dependencies() const
{
    std::vector<FeatureId> ids;
    if (onFace) {
        addAttachmentDependencies(ids, base);
    } else {
        ids.push_back(sketch);
    }
    return ids;
}

std::vector<Feature::NumericField> PatternFeature::numericFields()
{
    return {field("spacing", "Distancia entre copias", spacing), field("angle", "Ángulo total", angle, kRadiansPerDegree),
            {"count", "Cantidad", 1.0, [this] { return double(count); },
             [this](double v) { count = std::max(2, int(std::lround(v))); }}};
}

std::vector<Feature::NumericField> ConstructionPlaneFeature::numericFields()
{
    return {field("offset", "Distancia", offset), field("angle", "Ángulo", angle, kRadiansPerDegree)};
}

// --- Caja y cilindro --------------------------------------------------------------

namespace {

// Dónde empieza una primitiva: el plano, movido x/y en él y z a lo largo de la normal (y, si es simétrica,
// la mitad del alto hacia atrás).
sketch::Plane primitivePlane(sketch::Plane plane, double x, double y, double z, bool symmetric, double height)
{
    plane.origin = plane.toWorld({x, y});
    const double along = z - (symmetric ? height / 2.0 : 0.0);
    plane.origin = {plane.origin.x + plane.normal.x * along, plane.origin.y + plane.normal.y * along,
                    plane.origin.z + plane.normal.z * along};
    return plane;
}

} // namespace

sketch::Plane attachmentPlane(const ModelState& state, const SketchAttachment& attachment)
{
    std::optional<std::string> warning;
    return resolvePlane(state, attachment, "", "", warning);
}

std::optional<std::string> BoxFeature::compute(ModelState& state) const
{
    std::optional<std::string> warning;
    const sketch::Plane plane = resolvePlane(state, base, "de la caja", "se usa su última posición", warning);
    try {
        const geometry::Shape box =
            geometry::box(tag(), primitivePlane(plane, x, y, z, symmetric, height), width, depth, height, centered, cornerRadius);
        if (auto extra = applyOperation(state, *this, operation, box)) {
            warning = warning ? *warning + " " + *extra : *extra;
        }
    } catch (const geometry::GeometryError& e) {
        throw FeatureError(e.what());
    }
    return warning;
}

std::vector<FeatureId> BoxFeature::dependencies() const
{
    std::vector<FeatureId> ids;
    addAttachmentDependencies(ids, base);
    return ids;
}

std::vector<Feature::NumericField> BoxFeature::numericFields()
{
    return {field("x", "Posición X", x), field("y", "Posición Y", y), field("z", "Posición Z", z),
            field("width", "Ancho", width), field("depth", "Fondo", depth), field("height", "Alto", height),
            field("radius", "Radio de esquinas", cornerRadius)};
}

std::optional<std::string> CylinderFeature::compute(ModelState& state) const
{
    std::optional<std::string> warning;
    const sketch::Plane plane = resolvePlane(state, base, "del cilindro", "se usa su última posición", warning);
    try {
        const geometry::Shape cylinder =
            geometry::cylinder(tag(), primitivePlane(plane, x, y, z, symmetric, height), diameter, height);
        if (auto extra = applyOperation(state, *this, operation, cylinder)) {
            warning = warning ? *warning + " " + *extra : *extra;
        }
    } catch (const geometry::GeometryError& e) {
        throw FeatureError(e.what());
    }
    return warning;
}

std::vector<FeatureId> CylinderFeature::dependencies() const
{
    std::vector<FeatureId> ids;
    addAttachmentDependencies(ids, base);
    return ids;
}

std::vector<Feature::NumericField> CylinderFeature::numericFields()
{
    return {field("x", "Posición X", x), field("y", "Posición Y", y), field("z", "Posición Z", z),
            field("diameter", "Diámetro", diameter), field("height", "Alto", height)};
}

// --- Engranaje -----------------------------------------------------------------

std::optional<std::string> GearFeature::compute(ModelState& state) const
{
    std::optional<std::string> warning;
    sketch::Plane plane = resolvePlane(state, base, "del engranaje", "se usa su última posición", warning);
    plane.origin = plane.toWorld({x, y});
    if (rotation != 0.0) {
        // El primer diente apunta por el eje X del plano: se gira ese eje (y el Y con él) alrededor de la normal.
        const double c = std::cos(rotation), s = std::sin(rotation);
        const sketch::Vec3 xAxis = plane.xAxis, yAxis = plane.yAxis;
        plane.xAxis = {c * xAxis.x + s * yAxis.x, c * xAxis.y + s * yAxis.y, c * xAxis.z + s * yAxis.z};
        plane.yAxis = {-s * xAxis.x + c * yAxis.x, -s * xAxis.y + c * yAxis.y, -s * xAxis.z + c * yAxis.z};
    }
    try {
        const geometry::Shape gear = geometry::gear(tag(), plane, spec);
        if (auto extra = applyOperation(state, *this, operation, gear)) {
            warning = warning ? *warning + " " + *extra : *extra;
        }
    } catch (const geometry::GeometryError& e) {
        throw FeatureError(e.what());
    }
    return warning;
}

std::vector<FeatureId> GearFeature::dependencies() const
{
    std::vector<FeatureId> ids;
    addAttachmentDependencies(ids, base);
    return ids;
}

std::vector<Feature::NumericField> GearFeature::numericFields()
{
    return {field("x", "Posición X", x), field("y", "Posición Y", y),
            field("rotation", "Giro inicial", rotation, kRadiansPerDegree), field("module", "Módulo", spec.module),
            field("thickness", "Ancho", spec.thickness),
            field("bore", "Agujero central", spec.bore),
            field("rim", "Aro o base", spec.rim),
            field("length", "Largo de la barra", spec.length),
            field("pressureAngle", "Ángulo de presión", spec.pressureAngle, kRadiansPerDegree),
            field("helixAngle", "Ángulo de hélice", spec.helixAngle, kRadiansPerDegree),
            field("backlash", "Juego del diente", spec.backlash),
            field("baseChamfer", "Chaflán de la base", spec.baseChamfer),
            field("wormDiameter", "Diámetro del tornillo", spec.wormDiameter),
            field("herringboneGroove", "Surco central", spec.herringboneGroove),
            {"mateTeeth", "Dientes de la rueda pareja", 1.0, [this] { return double(spec.mateTeeth); },
             [this](double v) { spec.mateTeeth = int(std::lround(v)); }},
            {"leftHanded", "Hélice a izquierdas (0 o 1)", 1.0, [this] { return spec.leftHanded ? 1.0 : 0.0; },
             [this](double v) { spec.leftHanded = v > 0.5; }},
            {"profileShift", "Corrección de perfil (módulos)", 1.0, [this] { return spec.profileShift; },
             [this](double v) { spec.profileShift = v; }},
            {"teeth", "Dientes", 1.0, [this] { return double(spec.teeth); },
             [this](double v) { spec.teeth = int(std::lround(v)); }}};
}

// --- Rosca ----------------------------------------------------------------------

std::optional<std::string> ThreadFeature::compute(ModelState& state) const
{
    if (onCylinder) {
        const auto hit = state.findFace(cylinderFace);
        if (!hit) {
            throw FeatureError("Ya no existe la cara cilíndrica que se iba a roscar");
        }
        const auto info = state.bodies[hit->body].shape.cylinderFace(hit->face);
        if (!info) {
            throw FeatureError("La cara elegida ya no es un cilindro");
        }
        try {
            const geometry::Shape tool = geometry::threadOnCylinderTool(tag(), *info, spec);
            return applyOperation(state, *this, Operation::Cut, tool);
        } catch (const geometry::GeometryError& e) {
            throw FeatureError(e.what());
        }
    }
    std::optional<std::string> warning;
    sketch::Plane plane = resolvePlane(state, base, "de la rosca", "se usa su última posición", warning);
    plane.origin = plane.toWorld({x, y});
    try {
        geometry::ThreadSpec used = spec;
        if (operation == Operation::Cut) {
            used.diameter += 2.0 * clearance;
        }
        const geometry::Shape rod = geometry::thread(tag(), plane, used);
        if (auto extra = applyOperation(state, *this, operation, rod)) {
            warning = warning ? *warning + " " + *extra : *extra;
        }
    } catch (const geometry::GeometryError& e) {
        throw FeatureError(e.what());
    }
    return warning;
}

std::vector<FeatureId> ThreadFeature::dependencies() const
{
    std::vector<FeatureId> ids;
    if (onCylinder) {
        addAll(ids, cylinderFace);
        return ids;
    }
    addAttachmentDependencies(ids, base);
    return ids;
}

std::vector<Feature::NumericField> ThreadFeature::numericFields()
{
    return {field("x", "Posición X", x), field("y", "Posición Y", y), field("diameter", "Diámetro", spec.diameter),
            field("pitch", "Paso", spec.pitch), field("length", "Largo", spec.length),
            field("clearance", "Holgura", clearance)};
}

// --- Barrido ---------------------------------------------------------------------

std::optional<std::string> SweepFeature::compute(ModelState& state) const
{
    std::vector<std::string> warnings;
    try {
        const auto out = state.sketches.find(pathSketch);
        if (out == state.sketches.end()) {
            throw FeatureError("Falta el sketch de la trayectoria");
        }
        std::vector<geometry::Shape> tools;
        for (const geometry::SketchRegion& region : regionsOf(state, profiles, warnings)) {
            tools.push_back(geometry::sweep(tag(), region, *out->second.sketch, out->second.plane, path, spec));
        }
        if (auto w = applyOperation(state, *this, operation, fuseTools(tools))) {
            warnings.push_back(*w);
        }
    } catch (const geometry::GeometryError& e) {
        throw FeatureError(e.what());
    }
    return joinWarnings(warnings);
}

std::vector<FeatureId> SweepFeature::dependencies() const
{
    std::vector<FeatureId> ids;
    addProfileDependencies(ids, profiles);
    addUnique(ids, pathSketch);
    return ids;
}

// --- Loft ------------------------------------------------------------------------

std::optional<std::string> LoftFeature::compute(ModelState& state) const
{
    std::vector<std::string> warnings;
    try {
        std::vector<geometry::SketchRegion> sections;
        for (const ProfileRef& ref : profiles) {
            // Uno por uno, para conservar el orden.
            for (geometry::SketchRegion& region : regionsOf(state, {ref}, warnings)) {
                sections.push_back(std::move(region));
            }
        }
        if (sections.size() < profiles.size()) {
            throw FeatureError("Falta algún perfil del loft");
        }
        const geometry::Shape solid = geometry::loft(tag(), sections, ruled);
        if (auto w = applyOperation(state, *this, operation, solid)) {
            warnings.push_back(*w);
        }
    } catch (const geometry::GeometryError& e) {
        throw FeatureError(e.what());
    }
    return joinWarnings(warnings);
}

std::vector<FeatureId> LoftFeature::dependencies() const
{
    std::vector<FeatureId> ids;
    addProfileDependencies(ids, profiles);
    return ids;
}

// --- Mover y combinar cuerpos --------------------------------------------------------

std::optional<std::string> MoveFeature::compute(ModelState& state) const
{
    // El cuerpo y los que se mueven con él (un llavero son una placa y un texto): todos como una sola pieza.
    std::vector<std::vector<Body>::iterator> sources;
    for (const std::string& key : [&] {
             std::vector<std::string> keys{body};
             keys.insert(keys.end(), alsoMove.begin(), alsoMove.end());
             return keys;
         }()) {
        const auto found = std::ranges::find_if(state.bodies, [&](const Body& b) { return b.key == key; });
        if (found == state.bodies.end()) {
            throw FeatureError(key == body ? "El cuerpo que se iba a mover ya no existe"
                                           : "Uno de los cuerpos que se iban a mover ya no existe");
        }
        sources.push_back(found);
    }
    const auto source = sources.front();
    geometry::Axis rotationAxis;
    rotationAxis.direction = {axis == 0 ? 1.0 : 0.0, axis == 1 ? 1.0 : 0.0, axis == 2 ? 1.0 : 0.0};
    sketch::Vec3 center{};
    if (!aroundOrigin) {
        // El centro de todos juntos: giran como un solo bloque.
        geometry::Box box = source->shape.bounds();
        for (const auto& other : sources) {
            const geometry::Box b = other->shape.bounds();
            box.min = {std::min(box.min.x, b.min.x), std::min(box.min.y, b.min.y), std::min(box.min.z, b.min.z)};
            box.max = {std::max(box.max.x, b.max.x), std::max(box.max.y, b.max.y), std::max(box.max.z, b.max.z)};
        }
        center = {(box.min.x + box.max.x) / 2.0, (box.min.y + box.max.y) / 2.0, (box.min.z + box.max.z) / 2.0};
    }
    rotationAxis.point = center;
    if (axisRef != 0) {
        const auto line = state.axes.find(axisRef);
        if (line == state.axes.end()) {
            throw FeatureError("El eje de construcción del giro ya no existe");
        }
        rotationAxis.point = line->second.point;
        rotationAxis.direction = line->second.direction;
    }
    try {
        // Primero todo se calcula y luego se coloca (si una falla, no se mueve ninguno).
        std::vector<geometry::Shape> results;
        for (const auto& item : sources) {
            results.push_back(geometry::moveBody(tag(), item->shape, translation, rotationAxis, angle, copy, rotation, center));
        }
        if (copy) {
            // Varias copias a la vez (Fase 21): la primera lleva la clave de la operación y las demás «:c1», «:c2»…
            for (std::size_t i = 0; i < results.size(); ++i) {
                const std::string key = i == 0 ? tag() : tag() + ":c" + std::to_string(i);
                state.bodies.push_back({key, newBodyName(state, *this), std::move(results[i])});
            }
        } else {
            for (std::size_t i = 0; i < sources.size(); ++i) {
                sources[i]->shape = std::move(results[i]);
            }
        }
    } catch (const geometry::GeometryError& e) {
        throw FeatureError(e.what());
    }
    return std::nullopt;
}

std::vector<FeatureId> MoveFeature::dependencies() const
{
    std::vector<FeatureId> ids;
    addAll(ids, geometry::ElementRef{body + ":", 0});
    for (const std::string& other : alsoMove) {
        addAll(ids, geometry::ElementRef{other + ":", 0});
    }
    if (axisRef != 0) {
        ids.push_back(axisRef);
    }
    return ids;
}

std::vector<Feature::NumericField> MoveFeature::numericFields()
{
    return {field("x", "Desplazamiento X", translation.x), field("y", "Desplazamiento Y", translation.y),
            field("z", "Desplazamiento Z", translation.z), field("angle", "Ángulo", angle, kRadiansPerDegree),
            field("rx", "Giro X", rotation.x, kRadiansPerDegree), field("ry", "Giro Y", rotation.y, kRadiansPerDegree),
            field("rz", "Giro Z", rotation.z, kRadiansPerDegree)};
}

std::optional<std::string> CombineFeature::compute(ModelState& state) const
{
    if (tools.empty()) {
        throw FeatureError("Elige al menos un cuerpo herramienta");
    }
    const auto find = [&](const std::string& key) {
        return std::ranges::find_if(state.bodies, [&](const Body& b) { return b.key == key; });
    };
    const auto destination = find(target);
    if (destination == state.bodies.end()) {
        throw FeatureError("El cuerpo destino ya no existe");
    }
    std::vector<std::string> usedKeys;
    geometry::Shape result = destination->shape;
    std::optional<std::string> warning;
    try {
        const auto op = operation == Operation::Cut         ? geometry::BooleanOp::Cut
                        : operation == Operation::Intersect ? geometry::BooleanOp::Common
                                                            : geometry::BooleanOp::Fuse;
        for (const std::string& key : tools) {
            if (key == target) {
                continue;
            }
            const auto tool = find(key);
            if (tool == state.bodies.end()) {
                warning = "Un cuerpo herramienta ya no existe.";
                continue;
            }
            usedKeys.push_back(key);
            result = geometry::boolean(op, result, tool->shape);
            if (result.isNull()) {
                throw FeatureError("El resultado queda vacío");
            }
        }
    } catch (const geometry::GeometryError& e) {
        throw FeatureError(e.what());
    }
    if (usedKeys.empty()) {
        throw FeatureError("Faltan los cuerpos herramienta");
    }
    find(target)->shape = std::move(result);
    if (!keepTools) {
        std::erase_if(state.bodies, [&](const Body& b) { return std::ranges::find(usedKeys, b.key) != usedKeys.end(); });
    }
    return warning;
}

std::vector<FeatureId> CombineFeature::dependencies() const
{
    std::vector<FeatureId> ids;
    addAll(ids, geometry::ElementRef{target + ":", 0});
    for (const std::string& key : tools) {
        addAll(ids, geometry::ElementRef{key + ":", 0});
    }
    return ids;
}

// --- Unión entre cuerpos ------------------------------------------------------------

namespace {

// Lo que piden una unión o un alineado: de qué a qué y con qué valores.
struct PlacementRequest {
    ElementPick moving;
    ElementPick target;
    std::vector<std::string> alsoMove;
    bool flip = false;
    double offset = 0.0;
    double angle = 0.0;
    double slide = 0.0;
    double slideY = 0.0;
    double tiltX = 0.0;
    double tiltY = 0.0;
};

// Lo que quedó colocado: el eje y los cuerpos que se movieron (para el enlace de movimiento).
struct PlacementResult {
    sketch::Vec3 origin;
    sketch::Vec3 direction;
    sketch::Vec3 xAxis;
    std::vector<std::string> moved;
};

struct ResolvedFrame {
    std::size_t body = 0;
    geometry::Frame frame;
};

// «@axis:<clave>»: el eje de un cuerpo (lo usan las recetas, como el tren planetario, que no conocen los nombres de
// las caras de lo que generan): la cara cilíndrica o cónica más grande del cuerpo.
constexpr std::string_view kAxisPrefix = "@axis:";

ResolvedFrame resolveFrame(const ModelState& state, const ElementPick& pick, const char* which)
{
    if (pick.ref.name.rfind(kAxisPrefix, 0) == 0) {
        const std::string key = pick.ref.name.substr(kAxisPrefix.size());
        const auto body = std::ranges::find_if(state.bodies, [&](const Body& b) { return b.key == key; });
        if (body == state.bodies.end()) {
            throw FeatureError(std::string("El cuerpo ") + which + " ya no existe");
        }
        std::optional<geometry::Frame> best;
        double bestSize = -1.0;
        for (int i = 0; i < body->shape.faceCount(); ++i) {
            const auto frame = body->shape.faceFrame(i);
            if (!frame || frame->kind != geometry::Frame::Kind::Axis) {
                continue;
            }
            const geometry::Box box = body->shape.faceBounds(i);
            const double size = std::abs(box.max.x - box.min.x) + std::abs(box.max.y - box.min.y) + std::abs(box.max.z - box.min.z);
            if (size > bestSize) {
                bestSize = size;
                best = frame;
            }
        }
        if (!best) {
            throw FeatureError(std::string("El cuerpo ") + which + " no tiene una cara cilíndrica que sirva de eje");
        }
        return {std::size_t(body - state.bodies.begin()), *best};
    }
    if (pick.edge) {
        const auto hit = state.findEdge(pick.ref);
        if (!hit) {
            throw FeatureError(std::string("La arista ") + which + " ya no existe");
        }
        const auto frame = state.bodies[hit->body].shape.edgeFrame(hit->edge);
        if (!frame) {
            throw FeatureError(std::string("La arista ") + which + " no es circular ni recta: elige otra");
        }
        return {hit->body, *frame};
    }
    const auto hit = state.findFace(pick.ref);
    if (!hit) {
        throw FeatureError(std::string("La cara ") + which + " ya no existe");
    }
    const auto frame = state.bodies[hit->body].shape.faceFrame(hit->face);
    if (!frame) {
        throw FeatureError(std::string("La cara ") + which + " no es plana, cilíndrica, cónica ni esférica: elige otra");
    }
    return {hit->body, *frame};
}

sketch::Vec3 crossOf(const sketch::Vec3& u, const sketch::Vec3& v)
{
    return {u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z, u.x * v.y - u.y * v.x};
}

// Coloca la pieza «moving» sobre «target»: el marco de uno coincide con el del otro, más los valores pedidos.
// Si la que se mueve está fija, se mueve la otra (con la transformación inversa). Con dos piezas fijas, error.
PlacementResult applyPlacement(ModelState& state, const std::string& tag, const PlacementRequest& request)
{
    const ResolvedFrame target = resolveFrame(state, request.target, "de destino");
    const ResolvedFrame moving = resolveFrame(state, request.moving, "que se mueve");
    if (target.body == moving.body) {
        throw FeatureError("Los dos elementos son del mismo cuerpo: elige elementos de cuerpos distintos");
    }
    const geometry::Frame& a = target.frame;
    const geometry::Frame& b = moving.frame;
    const sketch::Vec3 ay = crossOf(a.direction, a.xAxis);

    // Destino: el marco del elemento de destino, con el giro, los deslizamientos y los cabeceos pedidos.
    const bool facing = a.kind == geometry::Frame::Kind::PlaneFace && b.kind == geometry::Frame::Kind::PlaneFace;
    sketch::Vec3 toNormal =
        (facing != request.flip) ? sketch::Vec3{-a.direction.x, -a.direction.y, -a.direction.z} : a.direction;
    sketch::Vec3 toX = rotateAbout(a.xAxis, a.direction, request.angle);
    if (request.tiltX != 0.0) {
        toNormal = rotateAbout(toNormal, a.xAxis, request.tiltX);
        toX = rotateAbout(toX, a.xAxis, request.tiltX);
    }
    if (request.tiltY != 0.0) {
        toNormal = rotateAbout(toNormal, ay, request.tiltY);
        toX = rotateAbout(toX, ay, request.tiltY);
    }
    sketch::Plane to;
    to.normal = toNormal;
    to.xAxis = toX;
    to.yAxis = crossOf(to.normal, to.xAxis);
    to.origin = {a.origin.x + a.direction.x * request.offset + a.xAxis.x * request.slide + ay.x * request.slideY,
                 a.origin.y + a.direction.y * request.offset + a.xAxis.y * request.slide + ay.y * request.slideY,
                 a.origin.z + a.direction.z * request.offset + a.xAxis.z * request.slide + ay.z * request.slideY};
    sketch::Plane from;
    from.origin = b.origin;
    from.normal = b.direction;
    from.xAxis = b.xAxis;
    from.yAxis = crossOf(from.normal, from.xAxis);

    const std::string movingKey = state.bodies[moving.body].key;
    const std::string targetKey = state.bodies[target.body].key;
    const bool movingFixed = state.fixedBodies.contains(movingKey);
    const bool targetFixed = state.fixedBodies.contains(targetKey);
    if (movingFixed && targetFixed) {
        throw FeatureError("Las dos piezas están fijas: suelta una para poder moverla");
    }
    // Lo que se mueve: la pieza (con sus acompañantes) o, si está fija, la otra.
    std::vector<std::size_t> group;
    bool inverse = false;
    if (movingFixed) {
        group.push_back(target.body);
        inverse = true;
    } else {
        group.push_back(moving.body);
        for (const std::string& key : request.alsoMove) {
            const auto found = std::ranges::find_if(state.bodies, [&](const Body& body) { return body.key == key; });
            if (found == state.bodies.end()) {
                throw FeatureError("Uno de los cuerpos que van con la pieza ya no existe");
            }
            const std::size_t index = std::size_t(found - state.bodies.begin());
            if (index != target.body && std::ranges::find(group, index) == group.end() && !state.fixedBodies.contains(key)) {
                group.push_back(index);
            }
        }
    }
    PlacementResult result;
    result.origin = {a.origin.x + a.direction.x * request.offset, a.origin.y + a.direction.y * request.offset,
                     a.origin.z + a.direction.z * request.offset};
    result.direction = a.direction;
    result.xAxis = a.xAxis;
    try {
        std::vector<geometry::Shape> placed;
        for (const std::size_t index : group) {
            placed.push_back(inverse ? geometry::placeBody(tag, state.bodies[index].shape, to, from)
                                     : geometry::placeBody(tag, state.bodies[index].shape, from, to));
        }
        for (std::size_t i = 0; i < group.size(); ++i) {
            state.bodies[group[i]].shape = std::move(placed[i]);
            result.moved.push_back(state.bodies[group[i]].key);
        }
    } catch (const geometry::GeometryError& e) {
        throw FeatureError(e.what());
    }
    return result;
}

// El valor entre los límites (si los hay).
double clampTo(double value, bool limited, double lo, double hi)
{
    if (!limited) {
        return value;
    }
    if (lo > hi) {
        std::swap(lo, hi);
    }
    return std::clamp(value, lo, hi);
}

} // namespace

std::optional<std::string> JointFeature::compute(ModelState& state) const
{
    PlacementRequest request;
    request.moving = {movingFace, movingEdge};
    request.target = {fixedFace, fixedEdge};
    request.alsoMove = alsoMove;
    request.flip = flip;
    // Cada tipo deja libre lo suyo y fija el resto (en la rígida, solo la distancia).
    const bool turns = type == JointType::Revolute || type == JointType::Cylindrical || type == JointType::PinSlot ||
                       type == JointType::Planar || type == JointType::Ball;
    const bool slidesX = type == JointType::Slider || type == JointType::PinSlot || type == JointType::Planar;
    const bool slidesY = type == JointType::Planar;
    const bool tilts = type == JointType::Ball;
    std::optional<std::string> warning;
    const auto limit = [&](double value, bool limited, double lo, double hi, const char* what) {
        const double clamped = clampTo(value, limited, lo, hi);
        if (clamped != value && !warning) {
            warning = std::string("Fuera de los límites de la unión: ") + what + " se quedó en el tope";
        }
        return clamped;
    };
    request.offset = type == JointType::Cylindrical ? limit(offset, limitSlide, slideMin, slideMax, "el deslizamiento") : offset;
    request.angle = turns ? limit(angle, limitAngle, angleMin, angleMax, "el giro") : 0.0;
    request.slide = slidesX ? limit(slide, limitSlide, slideMin, slideMax, "el deslizamiento") : 0.0;
    request.slideY = slidesY ? slideY : 0.0;
    request.tiltX = tilts ? tiltX : 0.0;
    request.tiltY = tilts ? tiltY : 0.0;
    const PlacementResult placed = applyPlacement(state, tag(), request);
    if (type != JointType::Rigid) {
        JointAxis axis;
        axis.origin = placed.origin;
        axis.direction = placed.direction;
        axis.xAxis = placed.xAxis;
        axis.dof = (type == JointType::Slider) ? 1 : 0;
        axis.moved = placed.moved;
        state.joints[id()] = axis;
    }
    return warning;
}

namespace {

// Las dependencias de una referencia: la de una cara o arista, o la del cuerpo de un «@axis:».
void addPickDependencies(std::vector<FeatureId>& ids, const geometry::ElementRef& ref)
{
    if (ref.name.rfind(kAxisPrefix, 0) == 0) {
        addAll(ids, geometry::ElementRef{ref.name.substr(kAxisPrefix.size()) + ":", 0});
    } else {
        addAll(ids, ref);
    }
}

} // namespace

std::vector<FeatureId> JointFeature::dependencies() const
{
    std::vector<FeatureId> ids;
    addPickDependencies(ids, fixedFace);
    addPickDependencies(ids, movingFace);
    for (const std::string& other : alsoMove) {
        addAll(ids, geometry::ElementRef{other + ":", 0});
    }
    return ids;
}

std::vector<Feature::NumericField> JointFeature::numericFields()
{
    return {field("offset", "Distancia", offset),
            field("angle", "Ángulo", angle, kRadiansPerDegree),
            field("slide", "Desplazamiento", slide),
            field("slideY", "Desplazamiento Y", slideY),
            field("tiltX", "Inclinación X", tiltX, kRadiansPerDegree),
            field("tiltY", "Inclinación Y", tiltY, kRadiansPerDegree)};
}

// --- Alinear -------------------------------------------------------------------

std::optional<std::string> AlignFeature::compute(ModelState& state) const
{
    PlacementRequest request;
    request.moving = moving;
    request.target = target;
    request.alsoMove = alsoMove;
    request.flip = flip;
    request.offset = offset;
    request.angle = angle;
    applyPlacement(state, tag(), request);
    return std::nullopt;
}

std::vector<FeatureId> AlignFeature::dependencies() const
{
    std::vector<FeatureId> ids;
    addAll(ids, moving.ref);
    addAll(ids, target.ref);
    for (const std::string& other : alsoMove) {
        addAll(ids, geometry::ElementRef{other + ":", 0});
    }
    return ids;
}

std::vector<Feature::NumericField> AlignFeature::numericFields()
{
    return {field("offset", "Distancia", offset), field("angle", "Ángulo", angle, kRadiansPerDegree)};
}

// --- Enlace de movimiento ------------------------------------------------------

std::optional<std::string> MotionLinkFeature::compute(ModelState& state) const
{
    if (members.size() < 2) {
        throw FeatureError("Un enlace necesita al menos dos uniones");
    }
    // Primero se mira todo y luego se mueve (si una falla, no se mueve ninguna).
    struct Step {
        const JointAxis* axis;
        double amount;
    };
    std::vector<Step> steps;
    for (const Member& member : members) {
        const auto found = state.joints.find(member.joint);
        if (found == state.joints.end()) {
            throw FeatureError("Una de las uniones del enlace ya no existe o es rígida");
        }
        steps.push_back({&found->second, member.ratio * position});
    }
    try {
        std::map<std::string, geometry::Shape> moved;
        for (const Step& step : steps) {
            for (const std::string& key : step.axis->moved) {
                const auto body = std::ranges::find_if(state.bodies, [&](const Body& b) { return b.key == key; });
                if (body == state.bodies.end()) {
                    throw FeatureError("Un cuerpo de una unión del enlace ya no existe");
                }
                geometry::Shape& shape = moved.try_emplace(key, body->shape).first->second;
                if (step.axis->dof == 0) {
                    geometry::Axis axis;
                    axis.point = step.axis->origin;
                    axis.direction = step.axis->direction;
                    shape = geometry::moveBody(tag(), shape, {}, axis, step.amount, false, {}, step.axis->origin);
                } else {
                    const sketch::Vec3 along = step.axis->dof == 1 ? step.axis->xAxis : step.axis->direction;
                    shape = geometry::moveBody(tag(), shape, {along.x * step.amount, along.y * step.amount, along.z * step.amount},
                                               geometry::Axis{}, 0.0, false, {}, {});
                }
            }
        }
        for (auto& [key, shape] : moved) {
            std::ranges::find_if(state.bodies, [&](const Body& b) { return b.key == key; })->shape = std::move(shape);
        }
    } catch (const geometry::GeometryError& e) {
        throw FeatureError(e.what());
    }
    return std::nullopt;
}

std::vector<FeatureId> MotionLinkFeature::dependencies() const
{
    std::vector<FeatureId> ids;
    for (const Member& member : members) {
        ids.push_back(member.joint);
    }
    return ids;
}

std::vector<Feature::NumericField> MotionLinkFeature::numericFields()
{
    return {field("position", "Posición", position, kRadiansPerDegree)};
}

// --- Eje de construcción ------------------------------------------------------

std::optional<std::string> ConstructionAxisFeature::compute(ModelState& state) const
{
    std::optional<std::string> warning;
    const sketch::Plane a = resolvePlane(state, first, "del primer plano", "se usa su última posición", warning);
    if (!useSecond) {
        state.axes[id()] = {a.origin, a.normal};
        return warning;
    }
    const sketch::Plane b = resolvePlane(state, second, "del segundo plano", "se usa su última posición", warning);
    const sketch::Vec3& n1 = a.normal;
    const sketch::Vec3& n2 = b.normal;
    const auto cross = [](const sketch::Vec3& u, const sketch::Vec3& v) {
        return sketch::Vec3{u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z, u.x * v.y - u.y * v.x};
    };
    const auto dot = [](const sketch::Vec3& u, const sketch::Vec3& v) { return u.x * v.x + u.y * v.y + u.z * v.z; };
    const sketch::Vec3 d = cross(n1, n2);
    const double dd = dot(d, d);
    if (dd < 1e-12) {
        throw FeatureError("Los dos planos son paralelos: no tienen una recta en común");
    }
    const double h1 = dot(n1, a.origin);
    const double h2 = dot(n2, b.origin);
    const sketch::Vec3 u = cross(n2, d);
    const sketch::Vec3 v = cross(d, n1);
    const sketch::Vec3 point{(h1 * u.x + h2 * v.x) / dd, (h1 * u.y + h2 * v.y) / dd, (h1 * u.z + h2 * v.z) / dd};
    const double len = std::sqrt(dd);
    state.axes[id()] = {point, {d.x / len, d.y / len, d.z / len}};
    return warning;
}

std::vector<FeatureId> ConstructionAxisFeature::dependencies() const
{
    std::vector<FeatureId> ids;
    addAttachmentDependencies(ids, first);
    if (useSecond) {
        addAttachmentDependencies(ids, second);
    }
    return ids;
}

// --- Patrón ------------------------------------------------------------------

namespace {

// Cómo se reparten las copias de un patrón (lineal, circular o a lo largo de una trayectoria).
geometry::PatternSpec patternSpec(const PatternFeature& feature, const ModelState& state)
{
    geometry::PatternSpec spec;
    spec.circular = feature.layout == PatternLayout::Circular;
    spec.axis.direction = {feature.axis == 0 ? 1.0 : 0.0, feature.axis == 1 ? 1.0 : 0.0, feature.axis == 2 ? 1.0 : 0.0};
    if (feature.axisRef != 0) {
        const auto line = state.axes.find(feature.axisRef);
        if (line == state.axes.end()) {
            throw FeatureError("El eje de construcción del patrón ya no existe");
        }
        spec.axis.point = line->second.point;
        spec.axis.direction = line->second.direction;
    }
    spec.spacing = feature.spacing;
    spec.angle = feature.angle;
    spec.count = feature.count;
    if (feature.layout == PatternLayout::Path) {
        const auto out = state.sketches.find(feature.pathSketch);
        if (out == state.sketches.end()) {
            throw FeatureError("Falta el sketch de la trayectoria del patrón");
        }
        try {
            std::vector<sketch::Vec3> tangents;
            const std::vector<sketch::Vec3> points = geometry::pathPoints(*out->second.sketch, out->second.plane, feature.path, feature.count,
                                                                         feature.followPath ? &tangents : nullptr);
            if (feature.followPath) {
                spec.tangents = tangents;
                spec.tangentOrigin = points[0];
            }
            for (std::size_t i = 1; i < points.size(); ++i) {
                spec.offsets.push_back({points[i].x - points[0].x, points[i].y - points[0].y, points[i].z - points[0].z});
            }
        } catch (const geometry::GeometryError& e) {
            throw FeatureError(e.what());
        }
    }
    return spec;
}

} // namespace

std::optional<std::string> PatternFeature::compute(ModelState& state) const
{
    if (sourceFeature != 0) {
        // Repetir una operación (un agujero, un corte…): su sólido se copia con el patrón y se aplica igual que ella.
        const auto original = state.operationTools.find(sourceFeature);
        if (original == state.operationTools.end()) {
            throw FeatureError("La operación que se iba a repetir ya no existe");
        }
        const geometry::PatternSpec spec = patternSpec(*this, state);
        try {
            const std::vector<geometry::Shape> copies = geometry::pattern(tag(), original->second.shape, spec);
            return applyOperation(state, *this, Operation(original->second.operation), geometry::fuseBalanced(copies));
        } catch (const geometry::GeometryError& e) {
            throw FeatureError(e.what());
        }
    }
    const auto source = std::ranges::find_if(state.bodies, [&](const Body& b) { return b.key == body; });
    if (source == state.bodies.end()) {
        throw FeatureError("El cuerpo que se iba a repetir ya no existe");
    }
    const geometry::PatternSpec spec = patternSpec(*this, state);
    // El cuerpo y los demás de la lista (los que ya no existen se avisan); cada uno se repite con sus copias.
    std::vector<std::string> keys = {body};
    int missing = 0;
    for (const std::string& key : alsoBodies) {
        if (std::ranges::none_of(state.bodies, [&](const Body& b) { return b.key == key; })) {
            ++missing;
        } else if (key != body) {
            keys.push_back(key);
        }
    }
    try {
        std::size_t index = 0;
        for (const std::string& key : keys) {
            const auto found = std::ranges::find_if(state.bodies, [&](const Body& b) { return b.key == key; });
            // Cada cuerpo lleva su número en el nombre de las caras, para que no se repitan.
            const std::string name = index == 0 ? tag() : tag() + ":c" + std::to_string(index);
            ++index;
            const geometry::Shape original = found->shape;
            const std::vector<geometry::Shape> copies = geometry::pattern(name, original, spec);
            // De dos en dos, en árbol (no todas juntas, para que si se solapan entre sí cada par lo resuelva el kernel,
            // ni una a una a un resultado cada vez mayor: con 50 copias eso era lo que más tardaba).
            if (operation == Operation::NewBody) {
                state.bodies.push_back({index == 1 ? tag() : name, newBodyName(state, *this), geometry::fuseBalanced(copies)});
            } else {
                std::vector<geometry::Shape> parts;
                parts.push_back(original);
                parts.insert(parts.end(), copies.begin(), copies.end());
                std::ranges::find_if(state.bodies, [&](const Body& b) { return b.key == key; })->shape = geometry::fuseBalanced(std::move(parts));
            }
        }
    } catch (const geometry::GeometryError& e) {
        throw FeatureError(e.what());
    }
    if (missing > 0) {
        return missing == 1 ? "Un cuerpo de la lista ya no existe." : "Algunos cuerpos de la lista ya no existen.";
    }
    return std::nullopt;
}

std::vector<FeatureId> PatternFeature::dependencies() const
{
    std::vector<FeatureId> ids;
    if (sourceFeature != 0) {
        ids.push_back(sourceFeature);
    } else {
        addAll(ids, geometry::ElementRef{body + ":", 0});  // la operación que creó el cuerpo
        for (const std::string& other : alsoBodies) {
            addAll(ids, geometry::ElementRef{other + ":", 0});
        }
    }
    if (axisRef != 0) {
        ids.push_back(axisRef);
    }
    if (layout == PatternLayout::Path && pathSketch != 0) {
        ids.push_back(pathSketch);
    }
    return ids;
}

// --- Agujeros ----------------------------------------------------------------

geometry::Shape HoleFeature::tool(const ModelState& state) const
{
    if (onFace) {
        std::optional<std::string> ignored;
        const sketch::Plane plane = resolvePlane(state, base, "del agujero", "se usa su última posición", ignored);
        try {
            return geometry::holeTool(tag(), plane, {sketch::Vec2{x, y}}, spec, flip, state.shapes());
        } catch (const geometry::GeometryError& e) {
            throw FeatureError(e.what());
        }
    }
    const auto out = state.sketches.find(sketch);
    if (out == state.sketches.end()) {
        throw FeatureError("Falta el sketch que marca los agujeros");
    }
    // Centros de círculos y puntos sueltos, sin repetir (un punto en el centro
    // de un círculo es el mismo agujero).
    std::vector<sketch::Vec2> centers;
    const auto add = [&](sketch::Vec2 p) {
        const bool known = std::ranges::any_of(centers, [&](const sketch::Vec2& q) {
            return std::abs(q.x - p.x) < 1e-6 && std::abs(q.y - p.y) < 1e-6;
        });
        if (!known) {
            centers.push_back(p);
        }
    };
    for (const sketch::Entity& e : out->second.sketch->entities()) {
        if (e.reference) {
            continue;
        }
        if (const auto* circle = std::get_if<sketch::Circle>(&e.geometry); circle && !e.construction) {
            add(circle->center);
        } else if (const auto* point = std::get_if<sketch::Point>(&e.geometry)) {
            add(point->position);
        }
    }
    try {
        return geometry::holeTool(tag(), out->second.plane, centers, spec, flip, state.shapes());
    } catch (const geometry::GeometryError& e) {
        throw FeatureError(e.what());
    }
}

std::optional<std::string> HoleFeature::compute(ModelState& state) const
{
    const geometry::Shape solid = tool(state);
    try {
        return applyOperation(state, *this, Operation::Cut, solid);
    } catch (const geometry::GeometryError& e) {
        throw FeatureError(e.what());
    }
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
        state.bodies.push_back({key, newBodyName(state, *this), shapes[i]});
    }
    return joinWarnings(warnings);
}

} // namespace nuf::document
