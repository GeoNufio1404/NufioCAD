#include <nuf/io/DocumentFile.hpp>

#include <nuf/document/Features.hpp>

#include <nlohmann/json.hpp>

#include <array>
#include <chrono>
#include <cmath>
#include <thread>

namespace nuf::io {

namespace {

using Json = nlohmann::json;
using namespace nuf::document;
namespace sk = nuf::sketch;
namespace geo = nuf::geometry;

constexpr const char* kUnits = "mm";

// ---------------------------------------------------------------------------
// Enumeraciones como texto: reordenar un enum en el código no debe cambiar el
// significado de los archivos ya guardados.
// ---------------------------------------------------------------------------

template <typename E, std::size_t N>
using Table = std::array<std::pair<E, const char*>, N>;

template <typename E, std::size_t N>
const char* nameOf(const Table<E, N>& table, E value)
{
    for (const auto& [e, text] : table) {
        if (e == value) {
            return text;
        }
    }
    throw NufError("Valor interno desconocido al guardar");
}

template <typename E, std::size_t N>
E parseEnum(const Table<E, N>& table, const std::string& text, const char* what)
{
    for (const auto& [e, name] : table) {
        if (text == name) {
            return e;
        }
    }
    throw NufError(std::string("Valor desconocido en el archivo (") + what + "): " + text);
}

using CT = sk::ConstraintType;
const Table<CT, 22> kConstraintTypes{{
    {CT::Coincident, "coincident"}, {CT::PointOnObject, "pointOnObject"}, {CT::Horizontal, "horizontal"},
    {CT::Vertical, "vertical"}, {CT::Parallel, "parallel"}, {CT::Perpendicular, "perpendicular"},
    {CT::Tangent, "tangent"}, {CT::Equal, "equal"}, {CT::Collinear, "collinear"},
    {CT::Concentric, "concentric"}, {CT::Midpoint, "midpoint"}, {CT::Symmetric, "symmetric"},
    {CT::Fix, "fix"}, {CT::Curvature, "curvature"}, {CT::Distance, "distance"},
    {CT::DistanceX, "distanceX"}, {CT::DistanceY, "distanceY"}, {CT::Angle, "angle"},
    {CT::Radius, "radius"}, {CT::Diameter, "diameter"}, {CT::MajorRadius, "majorRadius"},
    {CT::MinorRadius, "minorRadius"},
}};

const Table<sk::Part, 5> kParts{{
    {sk::Part::Whole, "whole"}, {sk::Part::Start, "start"}, {sk::Part::End, "end"},
    {sk::Part::Center, "center"}, {sk::Part::Pole, "pole"},
}};

const Table<OriginPlane, 3> kOriginPlanes{{{OriginPlane::XY, "XY"}, {OriginPlane::XZ, "XZ"}, {OriginPlane::YZ, "YZ"}}};

const Table<Operation, 4> kOperations{{
    {Operation::Join, "join"}, {Operation::Cut, "cut"}, {Operation::Intersect, "intersect"},
    {Operation::NewBody, "newBody"},
}};

const Table<geo::Extent, 3> kExtents{{
    {geo::Extent::Distance, "distance"}, {geo::Extent::ThroughAll, "throughAll"}, {geo::Extent::ToFace, "toFace"},
}};

const Table<geo::ExtrudeDirection, 3> kDirections{{
    {geo::ExtrudeDirection::One, "one"}, {geo::ExtrudeDirection::Symmetric, "symmetric"},
    {geo::ExtrudeDirection::Two, "two"},
}};

const Table<geo::RevolveExtent, 2> kRevolveExtents{{
    {geo::RevolveExtent::Angle, "angle"}, {geo::RevolveExtent::Full, "full"},
}};

const Table<geo::HoleKind, 3> kHoleKinds{{
    {geo::HoleKind::Simple, "simple"}, {geo::HoleKind::Counterbore, "counterbore"},
    {geo::HoleKind::Countersink, "countersink"},
}};

const Table<PatternLayout, 2> kPatternLayouts{{
    {PatternLayout::Linear, "linear"}, {PatternLayout::Circular, "circular"},
}};

const Table<JointType, 3> kJointTypes{{
    {JointType::Rigid, "rigid"}, {JointType::Revolute, "revolute"}, {JointType::Slider, "slider"},
}};

const Table<AxisRef::Kind, 6> kAxisKinds{{
    {AxisRef::Kind::OriginX, "originX"}, {AxisRef::Kind::OriginY, "originY"}, {AxisRef::Kind::OriginZ, "originZ"},
    {AxisRef::Kind::SketchLine, "sketchLine"}, {AxisRef::Kind::Edge, "edge"},
    {AxisRef::Kind::Construction, "construction"},
}};

// ---------------------------------------------------------------------------
// Lectura con comprobaciones
// ---------------------------------------------------------------------------

double finite(const Json& j)
{
    const double value = j.get<double>();
    if (!std::isfinite(value)) {
        throw NufError("Número no válido en el archivo");
    }
    return value;
}

double number(const Json& j, const char* key)
{
    return finite(j.at(key));
}

// Cantidades enteras que son ids o índices: no negativas y razonables.
std::uint64_t unsignedValue(const Json& j, const char* key)
{
    const Json& v = j.at(key);
    if (!v.is_number_unsigned()) {
        throw NufError(std::string("Se esperaba un entero no negativo en «") + key + "»");
    }
    return v.get<std::uint64_t>();
}

std::string text(const Json& j, const char* key)
{
    return j.at(key).get<std::string>();
}

// ---------------------------------------------------------------------------
// Tipos pequeños
// ---------------------------------------------------------------------------

Json toJson(sk::Vec2 v) { return Json::array({v.x, v.y}); }
sk::Vec2 vec2(const Json& j)
{
    if (!j.is_array() || j.size() != 2) {
        throw NufError("Punto 2D mal formado");
    }
    return {finite(j[0]), finite(j[1])};
}

Json toJson(sk::Vec3 v) { return Json::array({v.x, v.y, v.z}); }
sk::Vec3 vec3(const Json& j)
{
    if (!j.is_array() || j.size() != 3) {
        throw NufError("Punto 3D mal formado");
    }
    return {finite(j[0]), finite(j[1]), finite(j[2])};
}

Json toJson(const sk::Plane& p)
{
    return {{"origin", toJson(p.origin)}, {"x", toJson(p.xAxis)}, {"y", toJson(p.yAxis)}, {"n", toJson(p.normal)}};
}
sk::Plane plane(const Json& j)
{
    sk::Plane p;
    p.origin = vec3(j.at("origin"));
    p.xAxis = vec3(j.at("x"));
    p.yAxis = vec3(j.at("y"));
    p.normal = vec3(j.at("n"));
    return p;
}

Json toJson(const geo::ElementRef& r) { return {{"name", r.name}, {"occurrence", r.occurrence}}; }
Json toJson(const std::vector<geo::ElementRef>& refs)
{
    Json list = Json::array();
    for (const geo::ElementRef& r : refs) {
        list.push_back(toJson(r));
    }
    return list;
}
geo::ElementRef elementRef(const Json& j)
{
    geo::ElementRef r;
    r.name = text(j, "name");
    r.occurrence = j.at("occurrence").get<int>();
    return r;
}

// ---------------------------------------------------------------------------
// Sketch
// ---------------------------------------------------------------------------

Json toJson(const sk::Geometry& g)
{
    if (const auto* p = std::get_if<sk::Point>(&g)) {
        return {{"t", "point"}, {"at", toJson(p->position)}};
    }
    if (const auto* l = std::get_if<sk::Line>(&g)) {
        return {{"t", "line"}, {"start", toJson(l->start)}, {"end", toJson(l->end)}};
    }
    if (const auto* c = std::get_if<sk::Circle>(&g)) {
        return {{"t", "circle"}, {"center", toJson(c->center)}, {"radius", c->radius}};
    }
    if (const auto* a = std::get_if<sk::Arc>(&g)) {
        return {{"t", "arc"}, {"center", toJson(a->center)}, {"radius", a->radius},
                {"startAngle", a->startAngle}, {"endAngle", a->endAngle}};
    }
    const auto ellipse = [](const sk::Ellipse& e) {
        return Json{{"center", toJson(e.center)}, {"major", e.majorRadius}, {"minor", e.minorRadius},
                    {"rotation", e.rotation}};
    };
    if (const auto* e = std::get_if<sk::Ellipse>(&g)) {
        Json j = ellipse(*e);
        j["t"] = "ellipse";
        return j;
    }
    if (const auto* ea = std::get_if<sk::EllipseArc>(&g)) {
        Json j = ellipse(ea->ellipse);
        j["t"] = "ellipseArc";
        j["startParam"] = ea->startParam;
        j["endParam"] = ea->endParam;
        return j;
    }
    const auto& s = std::get<sk::BSpline>(g);
    Json poles = Json::array();
    for (const sk::Vec2& p : s.poles) {
        poles.push_back(toJson(p));
    }
    return {{"t", "bspline"}, {"poles", poles}, {"weights", s.weights}, {"degree", s.degree}};
}

sk::Ellipse ellipseOf(const Json& j)
{
    sk::Ellipse e;
    e.center = vec2(j.at("center"));
    e.majorRadius = number(j, "major");
    e.minorRadius = number(j, "minor");
    e.rotation = number(j, "rotation");
    return e;
}

sk::Geometry geometry(const Json& j)
{
    const std::string t = text(j, "t");
    if (t == "point") {
        return sk::Point{vec2(j.at("at"))};
    }
    if (t == "line") {
        return sk::Line{vec2(j.at("start")), vec2(j.at("end"))};
    }
    if (t == "circle") {
        return sk::Circle{vec2(j.at("center")), number(j, "radius")};
    }
    if (t == "arc") {
        return sk::Arc{vec2(j.at("center")), number(j, "radius"), number(j, "startAngle"), number(j, "endAngle")};
    }
    if (t == "ellipse") {
        return ellipseOf(j);
    }
    if (t == "ellipseArc") {
        return sk::EllipseArc{ellipseOf(j), number(j, "startParam"), number(j, "endParam")};
    }
    if (t == "bspline") {
        sk::BSpline s;
        for (const Json& p : j.at("poles")) {
            s.poles.push_back(vec2(p));
        }
        for (const Json& w : j.at("weights")) {
            s.weights.push_back(finite(w));
        }
        s.degree = j.at("degree").get<int>();
        if (s.poles.size() < 2 || (!s.weights.empty() && s.weights.size() != s.poles.size())) {
            throw NufError("Spline mal formada");
        }
        for (double w : s.weights) {
            if (w <= 0.0) {
                throw NufError("Spline con pesos no positivos");
            }
        }
        return s;
    }
    throw NufError("Tipo de geometría desconocido: " + t);
}

Json toJson(const sk::Ref& r)
{
    return {{"entity", r.entity}, {"part", nameOf(kParts, r.part)}, {"index", r.index}};
}
sk::Ref ref(const Json& j)
{
    sk::Ref r;
    r.entity = static_cast<sk::EntityId>(unsignedValue(j, "entity"));
    r.part = parseEnum(kParts, text(j, "part"), "parte");
    r.index = j.at("index").get<int>();
    return r;
}

Json toJson(const sk::Sketch& s)
{
    Json entities = Json::array();
    for (const sk::Entity& e : s.entities()) {
        Json je = toJson(e.geometry);
        je["id"] = e.id;
        je["construction"] = e.construction;
        je["reference"] = e.reference;
        je["locked"] = e.locked;
        if (e.link) {
            je["link"] = {{"edge", e.link->edge}, {"occurrence", e.link->occurrence}, {"piece", e.link->piece}};
        }
        if (e.textOwner != 0) {
            je["textOwner"] = e.textOwner;
        }
        if (e.text) {
            je["text"] = {{"text", e.text->text}, {"family", e.text->family}, {"height", e.text->height},
                          {"x", e.text->at.x}, {"y", e.text->at.y}};
        }
        entities.push_back(std::move(je));
    }
    Json constraints = Json::array();
    for (const sk::Constraint& c : s.constraints()) {
        constraints.push_back({{"id", c.id}, {"type", nameOf(kConstraintTypes, c.type)}, {"first", toJson(c.first)},
                               {"second", toJson(c.second)}, {"third", toJson(c.third)}, {"value", c.value},
                               {"driving", c.driving}});
    }
    return {{"entities", entities}, {"constraints", constraints}, {"nextEntity", s.nextEntityId()},
            {"nextConstraint", s.nextConstraintId()}};
}

sk::Sketch sketchOf(const Json& j)
{
    std::vector<sk::Entity> entities;
    for (const Json& je : j.at("entities")) {
        sk::Entity e;
        e.id = static_cast<sk::EntityId>(unsignedValue(je, "id"));
        e.geometry = geometry(je);
        e.construction = je.at("construction").get<bool>();
        e.reference = je.at("reference").get<bool>();
        e.locked = je.at("locked").get<bool>();
        if (je.contains("link")) {
            const Json& jl = je.at("link");
            e.link = sk::ModelLink{text(jl, "edge"), jl.at("occurrence").get<int>(), jl.at("piece").get<int>()};
        }
        // Los archivos anteriores no traen el texto: sus letras siguen siendo curvas sueltas.
        e.textOwner = static_cast<sk::EntityId>(je.value("textOwner", 0ULL));
        if (je.contains("text")) {
            const Json& jt = je.at("text");
            e.text = sk::TextInfo{text(jt, "text"), text(jt, "family"), jt.at("height").get<double>(),
                                  {jt.at("x").get<double>(), jt.at("y").get<double>()}};
        }
        entities.push_back(std::move(e));
    }
    std::vector<sk::Constraint> constraints;
    for (const Json& jc : j.at("constraints")) {
        sk::Constraint c;
        c.id = static_cast<sk::ConstraintId>(unsignedValue(jc, "id"));
        c.type = parseEnum(kConstraintTypes, text(jc, "type"), "restricción");
        c.first = ref(jc.at("first"));
        c.second = ref(jc.at("second"));
        c.third = ref(jc.at("third"));
        c.value = number(jc, "value");
        c.driving = jc.at("driving").get<bool>();
        constraints.push_back(c);
    }
    try {
        return sk::Sketch::restore(std::move(entities), std::move(constraints),
                                   static_cast<sk::EntityId>(unsignedValue(j, "nextEntity")),
                                   static_cast<sk::ConstraintId>(unsignedValue(j, "nextConstraint")));
    } catch (const std::invalid_argument& e) {
        throw NufError(std::string("Sketch incoherente: ") + e.what());
    }
}

// ---------------------------------------------------------------------------
// Operaciones
// ---------------------------------------------------------------------------

Json toJson(const std::vector<ProfileRef>& profiles)
{
    Json result = Json::array();
    for (const ProfileRef& p : profiles) {
        result.push_back({{"sketch", p.sketch}, {"outer", p.outer}});
    }
    return result;
}
std::vector<ProfileRef> profilesOf(const Json& j)
{
    std::vector<ProfileRef> result;
    for (const Json& jp : j) {
        ProfileRef p;
        p.sketch = unsignedValue(jp, "sketch");
        for (const Json& id : jp.at("outer")) {
            if (!id.is_number_unsigned()) {
                throw NufError("Perfil mal formado");
            }
            p.outer.push_back(id.get<sk::EntityId>());
        }
        result.push_back(std::move(p));
    }
    return result;
}

Json toJson(const geo::ExtrudeSide& s)
{
    return {{"extent", nameOf(kExtents, s.extent)}, {"distance", s.distance}, {"taper", s.taper},
            {"face", toJson(s.face)}};
}
geo::ExtrudeSide sideOf(const Json& j)
{
    geo::ExtrudeSide s;
    s.extent = parseEnum(kExtents, text(j, "extent"), "extensión");
    s.distance = number(j, "distance");
    s.taper = number(j, "taper");
    s.face = elementRef(j.at("face"));
    return s;
}

Json toJson(const SketchAttachment& a)
{
    Json j{{"face", toJson(a.face)}, {"lastPlane", toJson(a.lastPlane)}};
    if (a.reference != 0) {
        j["reference"] = a.reference;
    }
    if (a.origin) {
        j["origin"] = nameOf(kOriginPlanes, *a.origin);
    }
    return j;
}
SketchAttachment attachmentOf(const Json& j)
{
    SketchAttachment a;
    a.origin.reset();
    if (j.contains("origin")) {
        a.origin = parseEnum(kOriginPlanes, text(j, "origin"), "plano de origen");
    }
    a.face = elementRef(j.at("face"));
    a.lastPlane = plane(j.at("lastPlane"));
    if (j.contains("reference")) {
        a.reference = unsignedValue(j, "reference");
    }
    if (!a.origin && a.face.empty() && a.reference == 0) {
        throw NufError("Un sketch no dice dónde va");
    }
    return a;
}

Json toJson(const AxisRef& a)
{
    Json j{{"kind", nameOf(kAxisKinds, a.kind)}, {"sketch", a.sketch}, {"entity", a.entity}, {"edge", toJson(a.edge)}};
    if (a.reference != 0) {
        j["reference"] = a.reference;
    }
    return j;
}
AxisRef axisOf(const Json& j)
{
    AxisRef a;
    a.kind = parseEnum(kAxisKinds, text(j, "kind"), "eje");
    a.sketch = unsignedValue(j, "sketch");
    a.entity = static_cast<sk::EntityId>(unsignedValue(j, "entity"));
    a.edge = elementRef(j.at("edge"));
    if (j.contains("reference")) {
        a.reference = unsignedValue(j, "reference");
    }
    return a;
}

// Dónde va el sólido de una importación dentro de la tabla de datos.
std::string blobKey(FeatureId id)
{
    return "import:" + std::to_string(id);
}

FeatureRow rowOf(const Feature& f)
{
    FeatureRow row;
    row.id = static_cast<std::int64_t>(f.id());
    row.name = f.name();
    row.suppressed = f.suppressed();
    Json params;
    switch (f.kind()) {
    case FeatureKind::Sketch: {
        const auto& s = static_cast<const SketchFeature&>(f);
        row.type = "sketch";
        params = {{"attachment", toJson(s.attachment())}, {"sketch", toJson(s.sketch())}};
        break;
    }
    case FeatureKind::Extrude: {
        const auto& e = static_cast<const ExtrudeFeature&>(f);
        row.type = "extrude";
        params = {{"profiles", toJson(e.profiles)}, {"face", toJson(e.face)},
                  {"direction", nameOf(kDirections, e.spec.direction)}, {"side1", toJson(e.spec.side1)},
                  {"side2", toJson(e.spec.side2)}, {"startOffset", e.spec.startOffset},
                  {"operation", nameOf(kOperations, e.operation)}};
        break;
    }
    case FeatureKind::Revolve: {
        const auto& r = static_cast<const RevolveFeature&>(f);
        row.type = "revolve";
        params = {{"profiles", toJson(r.profiles)}, {"axis", toJson(r.axis)},
                  {"extent", nameOf(kRevolveExtents, r.spec.extent)},
                  {"direction", nameOf(kDirections, r.spec.direction)}, {"angle1", r.spec.angle1},
                  {"angle2", r.spec.angle2}, {"operation", nameOf(kOperations, r.operation)}};
        break;
    }
    case FeatureKind::Fillet:
    case FeatureKind::Chamfer: {
        const auto& e = static_cast<const EdgeFeature&>(f);
        row.type = f.kind() == FeatureKind::Fillet ? "fillet" : "chamfer";
        params = {{"edges", toJson(e.edges)}, {"size", e.size}};
        if (f.kind() == FeatureKind::Fillet && e.variable) {
            params["variable"] = true;
            params["size2"] = e.size2;
        }
        if (f.kind() == FeatureKind::Chamfer) {
            params["style"] = e.style == geometry::ChamferStyle::TwoDistances ? "two" : e.style == geometry::ChamferStyle::DistanceAngle ? "angle" : "equal";
            params["size2"] = e.size2;
            params["angle"] = e.angle;
            params["flip"] = e.flip;
        }
        break;
    }
    case FeatureKind::Mirror: {
        const auto& m = static_cast<const MirrorFeature&>(f);
        row.type = "mirror";
        params = {{"body", m.body}, {"plane", toJson(m.plane)}, {"operation", nameOf(kOperations, m.operation)}};
        if (!m.alsoBodies.empty()) {
            params["alsoBodies"] = m.alsoBodies;
        }
        break;
    }
    case FeatureKind::Split: {
        const auto& sp = static_cast<const SplitFeature&>(f);
        row.type = "split";
        params = {{"body", sp.body}, {"plane", toJson(sp.plane)}, {"offset", sp.offset}};
        break;
    }
    case FeatureKind::ConstructionPlane: {
        const auto& cp = static_cast<const ConstructionPlaneFeature&>(f);
        row.type = "plane";
        params = {{"base", toJson(cp.base)}, {"offset", cp.offset}, {"angle", cp.angle}, {"angleAxis", cp.angleAxis}};
        break;
    }
    case FeatureKind::Joint: {
        const auto& j = static_cast<const JointFeature&>(f);
        row.type = "joint";
        params = {{"fixed", toJson(j.fixedFace)}, {"moving", toJson(j.movingFace)}, {"type", nameOf(kJointTypes, j.type)},
                  {"offset", j.offset}, {"angle", j.angle}, {"slide", j.slide}, {"flip", j.flip}};
        break;
    }
    case FeatureKind::Move: {
        const auto& m = static_cast<const MoveFeature&>(f);
        row.type = "move";
        params = {{"body", m.body}, {"translation", {m.translation.x, m.translation.y, m.translation.z}},
                  {"axis", m.axis}, {"axisRef", m.axisRef}, {"aroundOrigin", m.aroundOrigin},
                  {"angle", m.angle}, {"copy", m.copy}, {"rotation", {m.rotation.x, m.rotation.y, m.rotation.z}}, {"alsoMove", m.alsoMove}};
        break;
    }
    case FeatureKind::Combine: {
        const auto& c = static_cast<const CombineFeature&>(f);
        row.type = "combine";
        params = {{"target", c.target}, {"tools", c.tools}, {"operation", nameOf(kOperations, c.operation)},
                  {"keepTools", c.keepTools}};
        break;
    }
    case FeatureKind::Loft: {
        const auto& l = static_cast<const LoftFeature&>(f);
        row.type = "loft";
        params = {{"profiles", toJson(l.profiles)}, {"ruled", l.ruled}, {"operation", nameOf(kOperations, l.operation)}};
        break;
    }
    case FeatureKind::Sweep: {
        const auto& s = static_cast<const SweepFeature&>(f);
        row.type = "sweep";
        Json path = Json::array();
        for (const sk::EntityId id : s.path) {
            path.push_back(id);
        }
        params = {{"profiles", toJson(s.profiles)}, {"pathSketch", s.pathSketch}, {"path", path},
                  {"followPath", s.spec.followPath}, {"endScale", s.spec.endScale}, {"operation", nameOf(kOperations, s.operation)}};
        break;
    }
    case FeatureKind::Box: {
        const auto& b = static_cast<const BoxFeature&>(f);
        row.type = "box";
        params = {{"base", toJson(b.base)}, {"x", b.x}, {"y", b.y}, {"z", b.z}, {"width", b.width},
                  {"depth", b.depth}, {"height", b.height}, {"cornerRadius", b.cornerRadius}, {"centered", b.centered},
                  {"symmetric", b.symmetric}, {"operation", nameOf(kOperations, b.operation)}};
        break;
    }
    case FeatureKind::Cylinder: {
        const auto& c = static_cast<const CylinderFeature&>(f);
        row.type = "cylinder";
        params = {{"base", toJson(c.base)}, {"x", c.x}, {"y", c.y}, {"z", c.z}, {"diameter", c.diameter},
                  {"height", c.height}, {"symmetric", c.symmetric},
                  {"operation", nameOf(kOperations, c.operation)}};
        break;
    }
    case FeatureKind::Thread: {
        const auto& t = static_cast<const ThreadFeature&>(f);
        row.type = "thread";
        params = {{"base", toJson(t.base)}, {"x", t.x}, {"y", t.y}, {"clearance", t.clearance},
                  {"diameter", t.spec.diameter}, {"pitch", t.spec.pitch},
                  {"length", t.spec.length}, {"leftHanded", t.spec.leftHanded},
                  {"operation", nameOf(kOperations, t.operation)}};
        break;
    }
    case FeatureKind::Gear: {
        const auto& g = static_cast<const GearFeature&>(f);
        row.type = "gear";
        params = {{"base", toJson(g.base)}, {"x", g.x}, {"y", g.y}, {"rotation", g.rotation}, {"module", g.spec.module},
                  {"teeth", g.spec.teeth},
                  {"pressureAngle", g.spec.pressureAngle}, {"thickness", g.spec.thickness}, {"bore", g.spec.bore},
                  {"helixAngle", g.spec.helixAngle}, {"operation", nameOf(kOperations, g.operation)},
                  {"gearType", g.spec.type == geometry::GearType::Internal ? "internal"
                               : g.spec.type == geometry::GearType::Rack   ? "rack"
                               : g.spec.type == geometry::GearType::Worm   ? "worm"
                               : g.spec.type == geometry::GearType::Bevel  ? "bevel"
                                                                           : "external"},
                  {"rim", g.spec.rim}, {"length", g.spec.length},
                  {"backlash", g.spec.backlash}, {"baseChamfer", g.spec.baseChamfer},
                  {"herringbone", g.spec.herringbone}, {"profileShift", g.spec.profileShift}, {"wormDiameter", g.spec.wormDiameter},
                  {"leftHanded", g.spec.leftHanded}, {"mateTeeth", g.spec.mateTeeth},
                  {"herringboneGroove", g.spec.herringboneGroove}};
        break;
    }
    case FeatureKind::ConstructionAxis: {
        const auto& ca = static_cast<const ConstructionAxisFeature&>(f);
        row.type = "axis";
        params = {{"first", toJson(ca.first)}, {"useSecond", ca.useSecond}, {"second", toJson(ca.second)}};
        break;
    }
    case FeatureKind::Pattern: {
        const auto& pt = static_cast<const PatternFeature&>(f);
        row.type = "pattern";
        params = {{"body", pt.body}, {"layout", nameOf(kPatternLayouts, pt.layout)}, {"axis", pt.axis},
                  {"axisRef", pt.axisRef}, {"count", pt.count}, {"spacing", pt.spacing}, {"angle", pt.angle},
                  {"operation", nameOf(kOperations, pt.operation)}};
        break;
    }
    case FeatureKind::Hole: {
        const auto& h = static_cast<const HoleFeature&>(f);
        row.type = "hole";
        params = {{"sketch", h.sketch}, {"kind", nameOf(kHoleKinds, h.spec.kind)}, {"diameter", h.spec.diameter},
                  {"depth", h.spec.depth}, {"throughAll", h.spec.throughAll}, {"headDiameter", h.spec.headDiameter},
                  {"headDepth", h.spec.headDepth}, {"headAngle", h.spec.headAngle}, {"flip", h.flip}};
        break;
    }
    case FeatureKind::Shell: {
        const auto& s = static_cast<const ShellFeature&>(f);
        row.type = "shell";
        params = {{"faces", toJson(s.faces)}, {"thickness", s.thickness}, {"outward", s.outward}, {"closed", s.closed}};
        break;
    }
    case FeatureKind::Import: {
        const auto& i = static_cast<const ImportFeature&>(f);
        row.type = "import";
        params = {{"source", i.source}, {"notes", i.notes}, {"blob", blobKey(f.id())}};
        break;
    }
    }
    if (!f.expressions.empty()) {
        params["expressions"] = f.expressions;
    }
    if (f.origin.lead != 0) {
        params["origin"] = {{"generator", f.origin.generator}, {"suffix", f.origin.suffix},
                            {"values", f.origin.values}, {"lead", f.origin.lead}};
    }
    row.params = params.dump();
    return row;
}

std::unique_ptr<Feature> featureOf(const FeatureRow& row, const NufFile& file)
{
    if (row.id <= 0) {
        throw NufError("Operación con id no válido");
    }
    const Json p = Json::parse(row.params);
    std::unique_ptr<Feature> feature;
    if (row.type == "sketch") {
        feature = std::make_unique<SketchFeature>(sketchOf(p.at("sketch")), attachmentOf(p.at("attachment")));
    } else if (row.type == "extrude") {
        auto e = std::make_unique<ExtrudeFeature>();
        e->profiles = profilesOf(p.at("profiles"));
        e->face = elementRef(p.at("face"));
        e->spec.direction = parseEnum(kDirections, text(p, "direction"), "dirección");
        e->spec.side1 = sideOf(p.at("side1"));
        e->spec.side2 = sideOf(p.at("side2"));
        e->spec.startOffset = number(p, "startOffset");
        e->operation = parseEnum(kOperations, text(p, "operation"), "operación");
        feature = std::move(e);
    } else if (row.type == "revolve") {
        auto r = std::make_unique<RevolveFeature>();
        r->profiles = profilesOf(p.at("profiles"));
        r->axis = axisOf(p.at("axis"));
        r->spec.extent = parseEnum(kRevolveExtents, text(p, "extent"), "extensión de la revolución");
        r->spec.direction = parseEnum(kDirections, text(p, "direction"), "dirección");
        r->spec.angle1 = number(p, "angle1");
        r->spec.angle2 = number(p, "angle2");
        r->operation = parseEnum(kOperations, text(p, "operation"), "operación");
        feature = std::move(r);
    } else if (row.type == "fillet" || row.type == "chamfer") {
        std::unique_ptr<EdgeFeature> e;
        if (row.type == "fillet") {
            e = std::make_unique<FilletFeature>();
        } else {
            e = std::make_unique<ChamferFeature>();
        }
        for (const Json& edge : p.at("edges")) {
            e->edges.push_back(elementRef(edge));
        }
        e->size = number(p, "size");
        e->variable = p.value("variable", false);
        // Los archivos anteriores solo tienen chaflán con una distancia.
        {
            const std::string style = p.value("style", std::string("equal"));
            e->style = style == "two" ? geometry::ChamferStyle::TwoDistances
                       : style == "angle" ? geometry::ChamferStyle::DistanceAngle
                                          : geometry::ChamferStyle::Equal;
            e->size2 = p.value("size2", e->size);
            e->angle = p.value("angle", 0.7853981633974483);
            e->flip = p.value("flip", false);
        }
        feature = std::move(e);
    } else if (row.type == "split") {
        auto sp = std::make_unique<SplitFeature>();
        sp->body = text(p, "body");
        sp->plane = attachmentOf(p.at("plane"));
        sp->offset = number(p, "offset");
        feature = std::move(sp);
    } else if (row.type == "mirror") {
        auto m = std::make_unique<MirrorFeature>();
        m->body = text(p, "body");
        m->plane = attachmentOf(p.at("plane"));
        m->operation = parseEnum(kOperations, text(p, "operation"), "operación");
        if (p.contains("alsoBodies")) {
            for (const Json& other : p.at("alsoBodies")) {
                m->alsoBodies.push_back(other.get<std::string>());
            }
        }
        feature = std::move(m);
    } else if (row.type == "plane") {
        auto cp = std::make_unique<ConstructionPlaneFeature>();
        cp->base = attachmentOf(p.at("base"));
        cp->offset = number(p, "offset");
        cp->angle = number(p, "angle");
        cp->angleAxis = p.at("angleAxis").get<int>();
        feature = std::move(cp);
    } else if (row.type == "joint") {
        auto j = std::make_unique<JointFeature>();
        j->fixedFace = elementRef(p.at("fixed"));
        j->movingFace = elementRef(p.at("moving"));
        j->type = parseEnum(kJointTypes, text(p, "type"), "tipo de unión");
        j->offset = number(p, "offset");
        j->angle = number(p, "angle");
        j->slide = number(p, "slide");
        j->flip = p.at("flip").get<bool>();
        feature = std::move(j);
    } else if (row.type == "move") {
        auto m = std::make_unique<MoveFeature>();
        m->body = text(p, "body");
        const Json& t = p.at("translation");
        m->translation = {t.at(0).get<double>(), t.at(1).get<double>(), t.at(2).get<double>()};
        m->axis = p.at("axis").get<int>();
        m->axisRef = unsignedValue(p, "axisRef");
        m->aroundOrigin = p.at("aroundOrigin").get<bool>();
        m->angle = number(p, "angle");
        m->copy = p.at("copy").get<bool>();
        if (p.contains("rotation")) {  // los diseños anteriores no lo traen: sin giro
            const Json& r = p.at("rotation");
            m->rotation = {r.at(0).get<double>(), r.at(1).get<double>(), r.at(2).get<double>()};
        }
        if (p.contains("alsoMove")) {  // los anteriores mueven un solo cuerpo
            for (const Json& key : p.at("alsoMove")) {
                m->alsoMove.push_back(key.get<std::string>());
            }
        }
        feature = std::move(m);
    } else if (row.type == "combine") {
        auto c = std::make_unique<CombineFeature>();
        c->target = text(p, "target");
        for (const Json& key : p.at("tools")) {
            c->tools.push_back(key.get<std::string>());
        }
        c->operation = parseEnum(kOperations, text(p, "operation"), "operación");
        c->keepTools = p.at("keepTools").get<bool>();
        feature = std::move(c);
    } else if (row.type == "loft") {
        auto l = std::make_unique<LoftFeature>();
        l->profiles = profilesOf(p.at("profiles"));
        l->ruled = p.at("ruled").get<bool>();
        l->operation = parseEnum(kOperations, text(p, "operation"), "operación");
        feature = std::move(l);
    } else if (row.type == "sweep") {
        auto s = std::make_unique<SweepFeature>();
        s->profiles = profilesOf(p.at("profiles"));
        s->pathSketch = unsignedValue(p, "pathSketch");
        for (const Json& id : p.at("path")) {
            s->path.push_back(static_cast<sk::EntityId>(id.get<std::uint64_t>()));
        }
        s->spec.followPath = p.at("followPath").get<bool>();
        s->spec.endScale = p.value("endScale", 1.0);
        s->operation = parseEnum(kOperations, text(p, "operation"), "operación");
        feature = std::move(s);
    } else if (row.type == "box") {
        auto b = std::make_unique<BoxFeature>();
        b->base = attachmentOf(p.at("base"));
        b->x = number(p, "x");
        b->y = number(p, "y");
        b->z = number(p, "z");
        b->width = number(p, "width");
        b->depth = number(p, "depth");
        b->height = number(p, "height");
        b->cornerRadius = p.value("cornerRadius", 0.0);
        b->centered = p.at("centered").get<bool>();
        b->symmetric = p.at("symmetric").get<bool>();
        b->operation = parseEnum(kOperations, text(p, "operation"), "operación");
        feature = std::move(b);
    } else if (row.type == "cylinder") {
        auto c = std::make_unique<CylinderFeature>();
        c->base = attachmentOf(p.at("base"));
        c->x = number(p, "x");
        c->y = number(p, "y");
        c->z = number(p, "z");
        c->diameter = number(p, "diameter");
        c->height = number(p, "height");
        c->symmetric = p.at("symmetric").get<bool>();
        c->operation = parseEnum(kOperations, text(p, "operation"), "operación");
        feature = std::move(c);
    } else if (row.type == "thread") {
        auto t = std::make_unique<ThreadFeature>();
        t->base = attachmentOf(p.at("base"));
        t->x = p.value("x", 0.0);  // los archivos anteriores no lo traen
        t->y = p.value("y", 0.0);
        t->clearance = p.value("clearance", 0.0);
        t->spec.diameter = number(p, "diameter");
        t->spec.pitch = number(p, "pitch");
        t->spec.length = number(p, "length");
        t->spec.leftHanded = p.at("leftHanded").get<bool>();
        t->operation = parseEnum(kOperations, text(p, "operation"), "operación");
        feature = std::move(t);
    } else if (row.type == "gear") {
        auto g = std::make_unique<GearFeature>();
        g->base = attachmentOf(p.at("base"));
        g->x = p.value("x", 0.0);
        g->y = p.value("y", 0.0);
        g->rotation = p.value("rotation", 0.0);
        g->spec.module = number(p, "module");
        g->spec.teeth = p.at("teeth").get<int>();
        g->spec.pressureAngle = number(p, "pressureAngle");
        g->spec.thickness = number(p, "thickness");
        g->spec.bore = number(p, "bore");
        g->spec.helixAngle = number(p, "helixAngle");
        {
            // Los archivos anteriores no traen tipo ni aro: rueda exterior.
            const std::string type = p.value("gearType", std::string("external"));
            g->spec.type = type == "internal" ? geometry::GearType::Internal
                           : type == "rack"   ? geometry::GearType::Rack
                           : type == "worm"   ? geometry::GearType::Worm
                           : type == "bevel"  ? geometry::GearType::Bevel
                                              : geometry::GearType::External;
            g->spec.rim = p.value("rim", 5.0);
            g->spec.length = p.value("length", 0.0);  // los anteriores: barra de dientes completos
            g->spec.backlash = p.value("backlash", 0.0);  // los anteriores: sin juego ni chaflán
            g->spec.baseChamfer = p.value("baseChamfer", 0.0);
            g->spec.wormDiameter = p.value("wormDiameter", 20.0);
            g->spec.leftHanded = p.value("leftHanded", false);
            g->spec.mateTeeth = p.value("mateTeeth", 20);
            g->spec.herringboneGroove = p.value("herringboneGroove", 0.0);
            g->spec.profileShift = p.value("profileShift", 0.0);  // los anteriores: sin corrección
            g->spec.herringbone = p.value("herringbone", false);  // los anteriores: sin espina de pescado
        }
        g->operation = parseEnum(kOperations, text(p, "operation"), "operación");
        feature = std::move(g);
    } else if (row.type == "axis") {
        auto ca = std::make_unique<ConstructionAxisFeature>();
        ca->first = attachmentOf(p.at("first"));
        ca->useSecond = p.at("useSecond").get<bool>();
        ca->second = attachmentOf(p.at("second"));
        feature = std::move(ca);
    } else if (row.type == "pattern") {
        auto pt = std::make_unique<PatternFeature>();
        pt->body = text(p, "body");
        pt->layout = parseEnum(kPatternLayouts, text(p, "layout"), "tipo de patrón");
        pt->axis = p.at("axis").get<int>();
        if (p.contains("axisRef")) {
            pt->axisRef = unsignedValue(p, "axisRef");
        }
        pt->count = p.at("count").get<int>();
        pt->spacing = number(p, "spacing");
        pt->angle = number(p, "angle");
        pt->operation = parseEnum(kOperations, text(p, "operation"), "operación");
        feature = std::move(pt);
    } else if (row.type == "hole") {
        auto h = std::make_unique<HoleFeature>();
        h->sketch = unsignedValue(p, "sketch");
        h->spec.kind = parseEnum(kHoleKinds, text(p, "kind"), "tipo de agujero");
        h->spec.diameter = number(p, "diameter");
        h->spec.depth = number(p, "depth");
        h->spec.throughAll = p.at("throughAll").get<bool>();
        h->spec.headDiameter = number(p, "headDiameter");
        h->spec.headDepth = number(p, "headDepth");
        h->spec.headAngle = number(p, "headAngle");
        h->flip = p.at("flip").get<bool>();
        feature = std::move(h);
    } else if (row.type == "shell") {
        auto s = std::make_unique<ShellFeature>();
        for (const Json& face : p.at("faces")) {
            s->faces.push_back(elementRef(face));
        }
        s->thickness = number(p, "thickness");
        s->outward = p.at("outward").get<bool>();
        s->closed = p.value("closed", false);  // los anteriores: vaciado con caras abiertas
        feature = std::move(s);
    } else if (row.type == "import") {
        auto i = std::make_unique<ImportFeature>();
        i->source = text(p, "source");
        for (const Json& note : p.at("notes")) {
            i->notes.push_back(note.get<std::string>());
        }
        const auto data = file.readBlob(blobKey(static_cast<FeatureId>(row.id)));
        if (!data || data->empty()) {
            throw NufError("Falta el sólido importado de «" + row.name + "» en el archivo");
        }
        i->brep = std::make_shared<const std::string>(*data);
        feature = std::move(i);
    } else {
        throw NufError("Tipo de operación desconocido: " + row.type + " (¿archivo de una versión más reciente?)");
    }
    feature->setId(static_cast<FeatureId>(row.id));
    feature->setName(row.name);
    feature->setSuppressed(row.suppressed);
    if (p.contains("expressions")) {
        for (const auto& [key, expression] : p.at("expressions").items()) {
            feature->expressions[key] = expression.get<std::string>();
        }
    }
    if (p.contains("origin")) {
        const auto& o = p.at("origin");
        feature->origin = {o.value("generator", std::string()), o.value("suffix", std::string()),
                           o.value("values", std::string()), o.value("lead", FeatureId(0))};
    }
    return feature;
}

// ---------------------------------------------------------------------------
// Vista y metadatos
// ---------------------------------------------------------------------------

Json toJson(const ViewState& v)
{
    Json sketches = Json::object();
    for (const auto& [id, visible] : v.sketchVisibility) {
        sketches[std::to_string(id)] = visible;
    }
    return {{"hiddenBodies", v.hiddenBodies}, {"sketches", sketches}, {"origin", v.originVisible},
            {"ground", v.groundVisible}, {"displayUnit", v.displayUnit}};
}

ViewState viewOf(const Json& j)
{
    ViewState v;
    for (const Json& key : j.at("hiddenBodies")) {
        v.hiddenBodies.insert(key.get<std::string>());
    }
    for (const auto& [key, visible] : j.at("sketches").items()) {
        v.sketchVisibility[std::stoull(key)] = visible.get<bool>();
    }
    v.originVisible = j.at("origin").get<bool>();
    v.groundVisible = j.at("ground").get<bool>();
    v.displayUnit = j.value("displayUnit", std::string());
    return v;
}

std::string metaOrThrow(const NufFile& file, const char* key)
{
    const auto value = file.meta(key);
    if (!value) {
        throw NufError(std::string("Falta «") + key + "» en el archivo");
    }
    return *value;
}

// Con OneDrive o un antivirus mirando, el reemplazo puede fallar un instante.
void replaceFile(const std::filesystem::path& from, const std::filesystem::path& to)
{
    std::error_code error;
    for (int attempt = 0; attempt < 5; ++attempt) {
        std::filesystem::rename(from, to, error);
        if (!error) {
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(150 * (attempt + 1)));
    }
    throw NufError("No se pudo reemplazar el archivo: " + error.message());
}

} // namespace

struct SavePlan::Impl {
    std::string name;
    std::string marker;
    std::string nextId;
    std::string view;
    std::optional<std::string> bodies;
    std::vector<FeatureRow> rows;
    std::vector<std::pair<std::string, std::string>> parameters;
    std::vector<std::pair<std::string, std::shared_ptr<const std::string>>> imports;  // clave del blob y su BREP
    std::string thumbnail;
};

SavePlan prepareSave(const Document& document, const ViewState& view, const std::string& thumbnailPng)
{
    auto plan = std::make_shared<SavePlan::Impl>();
    plan->name = document.name();
    plan->marker = std::to_string(document.marker());
    plan->nextId = std::to_string(document.nextId());
    plan->view = toJson(view).dump();
    for (std::size_t i = 0; i < document.featureCount(); ++i) {
        plan->rows.push_back(rowOf(document.feature(i)));
    }
    for (const Parameter& p : document.parameters()) {
        plan->parameters.emplace_back(p.name, p.expression);
    }
    if (!document.bodyInfo().empty()) {
        Json bodies = Json::object();
        for (const auto& [key, info] : document.bodyInfo()) {
            bodies[key] = {{"name", info.name}, {"component", info.component}};
        }
        plan->bodies = bodies.dump();
    }
    for (std::size_t i = 0; i < document.featureCount(); ++i) {
        if (document.feature(i).kind() == FeatureKind::Import) {
            const auto& imported = static_cast<const ImportFeature&>(document.feature(i));
            if (imported.brep) {
                plan->imports.emplace_back(blobKey(imported.id()), imported.brep);
            }
        }
    }
    plan->thumbnail = thumbnailPng;
    return SavePlan{std::move(plan)};
}

void writeSave(const std::filesystem::path& path, const SavePlan& savePlan)
{
    if (!savePlan.impl) {
        throw NufError("No hay nada que guardar");
    }
    const SavePlan::Impl& plan = *savePlan.impl;
    std::filesystem::path temp = path;
    temp += ".guardando";
    std::error_code ignored;
    std::filesystem::remove(temp, ignored);

    try {
        {
            NufFile file = NufFile::create(temp);
            file.begin();
            try {
                file.setMeta("nombre", plan.name);
                file.setMeta("unidades", kUnits);
                file.setMeta("marcador", plan.marker);
                file.setMeta("siguiente_id", plan.nextId);
                file.setMeta("vista", plan.view);
                file.writeFeatures(plan.rows);
                file.writeParameters(plan.parameters);
                if (plan.bodies) {
                    file.setMeta("cuerpos", *plan.bodies);
                }
                for (const auto& [key, brep] : plan.imports) {
                    file.writeBlob(key, "import", *brep);
                }
                if (!plan.thumbnail.empty()) {
                    file.writeBlob("thumbnail", "thumbnail", plan.thumbnail);
                }
                file.commit();
            } catch (...) {
                file.rollback();
                throw;
            }
        }  // se cierra antes de moverlo (Windows no mueve un archivo abierto)
        replaceFile(temp, path);
    } catch (...) {
        std::filesystem::remove(temp, ignored);
        throw;
    }
}

void saveDocument(const std::filesystem::path& path, const Document& document, const ViewState& view,
                  const std::string& thumbnailPng)
{
    writeSave(path, prepareSave(document, view, thumbnailPng));
}

std::optional<std::string> readThumbnail(const std::filesystem::path& path) noexcept
{
    try {
        return NufFile::open(path).readBlob("thumbnail");
    } catch (...) {
        return std::nullopt;
    }
}

void loadDocument(const std::filesystem::path& path, Document& document, ViewState& view)
{
    std::vector<std::unique_ptr<Feature>> features;
    std::vector<Parameter> parameters;
    std::map<std::string, BodyInfo> bodyInfo;
    std::string name;
    std::size_t marker = 0;
    FeatureId nextId = 1;
    ViewState loadedView;
    try {
        const NufFile file = NufFile::open(path);
        const auto units = file.meta("unidades");
        if (units && *units != kUnits) {
            throw NufError("El archivo usa unidades que esta versión no admite: " + *units);
        }
        name = metaOrThrow(file, "nombre");
        marker = std::stoull(metaOrThrow(file, "marcador"));
        nextId = std::stoull(metaOrThrow(file, "siguiente_id"));
        loadedView = viewOf(Json::parse(metaOrThrow(file, "vista")));
        for (const FeatureRow& row : file.readFeatures()) {
            features.push_back(featureOf(row, file));
        }
        if (const auto bodies = file.meta("cuerpos")) {
            const Json parsed = Json::parse(*bodies);  // con nombre: items() de un temporal queda colgando
            for (const auto& [key, value] : parsed.items()) {
                bodyInfo[key] = {value.at("name").get<std::string>(), value.at("component").get<std::string>()};
            }
        }
        for (auto& [pname, expression] : file.readParameters()) {
            parameters.push_back({std::move(pname), std::move(expression)});
        }
        if (marker > features.size()) {
            throw NufError("El marcador está fuera del historial");
        }
    } catch (const NufError&) {
        throw;
    } catch (const std::exception& e) {
        throw NufError(std::string("El archivo está dañado: ") + e.what());
    }
    try {
        document.restore(std::move(name), std::move(features), marker, nextId, std::move(parameters));
        document.setBodyInfo(std::move(bodyInfo));
    } catch (const std::invalid_argument& e) {
        throw NufError(std::string("El archivo está dañado: ") + e.what());
    }
    view = std::move(loadedView);
}

std::string dumpDocument(const std::filesystem::path& path)
{
    try {
        const NufFile file = NufFile::open(path);
        Json meta = Json::object();
        for (const auto& [key, value] : file.allMeta()) {
            // «vista» ya es JSON: se muestra anidado.
            meta[key] = key == "vista" ? Json::parse(value) : Json(value);
        }
        Json features = Json::array();
        for (const FeatureRow& row : file.readFeatures()) {
            features.push_back({{"id", row.id}, {"type", row.type}, {"name", row.name},
                                {"suppressed", row.suppressed}, {"params", Json::parse(row.params)}});
        }
        return Json{{"meta", meta}, {"features", features}}.dump(2);
    } catch (const NufError&) {
        throw;
    } catch (const std::exception& e) {
        throw NufError(std::string("El archivo está dañado: ") + e.what());
    }
}

} // namespace nuf::io
