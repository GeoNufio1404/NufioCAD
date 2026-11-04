#include <nuf/document/Document.hpp>
#include <nuf/document/Features.hpp>
#include <nuf/io/DocumentFile.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <sqlite3.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

using namespace nuf;
using namespace nuf::document;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {

sketch::Sketch rectangleSketch(double x0, double y0, double x1, double y1)
{
    sketch::Sketch s;
    s.addLine({x0, y0}, {x1, y0});
    s.addLine({x1, y0}, {x1, y1});
    s.addLine({x1, y1}, {x0, y1});
    s.addLine({x0, y1}, {x0, y0});
    return s;
}

std::unique_ptr<SketchFeature> sketchOn(sketch::Sketch s, OriginPlane plane = OriginPlane::XY)
{
    SketchAttachment at;
    at.origin = plane;
    return std::make_unique<SketchFeature>(std::move(s), at);
}

std::unique_ptr<ExtrudeFeature> extrudeAll(const Document& doc, FeatureId sketchId, double distance, Operation op)
{
    auto f = std::make_unique<ExtrudeFeature>();
    const auto* sk = static_cast<const SketchFeature*>(doc.findFeature(sketchId));
    for (const sketch::Profile& p : sketch::findProfiles(sk->sketch())) {
        f->profiles.push_back(profileRef(sketchId, p));
    }
    f->spec.side1.distance = distance;
    f->operation = op;
    return f;
}

geometry::ElementRef topFace(const Document& doc, FeatureId id)
{
    const Body& body = doc.result().bodies.front();
    const std::string prefix = "f" + std::to_string(id) + ":fin:";
    for (int i = 0; i < body.shape.faceCount(); ++i) {
        if (body.shape.faceName(i).starts_with(prefix)) {
            return body.shape.faceRef(i);
        }
    }
    return {};
}

// Un diseño con de todo: sketch con restricciones y spline, extrusión,
// sketch sobre una cara con agujero cortado, sketch con lo proyectado
// vinculado, una revolución y una operación suprimida.
Document buildRichDocument()
{
    Document doc;
    doc.setName("Pieza de prueba ñ");

    sketch::Sketch base = rectangleSketch(0, 0, 40, 20);
    base.addConstraint(sketch::Constraint::length(4, 40.0));
    base.addConstraint(sketch::Constraint::horizontal(4));
    base.addConstraint(sketch::Constraint::distanceY(sketch::Ref::start(5), sketch::Ref::end(5), 20.0));
    base.addEllipse({{30, 10}, 3.0, 1.5, 0.3}, true);
    base.addBSpline({{{0, 30}, {5, 35}, {10, 30}}, {1.0, 2.0, 1.0}, 2}, true);
    const FeatureId sk = doc.addFeature(sketchOn(std::move(base)));
    const FeatureId ex = doc.addFeature(extrudeAll(doc, sk, 10, Operation::NewBody));

    sketch::Sketch hole;
    hole.addCircle({10, 10}, 3);
    SketchAttachment on;
    on.origin.reset();
    on.face = topFace(doc, ex);
    on.lastPlane = *doc.result().bodies.front().shape.facePlane(*doc.result().bodies.front().shape.findFace(on.face));
    const FeatureId sk2 = doc.addFeature(std::make_unique<SketchFeature>(std::move(hole), on));
    auto cut = extrudeAll(doc, sk2, -4, Operation::Cut);
    cut->spec.side1.taper = 0.1;
    cut->spec.startOffset = 0.5;
    doc.addFeature(std::move(cut));

    // Lo proyectado, vinculado a una arista de la pieza.
    const geometry::Shape& body = doc.result().bodies.front().shape;
    std::optional<int> edge;
    for (int i = 0; i < body.edgeCount(); ++i) {
        const auto line = body.edgeLine(i);
        if (line && body.edgeName(i).find(":fin:") != std::string::npos && std::abs(line->first.y) < 1e-9
            && std::abs(line->second.x) > 0.5) {
            edge = i;
        }
    }
    if (edge) {
        const geometry::ElementRef ref = body.edgeRef(*edge);
        sketch::Sketch projected;
        const sketch::EntityId linked = projected.addGeometry(body.projectEdge(*edge, sketch::Plane::xy()).front());
        projected.entity(linked)->locked = true;
        projected.entity(linked)->link = sketch::ModelLink{ref.name, ref.occurrence, 0};
        doc.addFeature(sketchOn(std::move(projected)));
    }

    sketch::Sketch profile = rectangleSketch(50, 0, 55, 4);
    const sketch::EntityId axis = profile.addLine({45, -10}, {45, 10}, true);
    const FeatureId sk3 = doc.addFeature(sketchOn(std::move(profile), OriginPlane::XZ));
    auto rev = std::make_unique<RevolveFeature>();
    for (const sketch::Profile& p : sketch::findProfiles(static_cast<const SketchFeature*>(doc.findFeature(sk3))->sketch())) {
        rev->profiles.push_back(profileRef(sk3, p));
    }
    rev->axis = {AxisRef::Kind::SketchLine, sk3, axis, {}};
    rev->spec.extent = geometry::RevolveExtent::Angle;
    rev->spec.direction = geometry::ExtrudeDirection::Symmetric;
    rev->spec.angle1 = 2.0;
    rev->operation = Operation::NewBody;
    const FeatureId revId = doc.addFeature(std::move(rev));
    doc.setSuppressed(revId, true);
    doc.setMarker(doc.featureCount() - 1);  // la última, por detrás del marcador
    return doc;
}

std::filesystem::path tempFile(const std::string& name)
{
    // Con acentos a propósito: las rutas deben ir en UTF-8 en Windows.
    const auto path = std::filesystem::temp_directory_path() / (std::u8string(u8"nuf_diseño_") + std::u8string(name.begin(), name.end()));
    std::filesystem::remove(path);
    return path;
}

struct Cleanup {
    std::filesystem::path path;
    ~Cleanup()
    {
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
    }
};

// Resultado comparable de un documento: operaciones, estados y todas las
// caras y aristas con su nombre.
std::string fingerprint(const Document& doc)
{
    std::string out = doc.name() + "|marker=" + std::to_string(doc.marker()) + "|next=" + std::to_string(doc.nextId()) + "\n";
    for (std::size_t i = 0; i < doc.featureCount(); ++i) {
        const Feature& f = doc.feature(i);
        out += std::to_string(f.id()) + ":" + f.name() + ":" + (f.suppressed() ? "S" : "-") + ":"
               + std::to_string(int(doc.status(f.id()).health)) + "\n";
    }
    for (const Body& body : doc.result().bodies) {
        out += body.key + "/" + body.name + " v=" + std::to_string(body.shape.volume()) + "\n";
        for (int i = 0; i < body.shape.faceCount(); ++i) {
            out += " F " + body.shape.faceName(i) + "\n";
        }
        for (int i = 0; i < body.shape.edgeCount(); ++i) {
            out += " E " + body.shape.edgeName(i) + "\n";
        }
    }
    for (const auto& [id, placed] : doc.result().sketches) {
        out += "sketch " + std::to_string(id) + " ent=" + std::to_string(placed.sketch->entities().size())
               + " con=" + std::to_string(placed.sketch->constraints().size()) + "\n";
    }
    return out;
}

void writeSql(const std::filesystem::path& path, const char* sql)
{
    sqlite3* db = nullptr;
    const std::u8string utf8 = path.u8string();
    REQUIRE(sqlite3_open(std::string(utf8.begin(), utf8.end()).c_str(), &db) == SQLITE_OK);
    char* error = nullptr;
    const int rc = sqlite3_exec(db, sql, nullptr, nullptr, &error);
    sqlite3_free(error);
    sqlite3_close(db);
    REQUIRE(rc == SQLITE_OK);
}

} // namespace

TEST_CASE("Guardar y abrir deja el mismo modelo, con los mismos nombres de caras y aristas", "[io][document]")
{
    const Document original = buildRichDocument();
    REQUIRE(original.featureCount() >= 6);
    const auto path = tempFile("ida_y_vuelta.nuf");
    Cleanup cleanup{path};

    io::ViewState view;
    view.hiddenBodies = {"f2"};
    view.sketchVisibility = {{1, true}, {3, false}};
    view.originVisible = true;
    view.groundVisible = false;
    io::saveDocument(path, original, view);

    Document reopened;
    io::ViewState reopenedView;
    io::loadDocument(path, reopened, reopenedView);

    REQUIRE(fingerprint(reopened) == fingerprint(original));
    REQUIRE(reopenedView == view);
    // El sketch conserva restricciones y ids.
    const auto* a = static_cast<const SketchFeature*>(original.findFeature(1));
    const auto* b = static_cast<const SketchFeature*>(reopened.findFeature(1));
    REQUIRE(b->sketch().entities().size() == a->sketch().entities().size());
    REQUIRE(b->sketch().constraints().size() == 3);
    REQUIRE(b->sketch().nextEntityId() == a->sketch().nextEntityId());
    // Guardado sin cambios: el JSON es idéntico (el formato es estable).
    const auto second = tempFile("ida_y_vuelta_2.nuf");
    Cleanup cleanup2{second};
    io::saveDocument(second, reopened, reopenedView);
    REQUIRE(io::dumpDocument(second) == io::dumpDocument(path));
}

TEST_CASE("Tras abrir se puede seguir editando: ids nuevos y referencias por nombre", "[io][document]")
{
    const Document original = buildRichDocument();
    const auto path = tempFile("seguir.nuf");
    Cleanup cleanup{path};
    io::saveDocument(path, original, {});

    Document doc;
    io::ViewState view;
    io::loadDocument(path, doc, view);

    // Un id nuevo no choca con los guardados.
    const FeatureId fresh = doc.reserveId();
    REQUIRE(fresh > 5);
    REQUIRE_FALSE(doc.findFeature(fresh));

    // Más alta la primera extrusión: el sketch de la cara superior la sigue.
    auto extrusion = std::unique_ptr<Feature>(doc.findFeature(2)->clone());
    static_cast<ExtrudeFeature&>(*extrusion).spec.side1.distance = 15;
    doc.replaceFeature(2, std::move(extrusion));
    REQUIRE(doc.status(3).health == Health::Ok);
    REQUIRE_THAT(doc.result().sketches.at(3).plane.origin.z, WithinAbs(15.0, 1e-9));
    REQUIRE(doc.status(4).health == Health::Ok);
}

TEST_CASE("Abrir recalcula: una operación que falla se abre marcada con error", "[io][document]")
{
    Document doc;
    const FeatureId sk = doc.addFeature(sketchOn(rectangleSketch(0, 0, 10, 10)));
    doc.addFeature(extrudeAll(doc, sk, 5, Operation::NewBody));
    const auto path = tempFile("error.nuf");
    Cleanup cleanup{path};
    io::saveDocument(path, doc, {});

    // Se quita el sketch del que depende la extrusión.
    writeSql(path, "DELETE FROM features WHERE type = 'sketch'; UPDATE meta SET value = '1' WHERE key = 'marcador';");

    Document reopened;
    io::ViewState view;
    io::loadDocument(path, reopened, view);
    REQUIRE(reopened.featureCount() == 1);
    REQUIRE(reopened.status(2).health == Health::Error);
    REQUIRE(reopened.result().bodies.empty());
}

TEST_CASE("Archivos dañados dan un error claro y no tocan el documento", "[io][document]")
{
    Document doc;
    const FeatureId sk = doc.addFeature(sketchOn(rectangleSketch(0, 0, 10, 10)));
    doc.addFeature(extrudeAll(doc, sk, 5, Operation::NewBody));
    const std::string before = fingerprint(doc);
    io::ViewState view;

    const auto good = tempFile("bueno.nuf");
    Cleanup cleanup{good};
    io::saveDocument(good, doc, {});

    const auto copyWith = [&](const char* sql) {
        const auto bad = tempFile("malo.nuf");
        std::filesystem::copy_file(good, bad, std::filesystem::copy_options::overwrite_existing);
        writeSql(bad, sql);
        return bad;
    };

    SECTION("no es un archivo .nuf")
    {
        const auto bad = tempFile("texto.nuf");
        Cleanup c{bad};
        std::ofstream(bad) << "esto no es una base de datos";
        REQUIRE_THROWS_AS(io::loadDocument(bad, doc, view), io::NufError);
    }
    SECTION("no existe")
    {
        REQUIRE_THROWS_AS(io::loadDocument(tempFile("nada.nuf"), doc, view), io::NufError);
    }
    SECTION("JSON roto")
    {
        const auto bad = copyWith("UPDATE features SET params = '{no es json' WHERE type = 'extrude';");
        Cleanup c{bad};
        REQUIRE_THROWS_AS(io::loadDocument(bad, doc, view), io::NufError);
    }
    SECTION("tipo de operación desconocido")
    {
        const auto bad = copyWith("UPDATE features SET type = 'fillet' WHERE type = 'extrude';");
        Cleanup c{bad};
        REQUIRE_THROWS_AS(io::loadDocument(bad, doc, view), io::NufError);
    }
    SECTION("valor de enumeración desconocido")
    {
        const auto bad = copyWith("UPDATE features SET params = replace(params, '\"newBody\"', '\"explode\"');");
        Cleanup c{bad};
        REQUIRE_THROWS_AS(io::loadDocument(bad, doc, view), io::NufError);
    }
    SECTION("falta un campo")
    {
        const auto bad = copyWith("UPDATE features SET params = '{}' WHERE type = 'sketch';");
        Cleanup c{bad};
        REQUIRE_THROWS_AS(io::loadDocument(bad, doc, view), io::NufError);
    }
    SECTION("el marcador no cabe")
    {
        const auto bad = copyWith("UPDATE meta SET value = '99' WHERE key = 'marcador';");
        Cleanup c{bad};
        REQUIRE_THROWS_AS(io::loadDocument(bad, doc, view), io::NufError);
    }
    SECTION("sketch incoherente: restricción con una entidad inexistente")
    {
        const auto bad = copyWith(
            "UPDATE features SET params = replace(params, '\"constraints\":[]', "
            "'\"constraints\":[{\"id\":1,\"type\":\"horizontal\",\"first\":{\"entity\":77,\"part\":\"whole\",\"index\":0},"
            "\"second\":{\"entity\":0,\"part\":\"whole\",\"index\":0},\"third\":{\"entity\":0,\"part\":\"whole\",\"index\":0},"
            "\"value\":0,\"driving\":true}]') WHERE type = 'sketch';");
        Cleanup c{bad};
        REQUIRE_THROWS_AS(io::loadDocument(bad, doc, view), io::NufError);
    }
    SECTION("creado por una versión más reciente")
    {
        const auto bad = copyWith("PRAGMA user_version = 99;");
        Cleanup c{bad};
        REQUIRE_THROWS_AS(io::loadDocument(bad, doc, view), io::NufError);
    }

    REQUIRE(fingerprint(doc) == before);
}

TEST_CASE("Guardar sobre un archivo existente lo reemplaza; si falla, no pierde el original", "[io][document]")
{
    Document doc;
    const FeatureId sk = doc.addFeature(sketchOn(rectangleSketch(0, 0, 10, 10)));
    doc.addFeature(extrudeAll(doc, sk, 5, Operation::NewBody));
    const auto path = tempFile("reemplazo.nuf");
    Cleanup cleanup{path};
    io::saveDocument(path, doc, {});

    doc.setName("Otro nombre");
    io::saveDocument(path, doc, {});
    Document reopened;
    io::ViewState view;
    io::loadDocument(path, reopened, view);
    REQUIRE(reopened.name() == "Otro nombre");

    // Sin carpeta de destino: error, y no queda ningún temporal suelto.
    const auto missing = std::filesystem::temp_directory_path() / "nuf_no_existe_carpeta" / "x.nuf";
    REQUIRE_THROWS_AS(io::saveDocument(missing, doc, {}), io::NufError);
    auto temp = missing;
    temp += ".guardando";
    REQUIRE_FALSE(std::filesystem::exists(temp));
    // El archivo anterior sigue entero.
    io::loadDocument(path, reopened, view);
    REQUIRE(reopened.name() == "Otro nombre");
    auto leftover = path;
    leftover += ".guardando";
    REQUIRE_FALSE(std::filesystem::exists(leftover));
}

// El formato no puede romperse sin querer: este archivo se escribió con la
// versión 1 del esquema y todas las versiones futuras deben seguir abriéndolo.
// Para regenerarlo a propósito (al subir el esquema, con su migración):
//   NUF_WRITE_REFERENCE=1 nuf_tests "[reference]"
TEST_CASE("Un archivo .nuf de la versión 1 sigue abriéndose", "[io][document][reference]")
{
    const std::filesystem::path reference = std::filesystem::path(NUF_TEST_DATA_DIR) / "v1.nuf";
    if (std::getenv("NUF_WRITE_REFERENCE") != nullptr) {
        std::filesystem::create_directories(reference.parent_path());
        io::ViewState view;
        view.hiddenBodies = {"f2"};
        view.originVisible = true;
        io::saveDocument(reference, buildRichDocument(), view);
        SUCCEED("Archivo de referencia regenerado");
        return;
    }
    REQUIRE(std::filesystem::exists(reference));
    Document doc;
    io::ViewState view;
    io::loadDocument(reference, doc, view);
    REQUIRE(doc.name() == "Pieza de prueba ñ");
    REQUIRE(doc.featureCount() == 7);
    REQUIRE(doc.marker() == 6);
    REQUIRE(view.hiddenBodies.contains("f2"));
    REQUIRE(view.originVisible);
    REQUIRE(doc.status(2).health == Health::Ok);
    REQUIRE(doc.status(7).health == Health::RolledBack);
    REQUIRE(doc.result().bodies.size() == 1);
    // Una caja 40×20×10 con un agujero de radio 3 y 4 mm de profundidad (con inclinación).
    REQUIRE(doc.result().bodies.front().shape.volume() < 8000.0);
    REQUIRE(doc.result().bodies.front().shape.volume() > 7800.0);
}

TEST_CASE("El volcado a JSON es legible", "[io][document]")
{
    Document doc;
    const FeatureId sk = doc.addFeature(sketchOn(rectangleSketch(0, 0, 10, 10)));
    doc.addFeature(extrudeAll(doc, sk, 5, Operation::NewBody));
    const auto path = tempFile("volcado.nuf");
    Cleanup cleanup{path};
    io::saveDocument(path, doc, {});
    const std::string dump = io::dumpDocument(path);
    REQUIRE(dump.find("\"type\": \"extrude\"") != std::string::npos);
    REQUIRE(dump.find("\"operation\": \"newBody\"") != std::string::npos);
}
