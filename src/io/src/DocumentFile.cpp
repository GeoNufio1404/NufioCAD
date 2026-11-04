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

const Table<AxisRef::Kind, 5> kAxisKinds{{
    {AxisRef::Kind::OriginX, "originX"}, {AxisRef::Kind::OriginY, "originY"}, {AxisRef::Kind::OriginZ, "originZ"},
    {AxisRef::Kind::SketchLine, "sketchLine"}, {AxisRef::Kind::Edge, "edge"},
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
    if (!a.origin && a.face.empty()) {
        throw NufError("Un sketch no dice dónde va");
    }
    return a;
}

Json toJson(const AxisRef& a)
{
    return {{"kind", nameOf(kAxisKinds, a.kind)}, {"sketch", a.sketch}, {"entity", a.entity}, {"edge", toJson(a.edge)}};
}
AxisRef axisOf(const Json& j)
{
    AxisRef a;
    a.kind = parseEnum(kAxisKinds, text(j, "kind"), "eje");
    a.sketch = unsignedValue(j, "sketch");
    a.entity = static_cast<sk::EntityId>(unsignedValue(j, "entity"));
    a.edge = elementRef(j.at("edge"));
    return a;
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
    }
    row.params = params.dump();
    return row;
}

std::unique_ptr<Feature> featureOf(const FeatureRow& row)
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
    } else {
        throw NufError("Tipo de operación desconocido: " + row.type + " (¿archivo de una versión más reciente?)");
    }
    feature->setId(static_cast<FeatureId>(row.id));
    feature->setName(row.name);
    feature->setSuppressed(row.suppressed);
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
            {"ground", v.groundVisible}};
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

void saveDocument(const std::filesystem::path& path, const Document& document, const ViewState& view)
{
    std::filesystem::path temp = path;
    temp += ".guardando";
    std::error_code ignored;
    std::filesystem::remove(temp, ignored);

    try {
        {
            NufFile file = NufFile::create(temp);
            file.begin();
            try {
                file.setMeta("nombre", document.name());
                file.setMeta("unidades", kUnits);
                file.setMeta("marcador", std::to_string(document.marker()));
                file.setMeta("siguiente_id", std::to_string(document.nextId()));
                file.setMeta("vista", toJson(view).dump());
                std::vector<FeatureRow> rows;
                for (std::size_t i = 0; i < document.featureCount(); ++i) {
                    rows.push_back(rowOf(document.feature(i)));
                }
                file.writeFeatures(rows);
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

void loadDocument(const std::filesystem::path& path, Document& document, ViewState& view)
{
    std::vector<std::unique_ptr<Feature>> features;
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
            features.push_back(featureOf(row));
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
        document.restore(std::move(name), std::move(features), marker, nextId);
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
