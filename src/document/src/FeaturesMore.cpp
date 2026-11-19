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
    return {field("diameter", "Diámetro", spec.diameter), field("depth", "Profundidad", spec.depth),
            field("headDiameter", "Diámetro de la cabeza", spec.headDiameter),
            field("headDepth", "Profundidad de la cabeza", spec.headDepth),
            field("headAngle", "Ángulo del cono", spec.headAngle, kRadiansPerDegree)};
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
    if (copy && sources.size() > 1) {
        throw FeatureError("Todavía no se pueden copiar varios cuerpos a la vez: muévelos, o copia de uno en uno");
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
            state.bodies.push_back({tag(), newBodyName(state, *this), std::move(results.front())});
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

std::optional<std::string> JointFeature::compute(ModelState& state) const
{
    const auto fixedHit = state.findFace(fixedFace);
    const auto movingHit = state.findFace(movingFace);
    if (!fixedHit) {
        throw FeatureError("La cara fija de la unión ya no existe");
    }
    if (!movingHit) {
        throw FeatureError("La cara móvil de la unión ya no existe");
    }
    if (fixedHit->body == movingHit->body) {
        throw FeatureError("Las dos caras son del mismo cuerpo: elige caras de cuerpos distintos");
    }
    const Body& fixedBody = state.bodies[fixedHit->body];
    Body& movingBody = state.bodies[movingHit->body];
    const auto a = fixedBody.shape.facePlane(fixedHit->face);
    const auto b = movingBody.shape.facePlane(movingHit->face);
    if (!a || !b) {
        throw FeatureError("Por ahora las uniones solo funcionan con caras planas");
    }
    const sketch::Vec3 centerA = fixedBody.shape.faceCenter(fixedHit->face);
    const sketch::Vec3 centerB = movingBody.shape.faceCenter(movingHit->face);

    const auto cross = [](const sketch::Vec3& u, const sketch::Vec3& v) {
        return sketch::Vec3{u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z, u.x * v.y - u.y * v.x};
    };
    // Sistema de origen: el centro de la cara móvil con los ejes de su plano.
    sketch::Plane from;
    from.origin = centerB;
    from.xAxis = b->xAxis;
    from.normal = b->normal;
    from.yAxis = cross(from.normal, from.xAxis);
    // Destino: el centro de la cara fija, con la normal opuesta (caras enfrentadas)
    // y el giro y los desplazamientos pedidos.
    sketch::Plane to;
    to.normal = flip ? a->normal : sketch::Vec3{-a->normal.x, -a->normal.y, -a->normal.z};
    to.xAxis = rotateAbout(a->xAxis, a->normal, angle);
    to.yAxis = cross(to.normal, to.xAxis);
    to.origin = {centerA.x + a->normal.x * offset + a->xAxis.x * slide, centerA.y + a->normal.y * offset + a->xAxis.y * slide,
                 centerA.z + a->normal.z * offset + a->xAxis.z * slide};
    try {
        movingBody.shape = geometry::placeBody(tag(), movingBody.shape, from, to);
    } catch (const geometry::GeometryError& e) {
        throw FeatureError(e.what());
    }
    return std::nullopt;
}

std::vector<FeatureId> JointFeature::dependencies() const
{
    std::vector<FeatureId> ids;
    addAll(ids, fixedFace);
    addAll(ids, movingFace);
    return ids;
}

std::vector<Feature::NumericField> JointFeature::numericFields()
{
    return {field("offset", "Distancia", offset), field("angle", "Ángulo", angle, kRadiansPerDegree),
            field("slide", "Desplazamiento", slide)};
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

std::optional<std::string> PatternFeature::compute(ModelState& state) const
{
    const auto source = std::ranges::find_if(state.bodies, [&](const Body& b) { return b.key == body; });
    if (source == state.bodies.end()) {
        throw FeatureError("El cuerpo que se iba a repetir ya no existe");
    }
    geometry::PatternSpec spec;
    spec.circular = layout == PatternLayout::Circular;
    spec.axis.direction = {axis == 0 ? 1.0 : 0.0, axis == 1 ? 1.0 : 0.0, axis == 2 ? 1.0 : 0.0};
    if (axisRef != 0) {
        const auto line = state.axes.find(axisRef);
        if (line == state.axes.end()) {
            throw FeatureError("El eje de construcción del patrón ya no existe");
        }
        spec.axis.point = line->second.point;
        spec.axis.direction = line->second.direction;
    }
    spec.spacing = spacing;
    spec.angle = angle;
    spec.count = count;
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
    addAll(ids, geometry::ElementRef{body + ":", 0});  // la operación que creó el cuerpo
    for (const std::string& other : alsoBodies) {
        addAll(ids, geometry::ElementRef{other + ":", 0});
    }
    if (axisRef != 0) {
        ids.push_back(axisRef);
    }
    return ids;
}

// --- Agujeros ----------------------------------------------------------------

geometry::Shape HoleFeature::tool(const ModelState& state) const
{
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
