#include <nuf/document/Document.hpp>
#include <nuf/document/Features.hpp>
#include <nuf/geometry/Modeling.hpp>
#include <nuf/io/DocumentFile.hpp>
#include <nuf/sketch/Profiles.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <filesystem>
#include <numbers>

using namespace nuf;
using namespace nuf::document;
using Catch::Matchers::WithinRel;

namespace {

constexpr double kPi = std::numbers::pi;

Document boxDocument(double depth = 5.0)
{
    Document doc;
    sketch::Sketch s;
    s.addLine({0, 0}, {20, 0});
    s.addLine({20, 0}, {20, 10});
    s.addLine({20, 10}, {0, 10});
    s.addLine({0, 10}, {0, 0});
    SketchAttachment at;
    at.origin = OriginPlane::XY;
    const FeatureId sk = doc.addFeature(std::make_unique<SketchFeature>(std::move(s), at));
    auto extrusion = std::make_unique<ExtrudeFeature>();
    extrusion->profiles.push_back(profileRef(sk, sketch::findProfiles(static_cast<const SketchFeature*>(doc.findFeature(sk))->sketch()).front()));
    extrusion->spec.side1.distance = depth;
    doc.addFeature(std::move(extrusion));
    return doc;
}

// Las cuatro aristas verticales del primer cuerpo.
std::vector<geometry::ElementRef> verticalEdges(const Document& doc)
{
    const geometry::Shape& body = doc.result().bodies.front().shape;
    std::vector<geometry::ElementRef> refs;
    for (int i = 0; i < body.edgeCount(); ++i) {
        const auto line = body.edgeLine(i);
        if (line && std::abs(line->second.z) > 0.9) {
            refs.push_back(body.edgeRef(i));
        }
    }
    return refs;
}

template <typename T>
FeatureId addEdgeFeature(Document& doc, double size, std::vector<geometry::ElementRef> edges)
{
    auto f = std::make_unique<T>();
    f->size = size;
    f->edges = std::move(edges);
    return doc.addFeature(std::move(f));
}

} // namespace

TEST_CASE("Redondear las aristas verticales quita el volumen que toca", "[edges]")
{
    Document doc = boxDocument();
    const auto edges = verticalEdges(doc);
    REQUIRE(edges.size() == 4);
    const FeatureId id = addEdgeFeature<FilletFeature>(doc, 2.0, edges);
    REQUIRE(doc.status(id).health == Health::Ok);
    const geometry::Shape& body = doc.result().bodies.front().shape;
    REQUIRE(body.isValid());
    REQUIRE_THAT(body.volume(), WithinRel(1000.0 - 4 * (4.0 - kPi * 4.0 / 4.0) * 5.0, 1e-6));
    // Cuatro caras redondeadas nuevas con nombre propio; las seis de antes siguen.
    int rounded = 0;
    for (int i = 0; i < body.faceCount(); ++i) {
        rounded += body.faceName(i).starts_with("f" + std::to_string(id) + ":redondeo:") ? 1 : 0;
    }
    REQUIRE(rounded == 4);
    REQUIRE(body.faceCount() == 10);
}

TEST_CASE("Achaflanar quita un prisma triangular por arista", "[edges]")
{
    Document doc = boxDocument();
    const FeatureId id = addEdgeFeature<ChamferFeature>(doc, 1.0, verticalEdges(doc));
    REQUIRE(doc.status(id).health == Health::Ok);
    const geometry::Shape& body = doc.result().bodies.front().shape;
    REQUIRE_THAT(body.volume(), WithinRel(1000.0 - 4 * 0.5 * 5.0, 1e-6));
    int chamfered = 0;
    for (int i = 0; i < body.faceCount(); ++i) {
        chamfered += body.faceName(i).starts_with("f" + std::to_string(id) + ":chaflan:") ? 1 : 0;
    }
    REQUIRE(chamfered == 4);
}

TEST_CASE("Un radio imposible da un error claro y el modelo queda como estaba", "[edges]")
{
    Document doc = boxDocument();
    const double before = doc.result().bodies.front().shape.volume();
    const FeatureId id = addEdgeFeature<FilletFeature>(doc, 50.0, verticalEdges(doc));
    REQUIRE(doc.status(id).health == Health::Error);
    REQUIRE_FALSE(doc.status(id).message.empty());
    REQUIRE_THAT(doc.result().bodies.front().shape.volume(), WithinRel(before, 1e-9));

    const FeatureId none = addEdgeFeature<ChamferFeature>(doc, 1.0, {});
    REQUIRE(doc.status(none).health == Health::Error);
    const FeatureId zero = addEdgeFeature<FilletFeature>(doc, 0.0, verticalEdges(doc));
    REQUIRE(doc.status(zero).health == Health::Error);
}

TEST_CASE("El redondeo sigue a la pieza cuando se edita lo anterior", "[edges]")
{
    Document doc = boxDocument(5.0);
    const FeatureId id = addEdgeFeature<FilletFeature>(doc, 1.0, verticalEdges(doc));
    REQUIRE(doc.status(id).health == Health::Ok);

    // Más alta la pieza: las aristas verticales se llaman igual y el redondeo las sigue.
    auto taller = std::unique_ptr<Feature>(doc.findFeature(2)->clone());
    static_cast<ExtrudeFeature&>(*taller).spec.side1.distance = 8.0;
    doc.replaceFeature(2, std::move(taller));
    REQUIRE(doc.status(id).health == Health::Ok);
    REQUIRE_THAT(doc.result().bodies.front().shape.volume(), WithinRel(200.0 * 8.0 - 4 * (1.0 - kPi / 4.0) * 8.0, 1e-6));
}

TEST_CASE("Si una arista elegida desaparece, se avisa y se hace con las demás", "[edges]")
{
    Document doc = boxDocument();
    auto edges = verticalEdges(doc);
    edges.push_back({"f99:no_existe", 0});
    const FeatureId id = addEdgeFeature<FilletFeature>(doc, 1.0, edges);
    REQUIRE(doc.status(id).health == Health::Warning);
    REQUIRE_THAT(doc.result().bodies.front().shape.volume(), WithinRel(1000.0 - 4 * (1.0 - kPi / 4.0) * 5.0, 1e-6));
}

TEST_CASE("Redondeo y chaflán se guardan y se abren con el mismo resultado", "[edges][io]")
{
    Document doc = boxDocument();
    const auto edges = verticalEdges(doc);
    const FeatureId fillet = addEdgeFeature<FilletFeature>(doc, 1.5, edges);
    // Chaflán sobre una arista de la tapa superior ya redondeada: se nombra con lo anterior.
    const geometry::Shape& body = doc.result().bodies.front().shape;
    std::vector<geometry::ElementRef> top;
    for (int i = 0; i < body.edgeCount(); ++i) {
        const auto line = body.edgeLine(i);
        if (line && std::abs(line->second.z) < 0.1 && body.edgeName(i).find(":fin:") != std::string::npos
            && body.edgeName(i).find("redondeo") == std::string::npos) {
            top.push_back(body.edgeRef(i));
            break;
        }
    }
    REQUIRE(top.size() == 1);
    const FeatureId chamfer = addEdgeFeature<ChamferFeature>(doc, 0.5, top);
    REQUIRE(doc.status(chamfer).health == Health::Ok);
    REQUIRE(doc.dependents(2).size() >= 1);

    const auto path = std::filesystem::temp_directory_path() / "nuf_bordes.nuf";
    std::filesystem::remove(path);
    io::saveDocument(path, doc, {});
    Document reopened;
    io::ViewState view;
    io::loadDocument(path, reopened, view);
    std::filesystem::remove(path);

    REQUIRE(reopened.featureCount() == doc.featureCount());
    REQUIRE(reopened.status(fillet).health == Health::Ok);
    REQUIRE(reopened.status(chamfer).health == Health::Ok);
    REQUIRE(reopened.findFeature(chamfer)->kind() == FeatureKind::Chamfer);
    REQUIRE_THAT(reopened.result().bodies.front().shape.volume(), WithinRel(doc.result().bodies.front().shape.volume(), 1e-9));
    REQUIRE(reopened.result().bodies.front().shape.faceCount() == doc.result().bodies.front().shape.faceCount());
}

// --- Vaciado ---------------------------------------------------------------------

namespace {

geometry::ElementRef topFaceOf(const Document& doc)
{
    const geometry::Shape& body = doc.result().bodies.front().shape;
    for (int i = 0; i < body.faceCount(); ++i) {
        if (const auto plane = body.facePlane(i); plane && plane->normal.z > 0.9) {
            return body.faceRef(i);
        }
    }
    return {};
}

FeatureId addShell(Document& doc, double thickness, bool outward, std::vector<geometry::ElementRef> faces)
{
    auto f = std::make_unique<ShellFeature>();
    f->thickness = thickness;
    f->outward = outward;
    f->faces = std::move(faces);
    return doc.addFeature(std::move(f));
}

int countNamed(const geometry::Shape& body, const std::string& prefix)
{
    int n = 0;
    for (int i = 0; i < body.faceCount(); ++i) {
        n += body.faceName(i).starts_with(prefix) ? 1 : 0;
    }
    return n;
}

} // namespace

TEST_CASE("Vaciar una caja por arriba deja una copa de paredes del espesor pedido", "[shell]")
{
    Document doc = boxDocument();
    const FeatureId id = addShell(doc, 1.0, false, {topFaceOf(doc)});
    REQUIRE(doc.status(id).health == Health::Ok);
    REQUIRE(doc.result().bodies.size() == 1);
    const geometry::Shape& body = doc.result().bodies.front().shape;
    REQUIRE(body.isValid());
    // 20×10×5 menos la cavidad de 18×8×4.
    REQUIRE_THAT(body.volume(), WithinRel(1000.0 - 18.0 * 8.0 * 4.0, 1e-6));
    const std::string tag = "f" + std::to_string(id);
    REQUIRE(countNamed(body, tag + ":interior:") == 5);
    // Las 5 caras de fuera siguen, las 5 interiores son nuevas y el borde de la abertura conserva su nombre.
    REQUIRE(body.faceCount() == 11);
    REQUIRE(countNamed(body, "f2:") == 6);
}

TEST_CASE("Vaciar hacia fuera y los casos que no se pueden", "[shell]")
{
    Document outward = boxDocument();
    const FeatureId o = addShell(outward, 1.0, true, {topFaceOf(outward)});
    REQUIRE(outward.status(o).health == Health::Ok);
    REQUIRE(outward.result().bodies.front().shape.isValid());
    // La cavidad es la caja original; las paredes quedan fuera de ella.
    REQUIRE(outward.result().bodies.front().shape.volume() > 400.0);
    REQUIRE(outward.result().bodies.front().shape.volume() < 1000.0);

    Document doc = boxDocument();
    const double before = doc.result().bodies.front().shape.volume();
    const FeatureId tooThick = addShell(doc, 6.0, false, {topFaceOf(doc)});
    REQUIRE(doc.status(tooThick).health == Health::Error);
    REQUIRE_THAT(doc.result().bodies.front().shape.volume(), WithinRel(before, 1e-9));
    REQUIRE(doc.status(addShell(doc, 1.0, false, {})).health == Health::Error);
    REQUIRE(doc.status(addShell(doc, 0.0, false, {topFaceOf(doc)})).health == Health::Error);
}

TEST_CASE("El vaciado sigue a la pieza, se guarda y se abre", "[shell][io]")
{
    Document doc = boxDocument(5.0);
    const FeatureId id = addShell(doc, 1.0, false, {topFaceOf(doc)});
    REQUIRE(doc.status(id).health == Health::Ok);

    auto taller = std::unique_ptr<Feature>(doc.findFeature(2)->clone());
    static_cast<ExtrudeFeature&>(*taller).spec.side1.distance = 8.0;
    doc.replaceFeature(2, std::move(taller));
    REQUIRE(doc.status(id).health == Health::Ok);
    REQUIRE_THAT(doc.result().bodies.front().shape.volume(), WithinRel(20.0 * 10.0 * 8.0 - 18.0 * 8.0 * 7.0, 1e-6));

    const auto path = std::filesystem::temp_directory_path() / "nuf_vaciado.nuf";
    std::filesystem::remove(path);
    io::saveDocument(path, doc, {});
    Document reopened;
    io::ViewState view;
    io::loadDocument(path, reopened, view);
    std::filesystem::remove(path);
    REQUIRE(reopened.findFeature(id)->kind() == FeatureKind::Shell);
    REQUIRE(reopened.status(id).health == Health::Ok);
    REQUIRE_THAT(reopened.result().bodies.front().shape.volume(), WithinRel(doc.result().bodies.front().shape.volume(), 1e-9));
    REQUIRE(reopened.result().bodies.front().shape.faceCount() == doc.result().bodies.front().shape.faceCount());
}
