#include <nuf/commands/AddRecipe.hpp>
#include <nuf/commands/ReplaceRecipe.hpp>
#include <nuf/document/Document.hpp>
#include <nuf/document/Generators.hpp>
#include <nuf/document/Expression.hpp>
#include <nuf/document/Features.hpp>
#include <nuf/geometry/Cancel.hpp>
#include <nuf/geometry/Modeling.hpp>
#include <nuf/geometry/DisplayMesh.hpp>
#include <nuf/geometry/Measure.hpp>
#include <nuf/geometry/Printability.hpp>
#include <nuf/io/DocumentFile.hpp>
#include <nuf/sketch/Profiles.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <chrono>
#include <cmath>
#include <deque>
#include <filesystem>
#include <functional>
#include <mutex>
#include <numbers>
#include <thread>

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

// --- Agujeros --------------------------------------------------------------------

namespace {

// La caja de 20×10×5 y un sketch en el plano XY (z = 0) con círculos de radio 2.
// Para que el agujero entre en el material se invierte («flip»): sube desde z = 0.
FeatureId addHoleSketch(Document& doc, std::vector<sketch::Vec2> centers)
{
    sketch::Sketch s;
    for (const sketch::Vec2& c : centers) {
        s.addCircle(c, 2.0);
    }
    SketchAttachment at;
    at.origin = OriginPlane::XY;
    return doc.addFeature(std::make_unique<SketchFeature>(std::move(s), at));
}

FeatureId addHole(Document& doc, FeatureId sketchId, const geometry::HoleSpec& spec)
{
    auto h = std::make_unique<HoleFeature>();
    h->sketch = sketchId;
    h->spec = spec;
    h->flip = true;
    return doc.addFeature(std::move(h));
}

constexpr double kBox = 20.0 * 10.0 * 5.0;

} // namespace

TEST_CASE("Un agujero simple resta un cilindro de la profundidad pedida", "[hole]")
{
    Document doc = boxDocument();
    const FeatureId sk = addHoleSketch(doc, {{10, 5}});
    geometry::HoleSpec spec;
    spec.diameter = 4;
    spec.depth = 3;
    const FeatureId id = addHole(doc, sk, spec);
    REQUIRE(doc.status(id).health == Health::Ok);
    const geometry::Shape& body = doc.result().bodies.front().shape;
    REQUIRE(body.isValid());
    REQUIRE_THAT(body.volume(), WithinRel(kBox - kPi * 4.0 * 3.0, 1e-6));
    REQUIRE(countNamed(body, "f" + std::to_string(id) + ":pared") >= 1);

    // Atravesando todo: la altura entera de la caja.
    spec.throughAll = true;
    Document through = boxDocument();
    const FeatureId skThrough = addHoleSketch(through, {{10, 5}});
    addHole(through, skThrough, spec);
    REQUIRE_THAT(through.result().bodies.front().shape.volume(), WithinRel(kBox - kPi * 4.0 * 5.0, 1e-6));

    // Dos círculos, dos agujeros.
    Document two = boxDocument();
    const FeatureId skTwo = addHoleSketch(two, {{5, 5}, {15, 5}});
    spec.throughAll = false;
    addHole(two, skTwo, spec);
    REQUIRE_THAT(two.result().bodies.front().shape.volume(), WithinRel(kBox - 2 * kPi * 4.0 * 3.0, 1e-6));
}

TEST_CASE("Abocardado y avellanado quitan más que un agujero simple", "[hole]")
{
    geometry::HoleSpec spec;
    spec.diameter = 4;
    spec.depth = 3;

    Document counter = boxDocument();
    const FeatureId skA = addHoleSketch(counter, {{10, 5}});
    spec.kind = geometry::HoleKind::Counterbore;
    spec.headDiameter = 8;
    spec.headDepth = 2;
    const FeatureId a = addHole(counter, skA, spec);
    REQUIRE(counter.status(a).health == Health::Ok);
    // Cilindro de la cabeza (r 4, 2 de hondo) y el resto del agujero (r 2, 1 más).
    REQUIRE_THAT(counter.result().bodies.front().shape.volume(),
                 WithinRel(kBox - kPi * 16.0 * 2.0 - kPi * 4.0 * 1.0, 1e-6));

    Document sink = boxDocument();
    const FeatureId skB = addHoleSketch(sink, {{10, 5}});
    spec.kind = geometry::HoleKind::Countersink;
    const FeatureId b = addHole(sink, skB, spec);
    REQUIRE(sink.status(b).health == Health::Ok);
    REQUIRE(sink.result().bodies.front().shape.isValid());
    REQUIRE(sink.result().bodies.front().shape.volume() < kBox - kPi * 4.0 * 3.0);
}

TEST_CASE("Los agujeros dan errores claros, se guardan y se abren", "[hole][io]")
{
    Document doc = boxDocument();
    geometry::HoleSpec spec;
    spec.diameter = 4;

    // Sin círculos ni puntos: no hay dónde.
    sketch::Sketch empty;
    SketchAttachment at;
    at.origin = OriginPlane::XY;
    const FeatureId none = doc.addFeature(std::make_unique<SketchFeature>(empty, at));
    REQUIRE(doc.status(addHole(doc, none, spec)).health == Health::Error);

    // Una cabeza más chica que el agujero no tiene sentido.
    const FeatureId sk = addHoleSketch(doc, {{10, 5}});
    spec.kind = geometry::HoleKind::Counterbore;
    spec.headDiameter = 3;
    REQUIRE(doc.status(addHole(doc, sk, spec)).health == Health::Error);

    // Uno bueno, y ida y vuelta por el archivo.
    spec.headDiameter = 8;
    spec.headDepth = 2;
    spec.depth = 4;
    const FeatureId good = addHole(doc, sk, spec);
    REQUIRE(doc.status(good).health == Health::Ok);
    const auto path = std::filesystem::temp_directory_path() / "nuf_agujero.nuf";
    std::filesystem::remove(path);
    io::saveDocument(path, doc, {});
    Document reopened;
    io::ViewState view;
    io::loadDocument(path, reopened, view);
    std::filesystem::remove(path);
    REQUIRE(reopened.findFeature(good)->kind() == FeatureKind::Hole);
    const auto& h = static_cast<const HoleFeature&>(*reopened.findFeature(good));
    REQUIRE(h.spec.kind == geometry::HoleKind::Counterbore);
    REQUIRE(h.flip);
    REQUIRE_THAT(reopened.result().bodies.front().shape.volume(), WithinRel(doc.result().bodies.front().shape.volume(), 1e-9));
}

// --- Espejo ----------------------------------------------------------------------

namespace {

FeatureId addMirror(Document& doc, OriginPlane plane, Operation operation, const std::string& body = "f2")
{
    auto m = std::make_unique<MirrorFeature>();
    m->body = body;
    m->plane.origin = plane;
    m->operation = operation;
    return doc.addFeature(std::move(m));
}

} // namespace

TEST_CASE("Reflejar un cuerpo que toca el plano lo une en una sola pieza simétrica", "[mirror]")
{
    Document doc = boxDocument();  // 20×10×5, de x = 0 a 20
    const FeatureId id = addMirror(doc, OriginPlane::YZ, Operation::Join);
    REQUIRE(doc.status(id).health == Health::Ok);
    REQUIRE(doc.result().bodies.size() == 1);
    const geometry::Shape& body = doc.result().bodies.front().shape;
    REQUIRE(body.isValid());
    REQUIRE_THAT(body.volume(), WithinRel(2.0 * kBox, 1e-6));
    // Una caja de 40×10×5 (x de -20 a 20): las caras coplanares se unen.
    REQUIRE(body.faceCount() == 6);
    REQUIRE_THAT(body.bounds().min.x, Catch::Matchers::WithinAbs(-20.0, 1e-6));
    REQUIRE_THAT(body.bounds().max.x, Catch::Matchers::WithinAbs(20.0, 1e-6));
}

TEST_CASE("Reflejar como cuerpo nuevo deja dos cuerpos, y una cara puede ser el plano", "[mirror]")
{
    Document copy = boxDocument();
    const FeatureId a = addMirror(copy, OriginPlane::YZ, Operation::NewBody);
    REQUIRE(copy.status(a).health == Health::Ok);
    REQUIRE(copy.result().bodies.size() == 2);
    REQUIRE_THAT(copy.result().bodies[1].shape.volume(), WithinRel(kBox, 1e-6));
    REQUIRE_THAT(copy.result().bodies[1].shape.bounds().min.x, Catch::Matchers::WithinAbs(-20.0, 1e-6));
    REQUIRE(copy.result().bodies[1].shape.faceName(0).find(":espejo:") != std::string::npos);

    // Reflejar sobre la cara derecha de la caja (x = 20).
    Document byFace = boxDocument();
    const geometry::Shape& body = byFace.result().bodies.front().shape;
    std::optional<int> right;
    for (int i = 0; i < body.faceCount(); ++i) {
        if (const auto plane = body.facePlane(i); plane && plane->normal.x > 0.9) {
            right = i;
        }
    }
    REQUIRE(right);
    auto m = std::make_unique<MirrorFeature>();
    m->body = "f2";
    m->plane.origin.reset();
    m->plane.face = body.faceRef(*right);
    m->plane.lastPlane = *body.facePlane(*right);
    const FeatureId b = byFace.addFeature(std::move(m));
    REQUIRE(byFace.status(b).health == Health::Ok);
    REQUIRE_THAT(byFace.result().bodies.front().shape.volume(), WithinRel(2.0 * kBox, 1e-6));
    REQUIRE_THAT(byFace.result().bodies.front().shape.bounds().max.x, Catch::Matchers::WithinAbs(40.0, 1e-6));
}

TEST_CASE("El espejo sigue a la pieza, da error sin cuerpo y se guarda", "[mirror][io]")
{
    Document doc = boxDocument(5.0);
    const FeatureId id = addMirror(doc, OriginPlane::YZ, Operation::Join);
    REQUIRE(doc.status(addMirror(doc, OriginPlane::XY, Operation::Join, "f99")).health == Health::Error);

    // Más alta la pieza: el espejo se rehace sobre ella.
    auto taller = std::unique_ptr<Feature>(doc.findFeature(2)->clone());
    static_cast<ExtrudeFeature&>(*taller).spec.side1.distance = 8.0;
    doc.replaceFeature(2, std::move(taller));
    REQUIRE(doc.status(id).health == Health::Ok);
    REQUIRE_THAT(doc.result().bodies.front().shape.volume(), WithinRel(2.0 * 20.0 * 10.0 * 8.0, 1e-6));

    const auto path = std::filesystem::temp_directory_path() / "nuf_espejo.nuf";
    std::filesystem::remove(path);
    io::saveDocument(path, doc, {});
    Document reopened;
    io::ViewState view;
    io::loadDocument(path, reopened, view);
    std::filesystem::remove(path);
    REQUIRE(reopened.findFeature(id)->kind() == FeatureKind::Mirror);
    REQUIRE(reopened.status(id).health == Health::Ok);
    REQUIRE_THAT(reopened.result().bodies.front().shape.volume(), WithinRel(doc.result().bodies.front().shape.volume(), 1e-9));
}

namespace {

FeatureId addPattern(Document& doc, PatternLayout layout, int axis, int count, double spacing, double angle,
                     Operation operation, const std::string& body = "f2")
{
    auto p = std::make_unique<PatternFeature>();
    p->body = body;
    p->layout = layout;
    p->axis = axis;
    p->count = count;
    p->spacing = spacing;
    p->angle = angle;
    p->operation = operation;
    return doc.addFeature(std::move(p));
}

} // namespace

TEST_CASE("Un patrón lineal repite el cuerpo, separado o fundido si se solapan", "[pattern]")
{
    // Separadas (cada 30 mm, la caja mide 20): 3 piezas en un solo cuerpo.
    Document apart = boxDocument();
    const FeatureId a = addPattern(apart, PatternLayout::Linear, 0, 3, 30.0, 0.0, Operation::Join);
    REQUIRE(apart.status(a).health == Health::Ok);
    REQUIRE(apart.result().bodies.size() == 1);
    REQUIRE_THAT(apart.result().bodies.front().shape.volume(), WithinRel(3.0 * kBox, 1e-6));
    REQUIRE_THAT(apart.result().bodies.front().shape.bounds().max.x, Catch::Matchers::WithinAbs(80.0, 1e-6));

    // Solapadas (cada 10 mm): se funden en una caja de 40×10×5.
    Document overlap = boxDocument();
    const FeatureId b = addPattern(overlap, PatternLayout::Linear, 0, 3, 10.0, 0.0, Operation::Join);
    REQUIRE(overlap.status(b).health == Health::Ok);
    REQUIRE_THAT(overlap.result().bodies.front().shape.volume(), WithinRel(2.0 * kBox, 1e-6));
    REQUIRE(overlap.result().bodies.front().shape.faceCount() == 6);

    // Cuerpo nuevo: el original queda intacto y las copias aparte; hacia atrás con distancia negativa.
    Document fresh = boxDocument();
    const FeatureId c = addPattern(fresh, PatternLayout::Linear, 1, 3, -30.0, 0.0, Operation::NewBody);
    REQUIRE(fresh.status(c).health == Health::Ok);
    REQUIRE(fresh.result().bodies.size() == 2);
    REQUIRE_THAT(fresh.result().bodies[0].shape.volume(), WithinRel(kBox, 1e-6));
    REQUIRE_THAT(fresh.result().bodies[1].shape.volume(), WithinRel(2.0 * kBox, 1e-6));
    REQUIRE_THAT(fresh.result().bodies[1].shape.bounds().min.y, Catch::Matchers::WithinAbs(-60.0, 1e-6));
    REQUIRE(fresh.result().bodies[1].shape.faceName(0).find(":patron") != std::string::npos);
}

TEST_CASE("Un patrón circular reparte las copias alrededor del eje", "[pattern]")
{
    // Cuatro cajas a 90° alrededor de Z: se tocan por las caras, sin solaparse.
    Document doc = boxDocument();
    const FeatureId id = addPattern(doc, PatternLayout::Circular, 2, 4, 0.0, 2.0 * kPi, Operation::Join);
    REQUIRE(doc.status(id).health == Health::Ok);
    REQUIRE(doc.result().bodies.size() == 1);
    const geometry::Shape& body = doc.result().bodies.front().shape;
    REQUIRE(body.isValid());
    REQUIRE_THAT(body.volume(), WithinRel(4.0 * kBox, 1e-6));
    REQUIRE_THAT(body.bounds().min.x, Catch::Matchers::WithinAbs(-20.0, 1e-6));
    REQUIRE_THAT(body.bounds().max.x, Catch::Matchers::WithinAbs(20.0, 1e-6));
    REQUIRE_THAT(body.bounds().min.y, Catch::Matchers::WithinAbs(-20.0, 1e-6));
    REQUIRE_THAT(body.bounds().max.y, Catch::Matchers::WithinAbs(20.0, 1e-6));

    // Medio giro con dos copias: la última queda a 180° (no a 360° / 2).
    Document half = boxDocument();
    addPattern(half, PatternLayout::Circular, 2, 2, 0.0, kPi, Operation::NewBody);
    REQUIRE(half.result().bodies.size() == 2);
    REQUIRE_THAT(half.result().bodies[1].shape.bounds().min.x, Catch::Matchers::WithinAbs(-20.0, 1e-6));
    REQUIRE_THAT(half.result().bodies[1].shape.bounds().max.y, Catch::Matchers::WithinAbs(0.0, 1e-6));
}

TEST_CASE("El patrón sigue a la pieza, da error si algo falla y se guarda", "[pattern][io]")
{
    Document doc = boxDocument(5.0);
    const FeatureId id = addPattern(doc, PatternLayout::Linear, 0, 3, 30.0, 0.0, Operation::Join);
    REQUIRE(doc.status(addPattern(doc, PatternLayout::Linear, 0, 3, 30.0, 0.0, Operation::Join, "f99")).health == Health::Error);
    REQUIRE(doc.status(addPattern(doc, PatternLayout::Linear, 0, 1, 30.0, 0.0, Operation::Join)).health == Health::Error);
    REQUIRE(doc.status(addPattern(doc, PatternLayout::Linear, 0, 3, 0.0, 0.0, Operation::Join)).health == Health::Error);

    // Más alta la pieza: el patrón se rehace sobre ella.
    auto taller = std::unique_ptr<Feature>(doc.findFeature(2)->clone());
    static_cast<ExtrudeFeature&>(*taller).spec.side1.distance = 8.0;
    doc.replaceFeature(2, std::move(taller));
    REQUIRE(doc.status(id).health == Health::Ok);
    REQUIRE_THAT(doc.result().bodies.front().shape.volume(), WithinRel(3.0 * 20.0 * 10.0 * 8.0, 1e-6));

    const auto path = std::filesystem::temp_directory_path() / "nuf_patron.nuf";
    std::filesystem::remove(path);
    io::saveDocument(path, doc, {});
    Document reopened;
    io::ViewState view;
    io::loadDocument(path, reopened, view);
    std::filesystem::remove(path);
    REQUIRE(reopened.findFeature(id)->kind() == FeatureKind::Pattern);
    REQUIRE(reopened.status(id).health == Health::Ok);
    REQUIRE_THAT(reopened.result().bodies.front().shape.volume(), WithinRel(doc.result().bodies.front().shape.volume(), 1e-9));
}

namespace {

FeatureId addPlane(Document& doc, double offset, double angle = 0.0, int angleAxis = 0, OriginPlane base = OriginPlane::XY)
{
    auto p = std::make_unique<ConstructionPlaneFeature>();
    p->base.origin = base;
    p->offset = offset;
    p->angle = angle;
    p->angleAxis = angleAxis;
    return doc.addFeature(std::move(p));
}

// Una caja de 20×10 sobre el plano de construcción dado, extruida «depth».
FeatureId boxOnPlane(Document& doc, FeatureId plane, double depth)
{
    sketch::Sketch s;
    s.addLine({0, 0}, {20, 0});
    s.addLine({20, 0}, {20, 10});
    s.addLine({20, 10}, {0, 10});
    s.addLine({0, 10}, {0, 0});
    SketchAttachment at;
    at.origin.reset();
    at.reference = plane;
    const FeatureId sk = doc.addFeature(std::make_unique<SketchFeature>(std::move(s), at));
    auto extrusion = std::make_unique<ExtrudeFeature>();
    extrusion->profiles.push_back(profileRef(sk, sketch::findProfiles(static_cast<const SketchFeature*>(doc.findFeature(sk))->sketch()).front()));
    extrusion->spec.side1.distance = depth;
    return doc.addFeature(std::move(extrusion));
}

} // namespace

TEST_CASE("Un plano de construcción desfasado sirve de plano de sketch", "[plane]")
{
    Document doc;
    const FeatureId plane = addPlane(doc, 25.0);
    REQUIRE(doc.status(plane).health == Health::Ok);
    REQUIRE_THAT(doc.result().planes.at(plane).origin.z, Catch::Matchers::WithinAbs(25.0, 1e-9));
    REQUIRE_THAT(doc.result().planes.at(plane).normal.z, Catch::Matchers::WithinAbs(1.0, 1e-9));

    const FeatureId extrusion = boxOnPlane(doc, plane, 5.0);
    REQUIRE(doc.status(extrusion).health == Health::Ok);
    REQUIRE(doc.result().bodies.size() == 1);
    const geometry::Box box = doc.result().bodies.front().shape.bounds();
    REQUIRE_THAT(box.min.z, Catch::Matchers::WithinAbs(25.0, 1e-6));
    REQUIRE_THAT(box.max.z, Catch::Matchers::WithinAbs(30.0, 1e-6));

    // Cambiar el desfase mueve la pieza que se apoya en el plano.
    auto moved = std::unique_ptr<Feature>(doc.findFeature(plane)->clone());
    static_cast<ConstructionPlaneFeature&>(*moved).offset = 40.0;
    doc.replaceFeature(plane, std::move(moved));
    REQUIRE_THAT(doc.result().bodies.front().shape.bounds().min.z, Catch::Matchers::WithinAbs(40.0, 1e-6));
}

TEST_CASE("Un plano de construcción puede girarse y partir de otro plano", "[plane]")
{
    Document doc;
    const FeatureId first = addPlane(doc, 0.0, kPi / 2.0, 0);  // XY girado 90° alrededor de X
    REQUIRE_THAT(doc.result().planes.at(first).normal.y, Catch::Matchers::WithinAbs(-1.0, 1e-9));
    REQUIRE_THAT(doc.result().planes.at(first).normal.z, Catch::Matchers::WithinAbs(0.0, 1e-9));

    auto second = std::make_unique<ConstructionPlaneFeature>();
    second->base.origin.reset();
    second->base.reference = first;
    second->offset = 15.0;
    const FeatureId b = doc.addFeature(std::move(second));
    REQUIRE(doc.status(b).health == Health::Ok);
    // 15 mm a lo largo de la normal del primero (hacia -Y).
    REQUIRE_THAT(doc.result().planes.at(b).origin.y, Catch::Matchers::WithinAbs(-15.0, 1e-9));
}

TEST_CASE("Se puede reflejar sobre un plano de construcción y se guarda en el archivo", "[plane][mirror][io]")
{
    Document doc = boxDocument();  // de x = 0 a 20
    const FeatureId plane = addPlane(doc, 30.0, 0.0, 0, OriginPlane::YZ);  // x = 30
    auto m = std::make_unique<MirrorFeature>();
    m->body = "f2";
    m->plane.origin.reset();
    m->plane.reference = plane;
    m->operation = Operation::NewBody;
    const FeatureId mirror = doc.addFeature(std::move(m));
    REQUIRE(doc.status(mirror).health == Health::Ok);
    REQUIRE(doc.result().bodies.size() == 2);
    REQUIRE_THAT(doc.result().bodies[1].shape.bounds().min.x, Catch::Matchers::WithinAbs(40.0, 1e-6));
    REQUIRE_THAT(doc.result().bodies[1].shape.bounds().max.x, Catch::Matchers::WithinAbs(60.0, 1e-6));

    const FeatureId onPlane = boxOnPlane(doc, plane, 5.0);
    REQUIRE(doc.status(onPlane).health == Health::Ok);

    const auto path = std::filesystem::temp_directory_path() / "nuf_plano.nuf";
    std::filesystem::remove(path);
    io::saveDocument(path, doc, {});
    Document reopened;
    io::ViewState view;
    io::loadDocument(path, reopened, view);
    std::filesystem::remove(path);
    REQUIRE(reopened.findFeature(plane)->kind() == FeatureKind::ConstructionPlane);
    REQUIRE(reopened.status(mirror).health == Health::Ok);
    REQUIRE(reopened.status(onPlane).health == Health::Ok);
    REQUIRE(reopened.result().bodies.size() == doc.result().bodies.size());
    REQUIRE_THAT(reopened.result().planes.at(plane).origin.x, Catch::Matchers::WithinAbs(30.0, 1e-9));
}

namespace {

FeatureId addAxis(Document& doc, SketchAttachment first, std::optional<SketchAttachment> second)
{
    auto a = std::make_unique<ConstructionAxisFeature>();
    a->first = std::move(first);
    a->useSecond = second.has_value();
    if (second) {
        a->second = std::move(*second);
    }
    return doc.addFeature(std::move(a));
}

SketchAttachment originAt(OriginPlane plane)
{
    SketchAttachment at;
    at.origin = plane;
    return at;
}

SketchAttachment refAt(FeatureId id)
{
    SketchAttachment at;
    at.origin.reset();
    at.reference = id;
    return at;
}

} // namespace

TEST_CASE("Un eje de construcción es la intersección de dos planos o la perpendicular a uno", "[axis]")
{
    Document doc;
    const FeatureId raised = addPlane(doc, 10.0);  // z = 10
    const FeatureId along = addAxis(doc, refAt(raised), originAt(OriginPlane::YZ));
    REQUIRE(doc.status(along).health == Health::Ok);
    const AxisLine line = doc.result().axes.at(along);
    REQUIRE_THAT(std::abs(line.direction.y), Catch::Matchers::WithinAbs(1.0, 1e-9));  // a lo largo de Y
    REQUIRE_THAT(line.point.z, Catch::Matchers::WithinAbs(10.0, 1e-9));
    REQUIRE_THAT(line.point.x, Catch::Matchers::WithinAbs(0.0, 1e-9));

    const FeatureId normal = addAxis(doc, refAt(raised), std::nullopt);
    REQUIRE_THAT(doc.result().axes.at(normal).point.z, Catch::Matchers::WithinAbs(10.0, 1e-9));
    REQUIRE_THAT(std::abs(doc.result().axes.at(normal).direction.z), Catch::Matchers::WithinAbs(1.0, 1e-9));

    // Dos planos paralelos no tienen una recta en común.
    const FeatureId parallel = addAxis(doc, originAt(OriginPlane::XY), refAt(raised));
    REQUIRE(doc.status(parallel).health == Health::Error);
}

TEST_CASE("Un patrón circular puede girar alrededor de un eje de construcción y se guarda", "[axis][pattern][io]")
{
    Document doc = boxDocument();  // x 0..20, y 0..10
    const FeatureId plane = addPlane(doc, 30.0, 0.0, 0, OriginPlane::YZ);  // x = 30
    const FeatureId axis = addAxis(doc, refAt(plane), originAt(OriginPlane::XZ));  // x = 30, y = 0: vertical
    REQUIRE(doc.status(axis).health == Health::Ok);

    auto p = std::make_unique<PatternFeature>();
    p->body = "f2";
    p->layout = PatternLayout::Circular;
    p->axisRef = axis;
    p->count = 2;
    p->angle = kPi;
    p->operation = Operation::NewBody;
    const FeatureId pattern = doc.addFeature(std::move(p));
    REQUIRE(doc.status(pattern).health == Health::Ok);
    REQUIRE(doc.result().bodies.size() == 2);
    const geometry::Box copy = doc.result().bodies[1].shape.bounds();
    REQUIRE_THAT(copy.min.x, Catch::Matchers::WithinAbs(40.0, 1e-6));
    REQUIRE_THAT(copy.max.x, Catch::Matchers::WithinAbs(60.0, 1e-6));
    REQUIRE_THAT(copy.min.y, Catch::Matchers::WithinAbs(-10.0, 1e-6));
    REQUIRE_THAT(copy.max.y, Catch::Matchers::WithinAbs(0.0, 1e-6));

    const auto path = std::filesystem::temp_directory_path() / "nuf_eje.nuf";
    std::filesystem::remove(path);
    io::saveDocument(path, doc, {});
    Document reopened;
    io::ViewState view;
    io::loadDocument(path, reopened, view);
    std::filesystem::remove(path);
    REQUIRE(reopened.findFeature(axis)->kind() == FeatureKind::ConstructionAxis);
    REQUIRE(reopened.status(pattern).health == Health::Ok);
    REQUIRE(reopened.result().bodies.size() == 2);
    REQUIRE_THAT(reopened.result().bodies[1].shape.bounds().min.x, Catch::Matchers::WithinAbs(40.0, 1e-6));
}

TEST_CASE("Las expresiones calculan con operadores, funciones, unidades y parámetros", "[params]")
{
    const auto lookup = [](std::string_view name) -> std::optional<double> {
        if (name == "grosor") {
            return 3.0;
        }
        return std::nullopt;
    };
    const auto value = [&](const char* text) { return evaluateExpression(text, lookup); };
    REQUIRE_THAT(value("2 + 3 * 4"), Catch::Matchers::WithinAbs(14.0, 1e-12));
    REQUIRE_THAT(value("(2 + 3) * 4"), Catch::Matchers::WithinAbs(20.0, 1e-12));
    REQUIRE_THAT(value("-2^2"), Catch::Matchers::WithinAbs(-4.0, 1e-12));
    REQUIRE_THAT(value("2^3^2"), Catch::Matchers::WithinAbs(512.0, 1e-12));
    REQUIRE_THAT(value("1,5 + 1"), Catch::Matchers::WithinAbs(2.5, 1e-12));
    REQUIRE_THAT(value("grosor * 2 + 1"), Catch::Matchers::WithinAbs(7.0, 1e-12));
    REQUIRE_THAT(value("sin(30)"), Catch::Matchers::WithinAbs(0.5, 1e-12));
    REQUIRE_THAT(value("atan(1)"), Catch::Matchers::WithinAbs(45.0, 1e-9));
    REQUIRE_THAT(value("min(3; 7; 2) + max(1; 4)"), Catch::Matchers::WithinAbs(6.0, 1e-12));
    REQUIRE_THAT(value("2 in"), Catch::Matchers::WithinAbs(50.8, 1e-12));
    REQUIRE_THAT(value("1 cm + 5"), Catch::Matchers::WithinAbs(15.0, 1e-12));
    REQUIRE_THAT(value("pi"), Catch::Matchers::WithinAbs(kPi, 1e-12));
    REQUIRE_THAT(value("sqrt(16)"), Catch::Matchers::WithinAbs(4.0, 1e-12));

    for (const char* bad : {"", "1 +", "(1 + 2", "foo", "1 / 0", "2 $ 3", "sin()", "min()", "sqrt(1; 2)", "3 3"}) {
        INFO(bad);
        REQUIRE_THROWS_AS(value(bad), ExpressionError);
    }
    REQUIRE(expressionNames("grosor * largo + sin(30) + grosor") == std::vector<std::string>{"grosor", "largo"});
    REQUIRE(isValidParameterName("grosor_2"));
    REQUIRE(isValidParameterName("_x"));
    for (const char* bad : {"", "2grosor", "a b", "sin", "pi", "mm", "a-b"}) {
        INFO(bad);
        REQUIRE_FALSE(isValidParameterName(bad));
    }
}

TEST_CASE("Una operación ligada a parámetros se recalcula al cambiar la tabla", "[params]")
{
    Document doc = boxDocument(5.0);
    doc.setParameters({{"grosor", "3"}, {"largo", "grosor * 10"}});
    REQUIRE_THAT(*doc.parameterValue("largo").value, Catch::Matchers::WithinAbs(30.0, 1e-12));

    auto bound = std::unique_ptr<Feature>(doc.findFeature(2)->clone());
    bound->expressions["distance"] = "largo / 2";
    doc.replaceFeature(2, std::move(bound));
    REQUIRE(doc.status(2).health == Health::Ok);
    REQUIRE_THAT(doc.result().bodies.front().shape.volume(), WithinRel(20.0 * 10.0 * 15.0, 1e-6));

    doc.setParameters({{"grosor", "4"}, {"largo", "grosor * 10"}});
    REQUIRE_THAT(doc.result().bodies.front().shape.volume(), WithinRel(20.0 * 10.0 * 20.0, 1e-6));

    // Un parámetro que no existe, o que se refiere a sí mismo, deja la operación en error.
    doc.setParameters({{"grosor", "4"}});
    REQUIRE(doc.status(2).health == Health::Error);
    REQUIRE(doc.result().bodies.empty());
    doc.setParameters({{"largo", "largo + 1"}});
    REQUIRE_FALSE(doc.parameterValue("largo").value);
    REQUIRE(doc.parameterValue("largo").error.find("circular") != std::string::npos);
    REQUIRE(doc.status(2).health == Health::Error);
    doc.setParameters({{"largo", "40"}});
    REQUIRE(doc.status(2).health == Health::Ok);
    REQUIRE_THAT(doc.result().bodies.front().shape.volume(), WithinRel(20.0 * 10.0 * 20.0, 1e-6));
}

TEST_CASE("Renombrar un parámetro actualiza las expresiones que lo usan", "[params]")
{
    using document::renameInExpression;
    REQUIRE(renameInExpression("grosor * 2 + grosor_2 + sin(grosor)", {{"grosor", "pared"}}) == "pared * 2 + grosor_2 + sin(pared)");
    REQUIRE(renameInExpression("12 mm + a", {{"a", "b"}}) == "12 mm + b");
    REQUIRE(renameInExpression("a + b", {{"a", "b"}, {"b", "a"}}) == "b + a");  // intercambio
    REQUIRE(renameInExpression("5 + 3", {{"a", "b"}}) == "5 + 3");

    Document doc = boxDocument(5.0);
    doc.setParameters({{"grosor", "3"}, {"largo", "grosor * 10"}});
    auto bound = std::unique_ptr<Feature>(doc.findFeature(2)->clone());
    bound->expressions["distance"] = "largo / 2";
    doc.replaceFeature(2, std::move(bound));
    const double volume = doc.result().bodies.front().shape.volume();
    REQUIRE(doc.status(2).health == Health::Ok);

    const auto before = doc.expressionSnapshot();
    const auto oldParameters = doc.parameters();
    // La tabla ya viene con la expresión de «largo» reescrita, como hace el diálogo.
    doc.setParameters({{"pared", "3"}, {"largo", "pared * 10"}}, {{"grosor", "pared"}});
    REQUIRE(doc.status(2).health == Health::Ok);
    REQUIRE(doc.findFeature(2)->expressions.at("distance") == "largo / 2");
    REQUIRE_THAT(doc.result().bodies.front().shape.volume(), WithinRel(volume, 1e-9));

    // Renombrar el parámetro que usa directamente la operación.
    doc.setParameters({{"pared", "3"}, {"alto", "pared * 10"}}, {{"largo", "alto"}});
    REQUIRE(doc.findFeature(2)->expressions.at("distance") == "alto / 2");
    REQUIRE(doc.status(2).health == Health::Ok);
    REQUIRE_THAT(doc.result().bodies.front().shape.volume(), WithinRel(volume, 1e-9));

    // Deshacer devuelve tabla y expresiones.
    doc.restoreParameters(oldParameters, before);
    REQUIRE(doc.parameters() == oldParameters);
    REQUIRE(doc.findFeature(2)->expressions.at("distance") == "largo / 2");
    REQUIRE(doc.status(2).health == Health::Ok);
    REQUIRE_THAT(doc.result().bodies.front().shape.volume(), WithinRel(volume, 1e-9));
}

TEST_CASE("Las cotas del sketch se ligan a parámetros", "[params][sketch]")
{
    // Un círculo en el origen cuyo radio es una cota, extruido 10 mm.
    Document doc;
    sketch::Sketch s;
    const sketch::EntityId circle = s.addCircle({0, 0}, 5.0);
    s.addConstraint(sketch::Constraint::coincident(sketch::Ref::center(circle), sketch::Ref::of(sketch::Sketch::kOrigin)));
    const sketch::ConstraintId radiusId = s.addConstraint(sketch::Constraint::radius(circle, 5.0));
    SketchAttachment at;
    at.origin = OriginPlane::XY;
    const FeatureId sk = doc.addFeature(std::make_unique<SketchFeature>(std::move(s), at));
    auto extrusion = std::make_unique<ExtrudeFeature>();
    extrusion->profiles.push_back(profileRef(sk, sketch::findProfiles(static_cast<const SketchFeature*>(doc.findFeature(sk))->sketch()).front()));
    extrusion->spec.side1.distance = 10.0;
    doc.addFeature(std::move(extrusion));
    const auto volumeFor = [&](double r) { return kPi * r * r * 10.0; };
    REQUIRE_THAT(doc.result().bodies.front().shape.volume(), WithinRel(volumeFor(5.0), 1e-3));

    // Edición: la expresión viaja en Constraint::expression y al terminar pasa a la operación.
    doc.setParameters({{"radio", "5"}});
    {
        auto edited = std::unique_ptr<SketchFeature>(static_cast<SketchFeature*>(doc.findFeature(sk)->clone().release()));
        sketch::Sketch session = edited->forEditing(doc.stateBefore(1).sketches.at(sk).sketch.operator*());
        REQUIRE(session.constraint(radiusId)->expression.empty());
        session.constraint(radiusId)->expression = "radio";
        edited->setSketchFromEditing(std::move(session));
        REQUIRE(edited->expressions.at("c" + std::to_string(radiusId)) == "radio");
        REQUIRE(edited->sketch().constraint(radiusId)->expression.empty());
        doc.replaceFeature(sk, std::move(edited));
    }
    REQUIRE(doc.status(sk).health == Health::Ok);
    REQUIRE_THAT(doc.result().bodies.front().shape.volume(), WithinRel(volumeFor(5.0), 1e-3));

    // Al cambiar el parámetro, el sketch se resuelve de nuevo y la pieza cambia.
    doc.setParameters({{"radio", "8"}});
    REQUIRE(doc.status(sk).health == Health::Ok);
    REQUIRE_THAT(doc.result().bodies.front().shape.volume(), WithinRel(volumeFor(8.0), 1e-3));
    REQUIRE_THAT(doc.stateBefore(1).sketches.at(sk).sketch->constraint(radiusId)->value, Catch::Matchers::WithinAbs(8.0, 1e-9));

    // Renombrar el parámetro actualiza la expresión de la cota.
    doc.setParameters({{"r", "8"}}, {{"radio", "r"}});
    REQUIRE(doc.findFeature(sk)->expressions.at("c" + std::to_string(radiusId)) == "r");
    REQUIRE_THAT(doc.result().bodies.front().shape.volume(), WithinRel(volumeFor(8.0), 1e-3));

    // Guardar y abrir conserva la expresión.
    const auto path = std::filesystem::temp_directory_path() / "nuf_cotas.nuf";
    std::filesystem::remove(path);
    io::saveDocument(path, doc, {});
    Document reopened;
    io::ViewState view;
    io::loadDocument(path, reopened, view);
    std::filesystem::remove(path);
    reopened.setParameters({{"r", "3"}});
    REQUIRE_THAT(reopened.result().bodies.front().shape.volume(), WithinRel(volumeFor(3.0), 1e-3));

    // Un valor imposible deja la operación en error con un mensaje, y se recupera.
    reopened.setParameters({{"r", "-2"}});
    REQUIRE(reopened.status(sk).health == Health::Error);
    reopened.setParameters({{"r", "4"}});
    REQUIRE(reopened.status(sk).health == Health::Ok);
    REQUIRE_THAT(reopened.result().bodies.front().shape.volume(), WithinRel(volumeFor(4.0), 1e-3));
}

TEST_CASE("Los parámetros y las expresiones se guardan en el archivo", "[params][io]")
{
    Document doc = boxDocument(5.0);
    doc.setParameters({{"alto", "12 mm"}});
    auto bound = std::unique_ptr<Feature>(doc.findFeature(2)->clone());
    bound->expressions["distance"] = "alto";
    doc.replaceFeature(2, std::move(bound));
    const FeatureId pattern = addPattern(doc, PatternLayout::Linear, 0, 3, 30.0, 0.0, Operation::Join);
    auto patternBound = std::unique_ptr<Feature>(doc.findFeature(pattern)->clone());
    patternBound->expressions["count"] = "alto / 4";  // 3 copias
    patternBound->expressions["angle"] = "90";
    doc.replaceFeature(pattern, std::move(patternBound));
    REQUIRE(doc.status(pattern).health == Health::Ok);

    const auto path = std::filesystem::temp_directory_path() / "nuf_parametros.nuf";
    std::filesystem::remove(path);
    io::saveDocument(path, doc, {});
    Document reopened;
    io::ViewState view;
    io::loadDocument(path, reopened, view);
    std::filesystem::remove(path);
    REQUIRE(reopened.parameters() == doc.parameters());
    REQUIRE(reopened.findFeature(2)->expressions.at("distance") == "alto");
    REQUIRE(reopened.status(pattern).health == Health::Ok);
    REQUIRE_THAT(reopened.result().bodies.front().shape.volume(), WithinRel(doc.result().bodies.front().shape.volume(), 1e-9));
    reopened.setParameters({{"alto", "6"}});
    REQUIRE_THAT(reopened.result().bodies.front().shape.volume(), WithinRel(2.0 * 20.0 * 10.0 * 6.0, 1e-6));  // 6 / 4 = 1,5 → 2 elementos
}

namespace {

FeatureId addGear(Document& doc, int teeth, double bore = 0.0, double helix = 0.0, Operation operation = Operation::NewBody)
{
    auto g = std::make_unique<GearFeature>();
    g->base.origin = OriginPlane::XY;
    g->spec.module = 2.0;
    g->spec.teeth = teeth;
    g->spec.thickness = 10.0;
    g->spec.bore = bore;
    g->spec.helixAngle = helix;
    g->operation = operation;
    return doc.addFeature(std::move(g));
}

} // namespace

TEST_CASE("Un engranaje recto tiene las medidas y las caras de su perfil de involuta", "[gear]")
{
    Document doc;
    const FeatureId id = addGear(doc, 20, 5.0);
    REQUIRE(doc.status(id).health == Health::Ok);
    REQUIRE(doc.result().bodies.size() == 1);
    const geometry::Shape& gear = doc.result().bodies.front().shape;
    REQUIRE(gear.isValid());
    // Ø primitivo 40 mm; cabeza a 22 mm (módulo 2).
    REQUIRE_THAT(gear.bounds().max.x, Catch::Matchers::WithinAbs(22.0, 1e-3));
    REQUIRE_THAT(gear.bounds().min.x, Catch::Matchers::WithinAbs(-22.0, 1e-3));
    REQUIRE_THAT(gear.bounds().max.z, Catch::Matchers::WithinAbs(10.0, 1e-6));
    // Entre el círculo de raíz y el de cabeza, cerca del primitivo, menos el agujero.
    const double expected = kPi * 20.0 * 20.0 * 10.0;
    REQUIRE(gear.volume() > 0.85 * expected);
    REQUIRE(gear.volume() < 1.05 * expected);
    // 6 caras por diente, las dos tapas y el agujero.
    REQUIRE(gear.faceCount() == 6 * 20 + 2 + 1);
    bool flank = false;
    for (int i = 0; i < gear.faceCount(); ++i) {
        flank = flank || gear.faceName(i).find(":lado:d1:flanco_d") != std::string::npos;
    }
    REQUIRE(flank);
}

TEST_CASE("Un engranaje helicoidal conserva el volumen del recto y valida sus datos", "[gear]")
{
    Document spur;
    addGear(spur, 20);
    Document helical;
    const FeatureId id = addGear(helical, 20, 5.0, 15.0 * kPi / 180.0);
    REQUIRE(helical.status(id).health == Health::Ok);
    const geometry::Shape& gear = helical.result().bodies.front().shape;
    REQUIRE(gear.isValid());
    const double spurVolume = spur.result().bodies.front().shape.volume();
    const double bore = kPi * 2.5 * 2.5 * 10.0;
    REQUIRE_THAT(gear.volume(), WithinRel(spurVolume - bore, 0.03));

    Document bad;
    REQUIRE(bad.status(addGear(bad, 5)).health == Health::Error);
    REQUIRE(bad.status(addGear(bad, 20, 40.0)).health == Health::Error);
    REQUIRE(bad.status(addGear(bad, 20, 0.0, 60.0 * kPi / 180.0)).health == Health::Error);
    REQUIRE(bad.result().bodies.empty());
}

TEST_CASE("Un engranaje ligado a parámetros se rehace al cambiarlos y se guarda", "[gear][params][io]")
{
    Document doc;
    doc.setParameters({{"dientes", "16"}});
    const FeatureId id = addGear(doc, 20);
    auto bound = std::unique_ptr<Feature>(doc.findFeature(id)->clone());
    bound->expressions["teeth"] = "dientes";
    doc.replaceFeature(id, std::move(bound));
    REQUIRE_THAT(doc.result().bodies.front().shape.bounds().max.x, Catch::Matchers::WithinAbs(18.0, 1e-3));  // rp 16 + 2

    const auto path = std::filesystem::temp_directory_path() / "nuf_engranaje.nuf";
    std::filesystem::remove(path);
    io::saveDocument(path, doc, {});
    Document reopened;
    io::ViewState view;
    io::loadDocument(path, reopened, view);
    std::filesystem::remove(path);
    REQUIRE(reopened.findFeature(id)->kind() == FeatureKind::Gear);
    REQUIRE_THAT(reopened.result().bodies.front().shape.volume(), WithinRel(doc.result().bodies.front().shape.volume(), 1e-9));
    reopened.setParameters({{"dientes", "24"}});
    REQUIRE_THAT(reopened.result().bodies.front().shape.bounds().max.x, Catch::Matchers::WithinAbs(26.0, 1e-3));
}

namespace {

FeatureId addThread(Document& doc, double diameter, double pitch, double length, bool left = false)
{
    auto t = std::make_unique<ThreadFeature>();
    t->base.origin = OriginPlane::XY;
    t->spec.diameter = diameter;
    t->spec.pitch = pitch;
    t->spec.length = length;
    t->spec.leftHanded = left;
    return doc.addFeature(std::move(t));
}

} // namespace

TEST_CASE("Una rosca métrica es una varilla con el diámetro y el largo pedidos", "[thread]")
{
    Document doc;
    const FeatureId id = addThread(doc, 8.0, 1.25, 10.0);
    INFO(doc.status(id).message);
    REQUIRE(doc.status(id).health == Health::Ok);
    REQUIRE(doc.result().bodies.size() == 1);
    const geometry::Shape& rod = doc.result().bodies.front().shape;
    REQUIRE(rod.isValid());
    REQUIRE_THAT(rod.bounds().min.z, Catch::Matchers::WithinAbs(0.0, 1e-6));
    REQUIRE_THAT(rod.bounds().max.z, Catch::Matchers::WithinAbs(10.0, 1e-6));
    REQUIRE(rod.bounds().max.x < 4.6);  // radio mayor 4 mm (la caja incluye los puntos de control de la superficie spline)
    REQUIRE(rod.bounds().max.x > 3.8);
    geometry::ThreadSpec spec;
    spec.diameter = 8.0;
    spec.pitch = 1.25;
    const double rMin = geometry::threadMinorDiameter(spec) / 2.0;
    REQUIRE(rod.volume() > kPi * rMin * rMin * 10.0);
    REQUIRE(rod.volume() < kPi * 4.0 * 4.0 * 10.0);

    // A izquierdas ocupa lo mismo; al revés (largo negativo) va hacia abajo.
    Document left;
    addThread(left, 8.0, 1.25, 10.0, true);
    REQUIRE_THAT(left.result().bodies.front().shape.volume(), WithinRel(rod.volume(), 0.01));
    Document down;
    addThread(down, 8.0, 1.25, -10.0);
    REQUIRE_THAT(down.result().bodies.front().shape.bounds().min.z, Catch::Matchers::WithinAbs(-10.0, 1e-6));
}

TEST_CASE("Una rosca da error con datos imposibles y se guarda en el archivo", "[thread][io]")
{
    Document bad;
    REQUIRE(bad.status(addThread(bad, 8.0, 0.0, 10.0)).health == Health::Error);
    REQUIRE(bad.status(addThread(bad, 1.0, 1.25, 10.0)).health == Health::Error);
    REQUIRE(bad.status(addThread(bad, 8.0, 1.25, 0.5)).health == Health::Error);

    Document doc;
    const FeatureId id = addThread(doc, 6.0, 1.0, 6.0);
    const auto path = std::filesystem::temp_directory_path() / "nuf_rosca.nuf";
    std::filesystem::remove(path);
    io::saveDocument(path, doc, {});
    Document reopened;
    io::ViewState view;
    io::loadDocument(path, reopened, view);
    std::filesystem::remove(path);
    REQUIRE(reopened.findFeature(id)->kind() == FeatureKind::Thread);
    REQUIRE_THAT(reopened.result().bodies.front().shape.volume(), WithinRel(doc.result().bodies.front().shape.volume(), 1e-9));
}

namespace {

// Perfil cuadrado de 4 mm en XZ (centrado en el origen) y una trayectoria en XY hecha con las
// líneas dadas; devuelve el barrido.
FeatureId addSweepDocument(Document& doc, const std::vector<std::pair<sketch::Vec2, sketch::Vec2>>& lines, bool follow = true)
{
    sketch::Sketch profile;
    profile.addLine({-2, -2}, {2, -2});
    profile.addLine({2, -2}, {2, 2});
    profile.addLine({2, 2}, {-2, 2});
    profile.addLine({-2, 2}, {-2, -2});
    SketchAttachment xz;
    xz.origin = OriginPlane::XZ;
    const FeatureId profileId = doc.addFeature(std::make_unique<SketchFeature>(std::move(profile), xz));

    sketch::Sketch pathSketch;
    std::vector<sketch::EntityId> ids;
    for (const auto& [a, b] : lines) {
        ids.push_back(pathSketch.addLine(a, b));
    }
    SketchAttachment xy;
    xy.origin = OriginPlane::XY;
    const FeatureId pathId = doc.addFeature(std::make_unique<SketchFeature>(std::move(pathSketch), xy));

    auto sweep = std::make_unique<SweepFeature>();
    sweep->profiles.push_back(profileRef(profileId, sketch::findProfiles(static_cast<const SketchFeature*>(doc.findFeature(profileId))->sketch()).front()));
    sweep->pathSketch = pathId;
    sweep->path = ids;
    sweep->spec.followPath = follow;
    return doc.addFeature(std::move(sweep));
}

} // namespace

TEST_CASE("Un barrido lleva el perfil a lo largo de la trayectoria", "[sweep]")
{
    Document straight;
    const FeatureId id = addSweepDocument(straight, {{{0, 0}, {0, 30}}});
    REQUIRE(straight.status(id).health == Health::Ok);
    REQUIRE(straight.result().bodies.size() == 1);
    const geometry::Shape& bar = straight.result().bodies.front().shape;
    REQUIRE(bar.isValid());
    REQUIRE_THAT(bar.volume(), WithinRel(4.0 * 4.0 * 30.0, 1e-6));
    REQUIRE_THAT(bar.bounds().min.y, Catch::Matchers::WithinAbs(0.0, 1e-6));
    REQUIRE_THAT(bar.bounds().max.y, Catch::Matchers::WithinAbs(30.0, 1e-6));
    bool side = false;
    for (int i = 0; i < bar.faceCount(); ++i) {
        side = side || bar.faceName(i).find(":lado:") != std::string::npos;
    }
    REQUIRE(side);

    // Un camino en L (dos líneas unidas): el volumen se acerca a la sección por el largo.
    Document bent;
    const FeatureId b = addSweepDocument(bent, {{{0, 0}, {0, 30}}, {{0, 30}, {20, 30}}});
    REQUIRE(bent.status(b).health == Health::Ok);
    const double volume = bent.result().bodies.front().shape.volume();
    REQUIRE(volume > 0.9 * 16.0 * 50.0);
    REQUIRE(volume < 1.2 * 16.0 * 50.0);
    REQUIRE_THAT(bent.result().bodies.front().shape.bounds().max.x, Catch::Matchers::WithinAbs(20.0, 3.0));
}

TEST_CASE("El barrido da error con una trayectoria rota y se guarda en el archivo", "[sweep][io]")
{
    Document broken;
    // Dos líneas sin tocarse.
    REQUIRE(broken.status(addSweepDocument(broken, {{{0, 0}, {0, 10}}, {{5, 20}, {5, 30}}})).health == Health::Error);

    Document doc;
    const FeatureId id = addSweepDocument(doc, {{{0, 0}, {0, 20}}});
    const auto path = std::filesystem::temp_directory_path() / "nuf_barrido.nuf";
    std::filesystem::remove(path);
    io::saveDocument(path, doc, {});
    Document reopened;
    io::ViewState view;
    io::loadDocument(path, reopened, view);
    std::filesystem::remove(path);
    REQUIRE(reopened.findFeature(id)->kind() == FeatureKind::Sweep);
    REQUIRE(reopened.status(id).health == Health::Ok);
    REQUIRE_THAT(reopened.result().bodies.front().shape.volume(), WithinRel(16.0 * 20.0, 1e-6));
}

namespace {

// Un cuadrado de lado «lado» centrado en (c, c) sobre el plano de construcción dado (0 = XY).
ProfileRef squareOn(Document& doc, FeatureId plane, double side, double center)
{
    sketch::Sketch s;
    const double lo = center - side / 2.0;
    const double hi = center + side / 2.0;
    s.addLine({lo, lo}, {hi, lo});
    s.addLine({hi, lo}, {hi, hi});
    s.addLine({hi, hi}, {lo, hi});
    s.addLine({lo, hi}, {lo, lo});
    SketchAttachment at;
    if (plane == 0) {
        at.origin = OriginPlane::XY;
    } else {
        at.origin.reset();
        at.reference = plane;
    }
    const FeatureId sk = doc.addFeature(std::make_unique<SketchFeature>(std::move(s), at));
    return profileRef(sk, sketch::findProfiles(static_cast<const SketchFeature*>(doc.findFeature(sk))->sketch()).front());
}

} // namespace

TEST_CASE("Un loft une dos perfiles como un tronco de pirámide", "[loft]")
{
    Document doc;
    const FeatureId top = addPlane(doc, 20.0);
    auto loft = std::make_unique<LoftFeature>();
    loft->profiles = {squareOn(doc, 0, 10.0, 5.0), squareOn(doc, top, 4.0, 5.0)};
    loft->ruled = true;
    const FeatureId id = doc.addFeature(std::move(loft));
    REQUIRE(doc.status(id).health == Health::Ok);
    REQUIRE(doc.result().bodies.size() == 1);
    const geometry::Shape& solid = doc.result().bodies.front().shape;
    REQUIRE(solid.isValid());
    // h/3 · (A1 + A2 + √(A1·A2)) con h = 20, A1 = 100, A2 = 16.
    REQUIRE_THAT(solid.volume(), WithinRel(20.0 / 3.0 * (100.0 + 16.0 + 40.0), 0.01));
    REQUIRE_THAT(solid.bounds().max.z, Catch::Matchers::WithinAbs(20.0, 1e-6));
    REQUIRE(solid.faceCount() == 6);
}

TEST_CASE("El loft pide dos perfiles como mínimo, admite tres y se guarda", "[loft][io]")
{
    Document one;
    auto single = std::make_unique<LoftFeature>();
    single->profiles = {squareOn(one, 0, 10.0, 5.0)};
    REQUIRE(one.status(one.addFeature(std::move(single))).health == Health::Error);

    Document doc;
    const FeatureId mid = addPlane(doc, 10.0);
    const FeatureId top = addPlane(doc, 20.0);
    auto loft = std::make_unique<LoftFeature>();
    loft->profiles = {squareOn(doc, 0, 10.0, 5.0), squareOn(doc, mid, 6.0, 5.0), squareOn(doc, top, 10.0, 5.0)};
    const FeatureId id = doc.addFeature(std::move(loft));
    REQUIRE(doc.status(id).health == Health::Ok);
    const double volume = doc.result().bodies.front().shape.volume();
    REQUIRE(volume > 0.5 * 100.0 * 20.0);  // más que un tronco muy estrecho, menos que el prisma
    REQUIRE(volume < 100.0 * 20.0);

    const auto path = std::filesystem::temp_directory_path() / "nuf_loft.nuf";
    std::filesystem::remove(path);
    io::saveDocument(path, doc, {});
    Document reopened;
    io::ViewState view;
    io::loadDocument(path, reopened, view);
    std::filesystem::remove(path);
    REQUIRE(reopened.findFeature(id)->kind() == FeatureKind::Loft);
    REQUIRE_THAT(reopened.result().bodies.front().shape.volume(), WithinRel(volume, 1e-9));
}

namespace {

FeatureId addMove(Document& doc, sketch::Vec3 translation, double angle = 0.0, bool copy = false, const std::string& body = "f2")
{
    auto m = std::make_unique<MoveFeature>();
    m->body = body;
    m->translation = translation;
    m->angle = angle;
    m->axis = 2;
    m->copy = copy;
    return doc.addFeature(std::move(m));
}

} // namespace

TEST_CASE("Mover un cuerpo lo desplaza y gira sin cambiar el nombre de sus caras", "[move]")
{
    Document doc = boxDocument();  // 20×10×5 desde el origen
    const FeatureId id = addMove(doc, {30, 5, 0});
    REQUIRE(doc.status(id).health == Health::Ok);
    REQUIRE(doc.result().bodies.size() == 1);
    const geometry::Shape& box = doc.result().bodies.front().shape;
    REQUIRE_THAT(box.bounds().min.x, Catch::Matchers::WithinAbs(30.0, 1e-6));
    REQUIRE_THAT(box.bounds().min.y, Catch::Matchers::WithinAbs(5.0, 1e-6));
    REQUIRE_THAT(box.volume(), WithinRel(kBox, 1e-9));
    REQUIRE(box.faceName(0).starts_with("f2:"));

    // 90° alrededor de Z por el centro del cuerpo: queda de 10×20 con el mismo centro.
    Document turned = boxDocument();
    addMove(turned, {0, 0, 0}, kPi / 2.0);
    const geometry::Box tb = turned.result().bodies.front().shape.bounds();
    REQUIRE_THAT(tb.max.x - tb.min.x, Catch::Matchers::WithinAbs(10.0, 1e-6));
    REQUIRE_THAT(tb.max.y - tb.min.y, Catch::Matchers::WithinAbs(20.0, 1e-6));
    REQUIRE_THAT((tb.min.x + tb.max.x) / 2.0, Catch::Matchers::WithinAbs(10.0, 1e-6));

    // Una copia deja el original y crea un cuerpo nuevo con otros nombres.
    Document copy = boxDocument();
    const FeatureId c = addMove(copy, {0, 20, 0}, 0.0, true);
    REQUIRE(copy.status(c).health == Health::Ok);
    REQUIRE(copy.result().bodies.size() == 2);
    REQUIRE_THAT(copy.result().bodies[0].shape.bounds().min.y, Catch::Matchers::WithinAbs(0.0, 1e-6));
    REQUIRE_THAT(copy.result().bodies[1].shape.bounds().min.y, Catch::Matchers::WithinAbs(20.0, 1e-6));
    REQUIRE(copy.result().bodies[1].shape.faceName(0).find(":copia:") != std::string::npos);

    REQUIRE(doc.status(addMove(doc, {1, 0, 0}, 0.0, false, "f99")).health == Health::Error);
}

TEST_CASE("Combinar cuerpos une, corta o interseca, y se guarda", "[combine][io]")
{
    const auto overlapping = [](Document& doc) {
        addMove(doc, {15, 0, 0}, 0.0, true);  // segunda caja de x = 15 a 35
    };
    const auto combine = [](Document& doc, Operation op, bool keep) {
        auto c = std::make_unique<CombineFeature>();
        c->target = "f2";
        c->tools = {"f3"};
        c->operation = op;
        c->keepTools = keep;
        return doc.addFeature(std::move(c));
    };
    Document join = boxDocument();
    overlapping(join);
    const FeatureId j = combine(join, Operation::Join, false);
    REQUIRE(join.status(j).health == Health::Ok);
    REQUIRE(join.result().bodies.size() == 1);
    REQUIRE_THAT(join.result().bodies.front().shape.volume(), WithinRel(2.0 * kBox - 5.0 * 10.0 * 5.0, 1e-6));

    Document cut = boxDocument();
    overlapping(cut);
    REQUIRE(cut.status(combine(cut, Operation::Cut, true)).health == Health::Ok);
    REQUIRE(cut.result().bodies.size() == 2);  // la herramienta se conserva
    REQUIRE_THAT(cut.result().bodies.front().shape.volume(), WithinRel(kBox - 250.0, 1e-6));

    Document common = boxDocument();
    overlapping(common);
    REQUIRE(common.status(combine(common, Operation::Intersect, false)).health == Health::Ok);
    REQUIRE_THAT(common.result().bodies.front().shape.volume(), WithinRel(250.0, 1e-6));

    Document none = boxDocument();
    auto empty = std::make_unique<CombineFeature>();
    empty->target = "f2";
    REQUIRE(none.status(none.addFeature(std::move(empty))).health == Health::Error);

    const auto path = std::filesystem::temp_directory_path() / "nuf_combinar.nuf";
    std::filesystem::remove(path);
    io::saveDocument(path, join, {});
    Document reopened;
    io::ViewState view;
    io::loadDocument(path, reopened, view);
    std::filesystem::remove(path);
    REQUIRE(reopened.findFeature(j)->kind() == FeatureKind::Combine);
    REQUIRE(reopened.status(j).health == Health::Ok);
    REQUIRE_THAT(reopened.result().bodies.front().shape.volume(), WithinRel(join.result().bodies.front().shape.volume(), 1e-9));
}

TEST_CASE("Los cuerpos pueden llevar nombre propio y componente, con deshacer y archivo", "[bodies][io]")
{
    Document doc = boxDocument();
    addMove(doc, {0, 20, 0}, 0.0, true);  // Cuerpo2
    REQUIRE(doc.result().bodies[0].name == "Extrusión 1");
    REQUIRE(doc.result().bodies[1].name == "Copia 1");

    const std::map<std::string, BodyInfo> info{{"f2", {"Base", "Carcasa"}}, {"f3", {"Tapa", "Carcasa"}}};
    doc.setBodyInfo(info);
    REQUIRE(doc.result().bodies[0].name == "Base");
    REQUIRE(doc.result().bodies[1].name == "Tapa");
    REQUIRE(doc.componentOf("f2") == "Carcasa");
    REQUIRE(doc.componentOf("f99").empty());

    // Un cambio posterior en el historial conserva los nombres.
    const FeatureId later = addMove(doc, {0, 0, 10});
    REQUIRE(doc.status(later).health == Health::Ok);
    REQUIRE(doc.result().bodies.size() == 2);
    REQUIRE(doc.result().bodies[0].name == "Base");

    const auto path = std::filesystem::temp_directory_path() / "nuf_cuerpos.nuf";
    std::filesystem::remove(path);
    io::saveDocument(path, doc, {});
    Document reopened;
    io::ViewState view;
    io::loadDocument(path, reopened, view);
    std::filesystem::remove(path);
    REQUIRE(reopened.bodyInfo() == doc.bodyInfo());
    REQUIRE(reopened.result().bodies[1].name == "Tapa");

    // Quitar el nombre devuelve el de fábrica.
    doc.setBodyInfo({{"f2", {"", "Carcasa"}}});
    REQUIRE(doc.result().bodies[0].name == "Extrusión 1");
    REQUIRE(doc.componentOf("f2") == "Carcasa");
}

namespace {

// Una segunda caja (copia de la primera, lejos) y una unión entre la cara superior de la primera y la
// inferior de la segunda.
FeatureId addJoint(Document& doc, JointType type, double offset, double angle, double slide)
{
    addMove(doc, {100, 0, 0}, 0.0, true);  // Cuerpo2 = f3
    const geometry::Shape& first = doc.result().bodies[0].shape;
    const geometry::Shape& second = doc.result().bodies[1].shape;
    std::optional<geometry::ElementRef> top;
    std::optional<geometry::ElementRef> bottom;
    for (int i = 0; i < first.faceCount(); ++i) {
        if (const auto plane = first.facePlane(i); plane && plane->normal.z > 0.9) {
            top = first.faceRef(i);
        }
    }
    for (int i = 0; i < second.faceCount(); ++i) {
        if (const auto plane = second.facePlane(i); plane && plane->normal.z < -0.9) {
            bottom = second.faceRef(i);
        }
    }
    auto j = std::make_unique<JointFeature>();
    j->fixedFace = *top;
    j->movingFace = *bottom;
    j->type = type;
    j->offset = offset;
    j->angle = angle;
    j->slide = slide;
    return doc.addFeature(std::move(j));
}

} // namespace

TEST_CASE("Una unión apoya un cuerpo sobre otro por sus caras", "[joint]")
{
    Document doc = boxDocument();  // 20×10×5
    const FeatureId id = addJoint(doc, JointType::Rigid, 0.0, 0.0, 0.0);
    REQUIRE(doc.status(id).health == Health::Ok);
    REQUIRE(doc.result().bodies.size() == 2);
    const geometry::Box moved = doc.result().bodies[1].shape.bounds();
    REQUIRE_THAT(moved.min.x, Catch::Matchers::WithinAbs(0.0, 1e-6));
    REQUIRE_THAT(moved.max.x, Catch::Matchers::WithinAbs(20.0, 1e-6));
    REQUIRE_THAT(moved.min.z, Catch::Matchers::WithinAbs(5.0, 1e-6));
    REQUIRE_THAT(moved.max.z, Catch::Matchers::WithinAbs(10.0, 1e-6));
    REQUIRE_THAT(doc.result().bodies[1].shape.volume(), WithinRel(kBox, 1e-9));

    // La fija no se mueve.
    REQUIRE_THAT(doc.result().bodies[0].shape.bounds().max.z, Catch::Matchers::WithinAbs(5.0, 1e-6));

    // Con distancia, el cuerpo móvil se separa.
    Document apart = boxDocument();
    addJoint(apart, JointType::Rigid, 3.0, 0.0, 0.0);
    REQUIRE_THAT(apart.result().bodies[1].shape.bounds().min.z, Catch::Matchers::WithinAbs(8.0, 1e-6));

    // Giratoria 90°: la planta de 20×10 queda de 10×20 con el mismo centro.
    Document turned = boxDocument();
    addJoint(turned, JointType::Revolute, 0.0, kPi / 2.0, 0.0);
    const geometry::Box tb = turned.result().bodies[1].shape.bounds();
    REQUIRE_THAT(tb.max.x - tb.min.x, Catch::Matchers::WithinAbs(10.0, 1e-6));
    REQUIRE_THAT(tb.max.y - tb.min.y, Catch::Matchers::WithinAbs(20.0, 1e-6));
    REQUIRE_THAT((tb.min.x + tb.max.x) / 2.0, Catch::Matchers::WithinAbs(10.0, 1e-6));

    // Deslizante 4 mm en X.
    Document slid = boxDocument();
    addJoint(slid, JointType::Slider, 0.0, 0.0, 4.0);
    REQUIRE_THAT(slid.result().bodies[1].shape.bounds().min.x, Catch::Matchers::WithinAbs(4.0, 1e-6));
}

TEST_CASE("La unión sigue a las piezas, avisa de los errores y se guarda", "[joint][io]")
{
    Document doc = boxDocument();
    const FeatureId id = addJoint(doc, JointType::Rigid, 0.0, 0.0, 0.0);

    // Si la caja fija se hace más alta, la otra sube con ella.
    auto taller = std::unique_ptr<Feature>(doc.findFeature(2)->clone());
    static_cast<ExtrudeFeature&>(*taller).spec.side1.distance = 9.0;
    doc.replaceFeature(2, std::move(taller));
    REQUIRE(doc.status(id).health == Health::Ok);
    REQUIRE_THAT(doc.result().bodies[1].shape.bounds().min.z, Catch::Matchers::WithinAbs(9.0, 1e-6));

    // Dos caras del mismo cuerpo dan error.
    auto same = std::make_unique<JointFeature>();
    same->fixedFace = doc.result().bodies[0].shape.faceRef(0);
    same->movingFace = doc.result().bodies[0].shape.faceRef(1);
    REQUIRE(doc.status(doc.addFeature(std::move(same))).health == Health::Error);

    const auto path = std::filesystem::temp_directory_path() / "nuf_union.nuf";
    std::filesystem::remove(path);
    io::saveDocument(path, doc, {});
    Document reopened;
    io::ViewState view;
    io::loadDocument(path, reopened, view);
    std::filesystem::remove(path);
    REQUIRE(reopened.findFeature(id)->kind() == FeatureKind::Joint);
    REQUIRE(reopened.status(id).health == Health::Ok);
    REQUIRE_THAT(reopened.result().bodies[1].shape.bounds().min.z, Catch::Matchers::WithinAbs(9.0, 1e-6));
}

namespace {

std::vector<std::pair<std::string, geometry::Shape>> namedBodies(const Document& doc)
{
    std::vector<std::pair<std::string, geometry::Shape>> bodies;
    for (const Body& body : doc.result().bodies) {
        bodies.emplace_back(body.name, body.shape);
    }
    return bodies;
}

bool mentions(const std::vector<geometry::PrintIssue>& issues, const std::string& text)
{
    return std::ranges::any_of(issues, [&](const geometry::PrintIssue& i) { return i.text.find(text) != std::string::npos; });
}

} // namespace

TEST_CASE("La revisión para imprimir da el visto bueno a una pieza normal", "[print]")
{
    Document doc = boxDocument();  // 20×10×5
    REQUIRE(geometry::checkPrintable(namedBodies(doc), {}).empty());
}

TEST_CASE("La revisión avisa de paredes finas, cuerpos solapados y piezas que no caben", "[print]")
{
    // Una lámina de 0,3 mm es más fina que una boquilla de 0,4; de 0,6 sale débil; de 1 mm está bien.
    for (const auto& [thickness, expected] : std::vector<std::pair<double, bool>>{{0.3, true}, {0.6, true}, {1.0, false}}) {
        Document doc = boxDocument(thickness);
        const auto issues = geometry::checkPrintable(namedBodies(doc), {});
        INFO(thickness);
        REQUIRE(mentions(issues, "Pared de") == expected);
    }
    {
        Document doc = boxDocument(0.3);
        REQUIRE(mentions(geometry::checkPrintable(namedBodies(doc), {}), "no se imprimirá"));
    }

    // Un vaciado con paredes de 0,3 mm.
    Document shelled = boxDocument(10.0);
    const geometry::Shape& box = shelled.result().bodies.front().shape;
    auto shell = std::make_unique<ShellFeature>();
    for (int i = 0; i < box.faceCount(); ++i) {
        if (const auto plane = box.facePlane(i); plane && plane->normal.z > 0.9) {
            shell->faces.push_back(box.faceRef(i));
        }
    }
    shell->thickness = 0.3;
    REQUIRE(shelled.status(shelled.addFeature(std::move(shell))).health == Health::Ok);
    REQUIRE(mentions(geometry::checkPrintable(namedBodies(shelled), {}), "Pared de"));

    // Dos cajas que se solapan.
    Document overlapping = boxDocument();
    addMove(overlapping, {15, 0, 0}, 0.0, true);
    const auto overlapIssues = geometry::checkPrintable(namedBodies(overlapping), {});
    REQUIRE(mentions(overlapIssues, "se solapan"));
    Document apart = boxDocument();
    addMove(apart, {50, 0, 0}, 0.0, true);
    REQUIRE_FALSE(mentions(geometry::checkPrintable(namedBodies(apart), {}), "se solapan"));

    // La cama de 15 mm no admite una pieza de 20×10×5; girada tampoco.
    geometry::PrintSettings small;
    small.bedX = small.bedY = small.bedZ = 15.0;
    REQUIRE(mentions(geometry::checkPrintable(namedBodies(Document(boxDocument())), small), "no cabe"));
    geometry::PrintSettings tight;
    tight.bedX = 30.0;
    tight.bedY = 12.0;
    tight.bedZ = 6.0;
    REQUIRE_FALSE(mentions(geometry::checkPrintable(namedBodies(Document(boxDocument())), tight), "no cabe"));
}

namespace {

int faceWithNormal(const geometry::Shape& shape, double x, double y, double z)
{
    for (int i = 0; i < shape.faceCount(); ++i) {
        if (const auto plane = shape.facePlane(i);
            plane && plane->normal.x * x + plane->normal.y * y + plane->normal.z * z > 0.99) {
            return i;
        }
    }
    return -1;
}

bool hasLine(const geometry::Measurement& m, const std::string& text)
{
    return std::ranges::any_of(m.lines, [&](const std::string& l) { return l.find(text) != std::string::npos; });
}

} // namespace

TEST_CASE("Medir una cara, una arista y la relación entre dos caras", "[measure]")
{
    Document doc = boxDocument();  // 20×10×5, arriba en z = 5
    const geometry::Shape& box = doc.result().bodies.front().shape;
    const int top = faceWithNormal(box, 0, 0, 1);
    const int bottom = faceWithNormal(box, 0, 0, -1);
    const int side = faceWithNormal(box, 1, 0, 0);
    REQUIRE(top >= 0);
    REQUIRE(bottom >= 0);
    REQUIRE(side >= 0);

    const geometry::Measurement one = geometry::measure({{box, true, top}});
    REQUIRE(one.title == "Cara plana");
    REQUIRE(hasLine(one, "Área: 200.00 mm²"));
    REQUIRE_FALSE(one.hasSegment);

    const geometry::Measurement parallel = geometry::measure({{box, true, top}, {box, true, bottom}});
    REQUIRE(parallel.hasSegment);
    REQUIRE_THAT(parallel.distance, Catch::Matchers::WithinAbs(5.0, 1e-6));
    REQUIRE(hasLine(parallel, "Paralelas, separadas 5.00 mm"));

    const geometry::Measurement square = geometry::measure({{box, true, top}, {box, true, side}});
    REQUIRE(hasLine(square, "Ángulo entre caras: 90.00°"));
    REQUIRE_THAT(square.distance, Catch::Matchers::WithinAbs(0.0, 1e-6));  // comparten una arista

    // Una arista recta de 20 mm.
    for (int i = 0; i < box.edgeCount(); ++i) {
        if (const auto line = box.edgeLine(i); line && std::abs(line->second.x) > 0.99) {
            REQUIRE(hasLine(geometry::measure({{box, false, i}}), "Longitud: 20.00 mm"));
            break;
        }
    }

    // Sin elementos válidos no hay medición.
    REQUIRE(geometry::measure({}).lines.empty());
    REQUIRE(geometry::measure({{box, true, 999}}).lines.empty());
}

#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

TEST_CASE("Medida de tiempo de la rosca", "[.perf]")
{
    using Clock = std::chrono::steady_clock;
    const auto plane = sketch::Plane::fromPointAndNormal({0, 0, 0}, {0, 0, 1});
    for (const auto& [d, p, len] : std::vector<std::tuple<double, double, double>>{
             {8, 1.25, 10}, {8, 1.25, 20}, {8, 1.25, 40}, {3, 0.5, 10}, {8, 0.5, 10}, {20, 2.5, 30}}) {
        geometry::ThreadSpec spec;
        spec.diameter = d;
        spec.pitch = p;
        spec.length = len;
        const auto t0 = Clock::now();
        geometry::Shape s = geometry::thread("t", plane, spec);
        const auto t1 = Clock::now();
        const bool ok = s.isValid();
        const auto t2 = Clock::now();
        const int faces = s.faceCount();
        std::cout << "d=" << d << " p=" << p << " L=" << len << " vueltas=" << len / p << " thread(): "
                  << std::chrono::duration<double>(t1 - t0).count() << " s (incl. isValid interna), isValid otra vez: "
                  << std::chrono::duration<double>(t2 - t1).count() << " s, caras=" << faces << " valido=" << ok << "\n";
    }
}

TEST_CASE("Una rosca se une y se corta bien con un bloque (y sus caras salen enteras)", "[thread]")
{
    Document doc = boxDocument(8.0);  // 20×10×8
    const geometry::Shape& block = doc.result().bodies.front().shape;
    geometry::ThreadSpec spec;
    spec.diameter = 8.0;
    spec.pitch = 1.25;
    spec.length = 10.0;
    auto plane = sketch::Plane::fromPointAndNormal({10, 5, -1}, {0, 0, 1});
    plane.origin = {10, 5, -1};  // el centro del bloque, asomando un milímetro por abajo y por arriba
    const geometry::Shape rod = geometry::thread("t", plane, spec);
    REQUIRE(rod.isValid());
    REQUIRE(rod.faceCount() == 8);  // seis tramos de ~1,5 vueltas y dos tapas, no cientos de caras
    // La caja puede ser algo mayor (incluye los puntos de control de la superficie), nunca menor.
    REQUIRE(rod.bounds().max.x >= 13.99);
    REQUIRE(rod.bounds().max.x < 14.6);
    REQUIRE(rod.bounds().min.x <= 6.01);
    const double inside = rod.volume() * 0.8;  // 8 de los 10 mm de largo quedan dentro del bloque

    const geometry::Shape cut = geometry::boolean(geometry::BooleanOp::Cut, block, rod);
    REQUIRE(cut.solidCount() == 1);
    REQUIRE_THAT(cut.volume(), Catch::Matchers::WithinAbs(1600.0 - inside, 25.0));
    const geometry::Shape fused = geometry::boolean(geometry::BooleanOp::Fuse, block, rod);
    REQUIRE(fused.solidCount() == 1);
    REQUIRE_THAT(fused.volume(), Catch::Matchers::WithinAbs(1600.0 + rod.volume() - inside, 25.0));

    // A izquierdas y con el largo hacia el otro lado también salen sólidos válidos y del mismo volumen.
    spec.leftHanded = true;
    const geometry::Shape left = geometry::thread("t", plane, spec);
    REQUIRE(left.isValid());
    REQUIRE_THAT(left.volume(), Catch::Matchers::WithinRel(rod.volume(), 0.01));
    spec.leftHanded = false;
    spec.length = -10.0;
    const geometry::Shape down = geometry::thread("t", plane, spec);
    REQUIRE(down.isValid());
    REQUIRE_THAT(down.volume(), Catch::Matchers::WithinRel(rod.volume(), 0.01));
    REQUIRE(down.bounds().max.z < -0.9);
}

TEST_CASE("Medida de tiempo del engranaje", "[.perf]")
{
    using Clock = std::chrono::steady_clock;
    const auto plane = sketch::Plane::fromPointAndNormal({0, 0, 0}, {0, 0, 1});
    for (const auto& [teeth, helix, bore] : std::vector<std::tuple<int, double, double>>{
             {12, 0, 0}, {20, 0, 5}, {40, 0, 5}, {12, 15, 0}, {20, 15, 5}, {40, 15, 5}, {20, 30, 5}}) {
        geometry::GearSpec spec;
        spec.teeth = teeth;
        spec.helixAngle = helix * 3.14159265358979 / 180.0;
        spec.bore = bore;
        spec.thickness = 10.0;
        const auto t0 = Clock::now();
        const geometry::Shape s = geometry::gear("g", plane, spec);
        const double dt = std::chrono::duration<double>(Clock::now() - t0).count();
        std::cout << "dientes=" << teeth << " helice=" << helix << " agujero=" << bore << " gear(): " << dt << " s, caras=" << s.faceCount()
                  << " valido=" << s.isValid() << std::endl;
    }
}

TEST_CASE("Un engranaje helicoidal con agujero sale hueco y hacia el lado pedido, y su volumen no cambia con el sentido", "[gear]")
{
    const auto plane = sketch::Plane::fromPointAndNormal({0, 0, 0}, {0, 0, 1});
    geometry::GearSpec spec;
    spec.teeth = 16;
    spec.bore = 5.0;
    spec.thickness = 8.0;
    spec.helixAngle = 20.0 * std::numbers::pi / 180.0;
    const geometry::Shape up = geometry::gear("g", plane, spec);
    REQUIRE(up.isValid());
    REQUIRE(up.bounds().min.z > -1e-3);
    REQUIRE(up.bounds().max.z < 8.001);
    spec.thickness = -8.0;
    spec.helixAngle = -spec.helixAngle;
    const geometry::Shape down = geometry::gear("g", plane, spec);
    REQUIRE(down.isValid());
    REQUIRE(down.bounds().max.z < 1e-3);
    REQUIRE(down.bounds().min.z > -8.001);
    REQUIRE_THAT(down.volume(), Catch::Matchers::WithinRel(up.volume(), 0.01));
    // Con el agujero pasante pierde el volumen del cilindro central (pi · 2,5² · 8) respecto al macizo.
    spec.thickness = 8.0;
    spec.helixAngle = 20.0 * std::numbers::pi / 180.0;
    spec.bore = 0.0;
    const geometry::Shape solid = geometry::gear("g", plane, spec);
    REQUIRE_THAT(solid.volume() - up.volume(), Catch::Matchers::WithinRel(std::numbers::pi * 2.5 * 2.5 * 8.0, 0.03));
}

#include <nuf/geometry/Exchange.hpp>

TEST_CASE("Medida de tiempo del mallado de la rosca (como lo dibuja el visor)", "[.perf]")
{
    using Clock = std::chrono::steady_clock;
    const auto plane = sketch::Plane::fromPointAndNormal({0, 0, 0}, {0, 0, 1});
    for (const auto& [d, p, len, defl] : std::vector<std::tuple<double, double, double, double>>{
             {8, 1.25, 10, 0.1}, {8, 1.25, 40, 0.1}, {8, 1.25, 10, 0.4}}) {
        geometry::ThreadSpec spec;
        spec.diameter = d;
        spec.pitch = p;
        spec.length = len;
        const geometry::Shape s = geometry::thread("t", plane, spec);
        const auto t0 = Clock::now();
        const auto triangles = geometry::countTriangles({s}, {defl, 0.5});
        std::cout << "d=" << d << " L=" << len << " desviacion=" << defl << ": " << triangles << " triangulos en "
                  << std::chrono::duration<double>(Clock::now() - t0).count() << " s" << std::endl;
    }
}

TEST_CASE("La malla de una rosca se prepara en otro hilo y se pasa al cuerpo original", "[thread]")
{
    const auto plane = sketch::Plane::fromPointAndNormal({0, 0, 0}, {0, 0, 1});
    geometry::ThreadSpec spec;
    spec.diameter = 8.0;
    spec.pitch = 1.25;
    spec.length = 10.0;
    const geometry::Shape rod = geometry::thread("t", plane, spec);
    REQUIRE(geometry::hasHeavySurface(rod));
    REQUIRE_FALSE(geometry::hasHeavySurface(boxDocument().result().bodies.front().shape));
    REQUIRE_FALSE(geometry::hasMesh(rod));

    std::atomic<bool> cancel{false};
    geometry::PreparedMesh mesh;
    std::thread worker([&] { mesh = geometry::prepareMesh(rod, geometry::kHeavyDeviation, geometry::kHeavyAngle, cancel); });
    worker.join();
    REQUIRE(mesh.valid());
    REQUIRE_FALSE(geometry::hasMesh(rod));  // el original no se toca hasta aplicar
    geometry::applyMesh(rod, mesh);
    REQUIRE(geometry::hasMesh(rod));

    // Cancelada, no da malla.
    const geometry::Shape other = geometry::thread("t", plane, spec);
    std::atomic<bool> stop{true};
    REQUIRE_FALSE(geometry::prepareMesh(other, geometry::kHeavyDeviation, geometry::kHeavyAngle, stop).valid());
}

namespace {

// Hace de «hilo de la interfaz»: lo que los hilos de cálculo publican se ejecuta aquí, cuando se pide.
class MainQueue {
public:
    void post(std::function<void()> f)
    {
        std::lock_guard lock(mutex_);
        queue_.push_back(std::move(f));
    }
    // Ejecuta lo publicado hasta que «done» sea cierto (o se acaba el tiempo).
    bool pumpUntil(const std::function<bool()>& done, std::chrono::seconds limit = std::chrono::seconds(60))
    {
        const auto end = std::chrono::steady_clock::now() + limit;
        while (!done()) {
            if (std::chrono::steady_clock::now() > end) {
                return false;
            }
            std::function<void()> next;
            {
                std::lock_guard lock(mutex_);
                if (!queue_.empty()) {
                    next = std::move(queue_.front());
                    queue_.pop_front();
                }
            }
            if (next) {
                next();
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
        }
        return true;
    }

private:
    std::mutex mutex_;
    std::deque<std::function<void()>> queue_;
};

} // namespace

TEST_CASE("El recálculo en segundo plano da lo mismo que el inmediato y no bloquea", "[background]")
{
    const Document expected = boxDocument(5.0);
    const double volume = expected.result().bodies.front().shape.volume();

    MainQueue queue;
    Document doc;
    doc.enableBackgroundCompute([&](std::function<void()> f) { queue.post(std::move(f)); });
    int changes = 0;
    std::vector<bool> states;
    doc.changed.connect([&] { ++changes; });
    doc.computingChanged.connect([&](bool on) { states.push_back(on); });

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
    extrusion->spec.side1.distance = 5.0;
    const FeatureId ex = doc.addFeature(std::move(extrusion));

    // Los cambios vuelven al instante; el modelo aún no es el nuevo.
    REQUIRE(doc.computing());
    REQUIRE(doc.featureCount() == 2);
    REQUIRE(doc.result().bodies.empty());

    REQUIRE(queue.pumpUntil([&] { return !doc.computing(); }));
    REQUIRE(doc.status(ex).health == Health::Ok);
    REQUIRE(doc.result().bodies.size() == 1);
    REQUIRE_THAT(doc.result().bodies.front().shape.volume(), WithinRel(volume, 1e-9));
    REQUIRE(states == std::vector<bool>{true, false});  // un solo cálculo vigente, aunque hubo dos cambios
    REQUIRE(changes >= 3);
}

TEST_CASE("Mover gira un cuerpo alrededor de X, Y y Z por su centro y se guarda", "[move]")
{
    Document doc = boxDocument(5.0);  // 20 × 10 × 5
    const std::string key = doc.result().bodies.front().key;
    const geometry::Box original = doc.result().bodies.front().shape.bounds();
    const double cx = 0.5 * (original.min.x + original.max.x);
    const double cy = 0.5 * (original.min.y + original.max.y);

    auto move = std::make_unique<MoveFeature>();
    move->body = key;
    move->rotation = {0.0, 0.0, kPi / 2.0};  // un cuarto de vuelta alrededor de Z
    const FeatureId id = doc.addFeature(std::move(move));
    REQUIRE(doc.status(id).health == Health::Ok);
    const geometry::Box turned = doc.result().bodies.front().shape.bounds();
    // 20 × 10 pasa a 10 × 20, con el mismo centro.
    REQUIRE_THAT(turned.max.x - turned.min.x, Catch::Matchers::WithinAbs(10.0, 0.05));
    REQUIRE_THAT(turned.max.y - turned.min.y, Catch::Matchers::WithinAbs(20.0, 0.05));
    REQUIRE_THAT(0.5 * (turned.min.x + turned.max.x), Catch::Matchers::WithinAbs(cx, 0.05));
    REQUIRE_THAT(0.5 * (turned.min.y + turned.max.y), Catch::Matchers::WithinAbs(cy, 0.05));

    // Alrededor de X: el alto (5) y el fondo (10) se cambian.
    auto flip = std::make_unique<MoveFeature>(*static_cast<const MoveFeature*>(doc.findFeature(id)));
    flip->rotation = {kPi / 2.0, 0.0, 0.0};
    doc.replaceFeature(id, std::move(flip));
    const geometry::Box rolled = doc.result().bodies.front().shape.bounds();
    REQUIRE_THAT(rolled.max.y - rolled.min.y, Catch::Matchers::WithinAbs(5.0, 0.05));
    REQUIRE_THAT(rolled.max.z - rolled.min.z, Catch::Matchers::WithinAbs(10.0, 0.05));

    // Los giros y el desplazamiento se guardan en el archivo.
    auto both = std::make_unique<MoveFeature>(*static_cast<const MoveFeature*>(doc.findFeature(id)));
    both->rotation = {0.0, kPi / 2.0, kPi / 4.0};
    both->translation = {3.0, 0.0, 0.0};
    doc.replaceFeature(id, std::move(both));
    const auto path = std::filesystem::temp_directory_path() / "nuf_giro.nuf";
    std::filesystem::remove(path);
    io::saveDocument(path, doc, {});
    Document reopened;
    io::ViewState view;
    io::loadDocument(path, reopened, view);
    const auto* loaded = static_cast<const MoveFeature*>(reopened.findFeature(id));
    REQUIRE(loaded != nullptr);
    REQUIRE_THAT(loaded->rotation.y, Catch::Matchers::WithinAbs(kPi / 2.0, 1e-9));
    REQUIRE_THAT(loaded->rotation.z, Catch::Matchers::WithinAbs(kPi / 4.0, 1e-9));
    REQUIRE_THAT(loaded->translation.x, Catch::Matchers::WithinAbs(3.0, 1e-9));
    const geometry::Box a = doc.result().bodies.front().shape.bounds();
    const geometry::Box b = reopened.result().bodies.front().shape.bounds();
    REQUIRE_THAT(b.max.x - b.min.x, Catch::Matchers::WithinAbs(a.max.x - a.min.x, 1e-6));
    REQUIRE_THAT(b.max.z - b.min.z, Catch::Matchers::WithinAbs(a.max.z - a.min.z, 1e-6));
    std::filesystem::remove(path);
}

TEST_CASE("Mover varios cuerpos a la vez los mueve y los gira como un solo bloque", "[move]")
{
    Document doc;
    const auto addBox = [&](double x) {
        auto box = std::make_unique<BoxFeature>();
        box->base.origin = OriginPlane::XY;
        box->x = x;
        box->width = 10.0;
        box->depth = 10.0;
        box->height = 5.0;
        box->operation = Operation::NewBody;
        doc.addFeature(std::move(box));
        return doc.result().bodies.back().key;
    };
    const std::string a = addBox(0.0);
    const std::string b = addBox(30.0);  // los dos cuerpos, separados (como la placa y el texto de un llavero)
    REQUIRE(doc.result().bodies.size() == 2);
    const auto centerOf = [&](const std::string& key) {
        for (const Body& body : doc.result().bodies) {
            if (body.key == key) {
                const geometry::Box box = body.shape.bounds();
                return sketch::Vec3{0.5 * (box.min.x + box.max.x), 0.5 * (box.min.y + box.max.y), 0.5 * (box.min.z + box.max.z)};
            }
        }
        return sketch::Vec3{};
    };
    const sketch::Vec3 ca = centerOf(a);
    const sketch::Vec3 cb = centerOf(b);
    const sketch::Vec3 pivot{0.5 * (ca.x + cb.x), 0.5 * (ca.y + cb.y), 0.5 * (ca.z + cb.z)};

    auto move = std::make_unique<MoveFeature>();
    move->body = a;
    move->alsoMove = {b};
    move->translation = {0.0, 0.0, 7.0};
    move->rotation = {0.0, 0.0, kPi / 2.0};  // un cuarto de vuelta alrededor de Z por el centro de los dos
    const FeatureId id = doc.addFeature(std::move(move));
    REQUIRE(doc.status(id).health == Health::Ok);
    // Cada centro gira 90° alrededor del pivote común y sube 7: (dx, dy) → (−dy, dx).
    const sketch::Vec3 na = centerOf(a);
    const sketch::Vec3 nb = centerOf(b);
    REQUIRE_THAT(na.x, Catch::Matchers::WithinAbs(pivot.x - (ca.y - pivot.y), 0.05));
    REQUIRE_THAT(na.y, Catch::Matchers::WithinAbs(pivot.y + (ca.x - pivot.x), 0.05));
    REQUIRE_THAT(nb.x, Catch::Matchers::WithinAbs(pivot.x - (cb.y - pivot.y), 0.05));
    REQUIRE_THAT(nb.y, Catch::Matchers::WithinAbs(pivot.y + (cb.x - pivot.x), 0.05));
    REQUIRE_THAT(na.z, Catch::Matchers::WithinAbs(ca.z + 7.0, 0.05));
    REQUIRE_THAT(nb.z, Catch::Matchers::WithinAbs(cb.z + 7.0, 0.05));
    // La distancia entre los dos no cambia.
    REQUIRE_THAT(std::hypot(na.x - nb.x, na.y - nb.y), Catch::Matchers::WithinAbs(std::hypot(ca.x - cb.x, ca.y - cb.y), 0.05));
    // Depende de los dos cuerpos.
    REQUIRE(doc.findFeature(id)->dependencies().size() >= 2);

    // Se guarda en el archivo.
    const auto path = std::filesystem::temp_directory_path() / "nuf_varios.nuf";
    std::filesystem::remove(path);
    io::saveDocument(path, doc, {});
    Document reopened;
    io::ViewState view;
    io::loadDocument(path, reopened, view);
    const auto* loaded = static_cast<const MoveFeature*>(reopened.findFeature(id));
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->alsoMove == std::vector<std::string>{b});
    std::filesystem::remove(path);

    // Copiar varios a la vez todavía no se puede: avisa en vez de hacer algo a medias.
    auto copy = std::make_unique<MoveFeature>(*static_cast<const MoveFeature*>(doc.findFeature(id)));
    copy->copy = true;
    doc.replaceFeature(id, std::move(copy));
    REQUIRE(doc.status(id).health == Health::Error);
}

TEST_CASE("Un cambio nuevo cancela el cálculo en curso y solo vale el último", "[background]")
{
    MainQueue queue;
    Document doc;
    doc.enableBackgroundCompute([&](std::function<void()> f) { queue.post(std::move(f)); });

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
    extrusion->spec.side1.distance = 5.0;
    const FeatureId ex = doc.addFeature(std::move(extrusion));
    // Se cambia dos veces seguidas sin esperar.
    for (const double depth : {7.0, 9.0}) {
        auto edited = std::make_unique<ExtrudeFeature>(*static_cast<const ExtrudeFeature*>(doc.findFeature(ex)));
        edited->spec.side1.distance = depth;
        doc.replaceFeature(ex, std::move(edited));
    }
    REQUIRE(queue.pumpUntil([&] { return !doc.computing(); }));
    REQUIRE_THAT(doc.result().bodies.front().shape.volume(), WithinRel(20.0 * 10.0 * 9.0, 1e-9));

    // Con otro cambio a medias, esperar a que acabe también sirve (guardar, exportar).
    auto again = std::make_unique<ExtrudeFeature>(*static_cast<const ExtrudeFeature*>(doc.findFeature(ex)));
    again->spec.side1.distance = 4.0;
    doc.replaceFeature(ex, std::move(again));
    doc.waitForCompute();
    REQUIRE_FALSE(doc.computing());
    REQUIRE_THAT(doc.result().bodies.front().shape.volume(), WithinRel(20.0 * 10.0 * 4.0, 1e-9));
}

TEST_CASE("Una booleana se interrumpe cuando el hilo de trabajo pide cancelar", "[background][thread]")
{
    const auto plane = sketch::Plane::fromPointAndNormal({0, 0, 0}, {0, 0, 1});
    geometry::ThreadSpec spec;
    spec.diameter = 8.0;
    spec.pitch = 1.25;
    spec.length = 10.0;
    const geometry::Shape rod = geometry::thread("t", plane, spec);
    const geometry::Shape block = boxDocument().result().bodies.front().shape;

    REQUIRE_FALSE(geometry::cancelRequested());
    std::atomic<bool> stop{true};
    {
        geometry::CancelScope scope(stop);
        REQUIRE(geometry::cancelRequested());
        REQUIRE_THROWS_AS(geometry::boolean(geometry::BooleanOp::Cut, block, rod), geometry::GeometryError);
    }
    REQUIRE_FALSE(geometry::cancelRequested());
    // Sin petición de cancelar, la misma booleana sale bien.
    REQUIRE_FALSE(geometry::boolean(geometry::BooleanOp::Cut, block, rod).isNull());
}

// Fase 13, F4: cerrar la ventana o cambiar algo no debe esperar a que termine una rosca, un engranaje helicoidal, un
// redondeo, un vaciado ni una unión de partes: todas las construcciones largas miran la petición de cancelar.
TEST_CASE("Las construcciones largas se interrumpen cuando el hilo de trabajo pide cancelar", "[background][thread]")
{
    const auto plane = sketch::Plane::fromPointAndNormal({0, 0, 0}, {0, 0, 1});
    const geometry::Shape block = geometry::box("b", plane, 20.0, 10.0, 5.0, false);
    const geometry::Shape other = geometry::box("c", plane, 10.0, 10.0, 8.0, false);
    geometry::ThreadSpec threadSpec;
    threadSpec.length = 40.0;
    geometry::GearSpec helical;
    helical.teeth = 20;
    helical.helixAngle = 0.4;
    helical.thickness = 10.0;
    geometry::GearSpec plain;
    plain.teeth = 20;
    plain.thickness = 10.0;

    std::atomic<bool> stop{true};
    {
        geometry::CancelScope scope(stop);
        const auto cancelled = [](const std::function<void()>& call) {
            REQUIRE_THROWS_WITH(call(), Catch::Matchers::ContainsSubstring("cancelado"));
        };
        cancelled([&] { geometry::thread("t", plane, threadSpec); });
        cancelled([&] { geometry::gear("g", plane, helical); });
        cancelled([&] { geometry::fillet("f", block, {0, 1}, 0.5); });
        cancelled([&] { geometry::chamfer("c", block, {0, 1}, 0.5); });
        cancelled([&] { geometry::shell("s", block, {0}, 1.0, false); });
        cancelled([&] { geometry::boolean(geometry::BooleanOp::Fuse, block, other); });
    }
    REQUIRE_FALSE(geometry::cancelRequested());
    // Sin petición de cancelar, las mismas operaciones salen bien.
    REQUIRE_FALSE(geometry::thread("t", plane, threadSpec).isNull());
    REQUIRE_FALSE(geometry::gear("g", plane, plain).isNull());
    REQUIRE_FALSE(geometry::fillet("f", block, {0, 1}, 0.5).isNull());
    REQUIRE_FALSE(geometry::chamfer("c", block, {0, 1}, 0.5).isNull());
    REQUIRE_FALSE(geometry::shell("s", block, {0}, 1.0, false).isNull());
}

// Si la malla preparada en segundo plano no lleva los polígonos de las aristas, el visor no dibuja el borde entre
// caras y las caras del mismo color se funden (se vio al mover el mallado de todos los cuerpos fuera de la interfaz).
TEST_CASE("La malla preparada en segundo plano lleva también el borde de cada cara", "[background][mesh]")
{
    const auto plane = sketch::Plane::fromPointAndNormal({0, 0, 0}, {0, 0, 1});
    const std::atomic<bool> never{false};
    for (const geometry::Shape& body : {geometry::box("b", plane, 20.0, 10.0, 5.0, false),
                                        geometry::cylinder("c", plane, 10.0, 8.0)}) {  // el cilindro tiene costura
        REQUIRE_FALSE(geometry::hasMesh(body));
        REQUIRE_FALSE(geometry::hasBoundaryPolygons(body));
        const geometry::PreparedMesh mesh = geometry::prepareMesh(body, 0.05, 0.5, never);
        REQUIRE(mesh.valid());
        geometry::applyMesh(body, mesh);
        REQUIRE(geometry::hasMesh(body));
        REQUIRE(geometry::hasBoundaryPolygons(body));
    }
}

// Fase 13, F5: el índice de nombres y la caja se guardan en la forma; los resultados no deben cambiar, ni siquiera
// con nombres repetidos (una cara partida por un corte) ni al pedirlos desde varios hilos a la vez.
TEST_CASE("Buscar caras y aristas por nombre da lo mismo con el índice guardado, también desde varios hilos", "[background][names]")
{
    const auto plane = sketch::Plane::fromPointAndNormal({0, 0, 0}, {0, 0, 1});
    geometry::Shape body = geometry::box("b", plane, 40.0, 20.0, 10.0, false);
    const geometry::Shape slot = geometry::box("s", plane, 10.0, 30.0, 4.0, false);
    // Un surco que atraviesa la cara de arriba: la parte en dos con el mismo nombre.
    const geometry::Shape moved = geometry::moveBody("m", slot, sketch::Vec3{15.0, -5.0, 6.0}, geometry::Axis{}, 0.0, false);
    const geometry::Shape cut = geometry::boolean(geometry::BooleanOp::Cut, body, moved);
    REQUIRE_FALSE(cut.isNull());

    const auto roundTrip = [](const geometry::Shape& shape) {
        for (int i = 0; i < shape.faceCount(); ++i) {
            const auto found = shape.findFace(shape.faceRef(i));
            REQUIRE(found.has_value());
            REQUIRE(*found == i);
        }
        for (int i = 0; i < shape.edgeCount(); ++i) {
            const auto found = shape.findEdge(shape.edgeRef(i));
            REQUIRE(found.has_value());
            REQUIRE(*found == i);
        }
    };
    roundTrip(cut);
    // Una consulta repetida y otra desde varios hilos a la vez dan lo mismo.
    const geometry::Box first = cut.bounds();
    std::vector<std::thread> threads;
    std::atomic<int> mismatches{0};
    for (int k = 0; k < 4; ++k) {
        threads.emplace_back([&] {
            for (int i = 0; i < cut.faceCount(); ++i) {
                const auto found = cut.findFace(cut.faceRef(i));
                if (!found || *found != i) {
                    ++mismatches;
                }
            }
            const geometry::Box again = cut.bounds();
            if (again.min.x != first.min.x || again.max.z != first.max.z) {
                ++mismatches;
            }
        });
    }
    for (std::thread& thread : threads) {
        thread.join();
    }
    REQUIRE(mismatches == 0);
}

TEST_CASE("Una rosca y un engranaje se colocan en una posición X/Y del plano", "[thread][gear]")
{
    Document doc;
    auto thread = std::make_unique<ThreadFeature>();
    thread->base.origin = OriginPlane::XY;
    thread->x = 10.0;
    thread->y = 5.0;
    thread->spec.length = 10.0;
    const FeatureId t = doc.addFeature(std::move(thread));
    REQUIRE(doc.status(t).health == Health::Ok);
    const geometry::Box tb = doc.result().bodies.back().shape.bounds();
    REQUIRE_THAT(0.5 * (tb.min.x + tb.max.x), Catch::Matchers::WithinAbs(10.0, 0.05));
    REQUIRE_THAT(0.5 * (tb.min.y + tb.max.y), Catch::Matchers::WithinAbs(5.0, 0.05));

    auto gear = std::make_unique<GearFeature>();
    gear->base.origin = OriginPlane::XY;
    gear->x = -30.0;
    gear->y = 12.0;
    const FeatureId g = doc.addFeature(std::move(gear));
    REQUIRE(doc.status(g).health == Health::Ok);
    const geometry::Box gb = doc.result().bodies.back().shape.bounds();
    REQUIRE_THAT(0.5 * (gb.min.x + gb.max.x), Catch::Matchers::WithinAbs(-30.0, 0.05));
    REQUIRE_THAT(0.5 * (gb.min.y + gb.max.y), Catch::Matchers::WithinAbs(12.0, 0.05));

    // La posición y la holgura se guardan en el archivo.
    auto edited = std::make_unique<ThreadFeature>(*static_cast<const ThreadFeature*>(doc.findFeature(t)));
    edited->clearance = 0.3;
    doc.replaceFeature(t, std::move(edited));
    const auto path = std::filesystem::temp_directory_path() / "nuf_posicion.nuf";
    std::filesystem::remove(path);
    io::saveDocument(path, doc, {});
    Document reopened;
    io::ViewState view;
    io::loadDocument(path, reopened, view);
    std::filesystem::remove(path);
    const auto* rt = static_cast<const ThreadFeature*>(reopened.findFeature(t));
    REQUIRE_THAT(rt->x, Catch::Matchers::WithinAbs(10.0, 1e-9));
    REQUIRE_THAT(rt->y, Catch::Matchers::WithinAbs(5.0, 1e-9));
    REQUIRE_THAT(rt->clearance, Catch::Matchers::WithinAbs(0.3, 1e-9));
    const auto* rg = static_cast<const GearFeature*>(reopened.findFeature(g));
    REQUIRE_THAT(rg->x, Catch::Matchers::WithinAbs(-30.0, 1e-9));
}

TEST_CASE("Una rosca interior corta en una cara hacia dentro, con el agujero más ancho si hay holgura", "[thread]")
{
    Document doc = boxDocument(8.0);  // 20×10×8
    const geometry::Shape& block = doc.result().bodies.front().shape;
    int top = -1;
    for (int i = 0; i < block.faceCount(); ++i) {
        if (const auto plane = block.facePlane(i); plane && plane->normal.z > 0.99) {
            top = i;
        }
    }
    REQUIRE(top >= 0);
    const auto facePlane = *block.facePlane(top);
    const sketch::Vec2 center = facePlane.toLocal(block.faceCenter(top));

    const auto cutWith = [&](double clearance) {
        Document copy = boxDocument(8.0);
        auto t = std::make_unique<ThreadFeature>();
        t->base.origin.reset();
        t->base.face = copy.result().bodies.front().shape.faceRef(top);
        t->x = center.x;
        t->y = center.y;
        t->spec.length = -6.0;  // hacia dentro de la pieza
        t->clearance = clearance;
        t->operation = Operation::Cut;
        const FeatureId id = copy.addFeature(std::move(t));
        REQUIRE(copy.status(id).health == Health::Ok);
        REQUIRE(copy.result().bodies.size() == 1);
        return copy.result().bodies.front().shape.volume();
    };
    const double tight = cutWith(0.0);
    const double loose = cutWith(0.5);
    REQUIRE_THAT(tight, Catch::Matchers::WithinAbs(1600.0 - 243.0, 40.0));  // ≈ 6 mm de una rosca M8
    REQUIRE(loose < tight - 30.0);                                         // más holgura, más material fuera
}

TEST_CASE("Una caja y un cilindro se colocan, se cortan y se ligan a parámetros", "[box]")
{
    Document doc;
    doc.setParameters({{"ancho", "30"}, {"pared", "2"}});
    auto box = std::make_unique<BoxFeature>();
    box->base.origin = OriginPlane::XY;
    box->width = 99.0;  // el valor de la expresión manda
    box->expressions["width"] = "ancho";
    box->depth = 20.0;
    box->height = 10.0;
    box->centered = true;
    const FeatureId b = doc.addFeature(std::move(box));
    REQUIRE(doc.status(b).health == Health::Ok);
    const geometry::Shape& shape = doc.result().bodies.front().shape;
    REQUIRE_THAT(shape.volume(), Catch::Matchers::WithinAbs(30.0 * 20.0 * 10.0, 1e-6));
    REQUIRE_THAT(shape.bounds().min.x, Catch::Matchers::WithinAbs(-15.0, 1e-6));
    REQUIRE_THAT(shape.bounds().max.z, Catch::Matchers::WithinAbs(10.0, 1e-6));

    // Las caras se llaman por su orientación.
    bool found = false;
    for (int i = 0; i < shape.faceCount(); ++i) {
        if (shape.faceName(i) == "f" + std::to_string(b) + ":cara:+x") {
            REQUIRE(shape.facePlane(i)->normal.x > 0.99);
            found = true;
        }
    }
    REQUIRE(found);

    // Un cilindro que corta un agujero pasante, centrado en el alto de la caja y repartido a los dos lados.
    auto hole = std::make_unique<CylinderFeature>();
    hole->base.origin = OriginPlane::XY;
    hole->diameter = 4.0;
    hole->height = 20.0;
    hole->z = 5.0;
    hole->symmetric = true;
    hole->operation = Operation::Cut;
    REQUIRE(doc.status(doc.addFeature(std::move(hole))).health == Health::Ok);
    const double expected = 30.0 * 20.0 * 10.0 - std::numbers::pi * 2.0 * 2.0 * 10.0;
    REQUIRE_THAT(doc.result().bodies.front().shape.volume(), Catch::Matchers::WithinAbs(expected, 0.5));

    // Al cambiar el parámetro, la caja se recalcula.
    doc.setParameters({{"ancho", "40"}, {"pared", "2"}});
    REQUIRE_THAT(doc.result().bodies.front().shape.bounds().max.x, Catch::Matchers::WithinAbs(20.0, 1e-6));

    // Hacia el otro lado con alto negativo, y se guarda.
    auto below = std::make_unique<BoxFeature>();
    below->base.origin = OriginPlane::XY;
    below->x = 100.0;
    below->height = -5.0;
    const FeatureId d = doc.addFeature(std::move(below));
    REQUIRE_THAT(doc.result().bodies.back().shape.bounds().max.z, Catch::Matchers::WithinAbs(0.0, 1e-6));
    REQUIRE_THAT(doc.result().bodies.back().shape.bounds().min.z, Catch::Matchers::WithinAbs(-5.0, 1e-6));

    const auto path = std::filesystem::temp_directory_path() / "nuf_caja.nuf";
    std::filesystem::remove(path);
    io::saveDocument(path, doc, {});
    Document reopened;
    io::ViewState view;
    io::loadDocument(path, reopened, view);
    std::filesystem::remove(path);
    const auto* rb = static_cast<const BoxFeature*>(reopened.findFeature(b));
    REQUIRE(rb->centered);
    REQUIRE(rb->expressions.at("width") == "ancho");
    REQUIRE(reopened.findFeature(d)->kind() == FeatureKind::Box);
    REQUIRE_THAT(reopened.result().bodies.front().shape.bounds().max.x, Catch::Matchers::WithinAbs(20.0, 1e-6));
}

TEST_CASE("El generador de lightbox hace una caja y una tapa de un solo cuerpo ligados a parámetros", "[generator]")
{
    const document::Generator* lightbox = document::findGenerator("lightbox");
    REQUIRE(lightbox != nullptr);
    document::GeneratorValues values = lightbox->defaults();
    values["cable"] = 0.0;  // sin agujero: los volúmenes se calculan a mano

    Document doc;
    commands::AddRecipe command(lightbox->make(values, ""), "Lightbox");
    command.execute(doc);
    REQUIRE(doc.result().bodies.size() == 2);  // la caja y la tapa (marco, labio y cara unidos)
    for (std::size_t i = 0; i < doc.featureCount(); ++i) {
        INFO(doc.feature(i).name());
        REQUIRE(doc.status(doc.feature(i).id()).health != Health::Error);
    }
    const char* names[] = {"Caja", "Tapa"};
    for (std::size_t i = 0; i < 2; ++i) {
        REQUIRE(doc.bodyInfo().at(doc.result().bodies[i].key).name == names[i]);
        REQUIRE(doc.bodyInfo().at(doc.result().bodies[i].key).component == "Lightbox 1");
    }

    // Como la de MakerLab: caja de 175 × 131 × 37 con paredes y fondo de 2 mm y esquinas de radio 2 por fuera;
    // la imagen mide 171 × 127.
    const double pi = std::numbers::pi;
    const double outer = 175.0 * 131.0 - (4.0 - pi) * 2.0 * 2.0;
    const auto volume = [&](int body) { return doc.result().bodies[std::size_t(body)].shape.volume(); };
    const double box = outer * 37.0 - 171.0 * 127.0 * 35.0;
    REQUIRE_THAT(volume(0), Catch::Matchers::WithinAbs(box, 3.0));
    REQUIRE(doc.result().bodies[0].shape.bounds().min.z == Catch::Approx(0.0).margin(1e-6));  // el fondo, en la cama
    REQUIRE(doc.result().bodies[0].shape.bounds().max.z == Catch::Approx(37.0).margin(1e-6));
    // La tapa, un solo cuerpo: la placa del tamaño de la caja (marco y cara central, 0,8 mm) más el labio (1 mm de
    // grueso y 5 de alto, 0,1 de holgura).
    REQUIRE_THAT(volume(1), Catch::Matchers::WithinAbs(outer * 0.8 + (170.8 * 126.8 - 168.8 * 124.8) * 5.0, 4.0));
    // La tapa va al lado y apoyada en la cama; el labio sobresale 5 mm del frente.
    REQUIRE(doc.result().bodies[1].shape.bounds().min.x > 80.0);
    REQUIRE(doc.result().bodies[1].shape.bounds().min.z == Catch::Approx(0.0).margin(1e-6));
    REQUIRE(doc.result().bodies[1].shape.bounds().max.z == Catch::Approx(5.8).margin(1e-6));

    // Los parámetros mandan: con más pared, menos hueco.
    auto params = doc.parameters();
    for (document::Parameter& p : params) {
        if (p.name == "pared") {
            p.expression = "4";
        }
    }
    doc.setParameters(params);
    REQUIRE(volume(0) > box + 1000.0);

    // Deshacer lo deja todo como estaba, y rehacer lo vuelve a poner.
    command.undo(doc);
    REQUIRE(doc.featureCount() == 0);
    REQUIRE(doc.parameters().empty());
    REQUIRE(doc.bodyInfo().empty());
    command.execute(doc);
    REQUIRE(doc.result().bodies.size() == 2);

    // Una segunda vez usa otros nombres de parámetros.
    REQUIRE(document::freeSuffix(doc, *lightbox, values) == "_2");
}

TEST_CASE("El lightbox redondo", "[generator]")
{
    const document::Generator* lightbox = document::findGenerator("lightbox");
    document::GeneratorValues values = lightbox->defaults();
    values["forma"] = 1.0;
    values["pared_cable"] = 1.0;  // por la pared de abajo
    Document doc;
    document::applyRecipe(doc, lightbox->make(values, ""));
    REQUIRE(doc.result().bodies.size() == 2);
    const double pi = std::numbers::pi;
    const double hollow = pi * 75.0 * 75.0 * 37.0 - pi * 73.0 * 73.0 * 35.0;
    // El agujero del cable (Ø6 por una pared de 2) le quita algo de material a la caja.
    const double volume = doc.result().bodies[0].shape.volume();
    REQUIRE(volume < hollow - 20.0);
    REQUIRE(volume > hollow - 200.0);
    REQUIRE(doc.result().bodies[0].shape.bounds().max.z == Catch::Approx(37.0).margin(1e-6));
    // La tapa redonda: placa de Ø150 × 0,8 más el labio (Ø148,8 por fuera, 1 mm de grueso, 5 de alto).
    REQUIRE_THAT(doc.result().bodies[1].shape.volume(),
                 Catch::Matchers::WithinAbs(pi * 75.0 * 75.0 * 0.8 + pi * (72.9 * 72.9 - 71.9 * 71.9) * 5.0, 4.0));
}

TEST_CASE("El agujero del cable del lightbox sale por la pared elegida", "[generator]")
{
    const document::Generator* lightbox = document::findGenerator("lightbox");
    const auto volumes = [&](double wall, double cable) {
        document::GeneratorValues values = lightbox->defaults();
        values["pared_cable"] = wall;
        values["cable"] = cable;
        Document doc;
        document::applyRecipe(doc, lightbox->make(values, ""));
        REQUIRE(doc.result().bodies.size() == 2);
        for (std::size_t i = 0; i < doc.featureCount(); ++i) {
            REQUIRE(doc.status(doc.feature(i).id()).health != Health::Error);
        }
        std::vector<double> result;
        for (const Body& body : doc.result().bodies) {
            result.push_back(body.shape.volume());
        }
        return result;
    };
    const std::vector<double> solid = volumes(0.0, 0.0);  // sin agujero
    for (const double wall : {0.0, 1.0, 2.0, 3.0, 4.0}) {  // por el fondo o por una pared: solo la caja pierde material
        INFO(wall);
        const std::vector<double> with = volumes(wall, 6.0);
        REQUIRE(with[0] < solid[0] - 30.0);
        for (std::size_t i = 1; i < 2; ++i) {
            REQUIRE_THAT(with[i], Catch::Matchers::WithinAbs(solid[i], 1e-3));
        }
    }
}

TEST_CASE("Una caja con esquinas redondeadas", "[box]")
{
    const double r = 3.0;
    const geometry::Shape rounded = geometry::box("c", sketch::Plane::xy(), 40.0, 20.0, 10.0, true, r);
    REQUIRE(rounded.isValid());
    REQUIRE_THAT(rounded.volume(), Catch::Matchers::WithinAbs((40.0 * 20.0 - (4.0 - std::numbers::pi) * r * r) * 10.0, 1e-3));
    REQUIRE_THAT(rounded.bounds().min.x, Catch::Matchers::WithinAbs(-20.0, 1e-6));
    REQUIRE_THAT(rounded.bounds().max.z, Catch::Matchers::WithinAbs(10.0, 1e-6));
    // Hacia el otro lado y con las caras de siempre.
    const geometry::Shape down = geometry::box("c", sketch::Plane::xy(), 40.0, 20.0, -10.0, true, r);
    REQUIRE_THAT(down.bounds().min.z, Catch::Matchers::WithinAbs(-10.0, 1e-6));
    int faces = 0;
    for (int i = 0; i < down.faceCount(); ++i) {
        faces += down.faceName(i).starts_with("c:cara:") ? 1 : 0;
    }
    REQUIRE(faces == 10);  // 4 lados, 4 esquinas, fondo y tapa
}

TEST_CASE("La caja con tapa cierra con labio a presión o con tornillos en las esquinas", "[generator]")
{
    const document::Generator* generator = document::findGenerator("caja-con-tapa");
    REQUIRE(generator != nullptr);
    const double pi = std::numbers::pi;
    const auto build = [&](double closure, Document& doc) {
        document::GeneratorValues values = generator->defaults();
        values["cierre"] = closure;
        document::applyRecipe(doc, generator->make(values, ""));
        REQUIRE(doc.result().bodies.size() == 2);
        for (std::size_t i = 0; i < doc.featureCount(); ++i) {
            INFO(doc.feature(i).name());
            REQUIRE(doc.status(doc.feature(i).id()).health != Health::Error);
        }
    };

    Document lip;
    build(0.0, lip);
    // 100 × 60 × 40 con pared y base de 2 y esquinas de radio 3: el hueco es 96 × 56 × 38.
    const double outer = 100.0 * 60.0 - (4.0 - pi) * 9.0;
    const double boxLip = outer * 40.0 - 96.0 * 56.0 * 38.0;
    REQUIRE_THAT(lip.result().bodies[0].shape.volume(), Catch::Matchers::WithinAbs(boxLip, 3.0));
    // La tapa: placa de 2 mm y labio de 3 mm (hueco − 2·0,2 de holgura por fuera, 1,2 de grueso).
    const double ring = (95.6 * 55.6) - (93.2 * 53.2);
    REQUIRE_THAT(lip.result().bodies[1].shape.volume(), Catch::Matchers::WithinAbs(outer * 2.0 + ring * 3.0, 4.0));
    REQUIRE(lip.result().bodies[1].shape.bounds().min.x > 50.0);  // al lado de la caja

    Document screws;
    build(1.0, screws);
    // Los cuatro postes aportan material (y los agujeros se llevan un poco); la tapa pierde cuatro agujeros.
    const double boxScrews = screws.result().bodies[0].shape.volume();
    REQUIRE(boxScrews > boxLip + 3000.0);
    REQUIRE(boxScrews < boxLip + 9000.0);
    REQUIRE_THAT(screws.result().bodies[1].shape.volume(),
                 Catch::Matchers::WithinAbs(outer * 2.0 - 4.0 * pi * 1.7 * 1.7 * 2.0, 3.0));
    // Los postes llegan hasta el borde de la caja.
    REQUIRE(screws.result().bodies[0].shape.bounds().max.z == Catch::Approx(40.0).margin(1e-6));

    // Cambiar un parámetro cambia todo: más alta, más material.
    auto params = screws.parameters();
    for (document::Parameter& p : params) {
        if (p.name == "alto") {
            p.expression = "60";
        }
    }
    screws.setParameters(params);
    REQUIRE(screws.result().bodies[0].shape.volume() > boxScrews + 5000.0);
    REQUIRE(screws.result().bodies[0].shape.bounds().max.z == Catch::Approx(60.0).margin(1e-6));
}

namespace {

// Un «texto» de mentira para las pruebas: cada letra es un cuadrado de 10 × 14 con un hueco de 4 × 8 (como una «O»).
std::vector<sketch::Geometry> fakeLetters(const std::string& text, double, const std::string&)
{
    std::vector<sketch::Geometry> out;
    const auto rectangle = [&](double x0, double y0, double x1, double y1) {
        out.push_back(sketch::Line{{x0, y0}, {x1, y0}});
        out.push_back(sketch::Line{{x1, y0}, {x1, y1}});
        out.push_back(sketch::Line{{x1, y1}, {x0, y1}});
        out.push_back(sketch::Line{{x0, y1}, {x0, y0}});
    };
    for (std::size_t i = 0; i < text.size(); ++i) {
        const double x = double(i) * 12.0;
        rectangle(x, 0.0, x + 10.0, 14.0);
        rectangle(x + 3.0, 3.0, x + 7.0, 11.0);
    }
    return out;
}

} // namespace

TEST_CASE("El generador de letreros pone las letras en relieve, grabadas o sueltas, y hace llaveros", "[generator]")
{
    document::setTextOutliner(fakeLetters);
    const document::Generator* generator = document::findGenerator("letrero");
    REQUIRE(generator != nullptr);
    const double pi = std::numbers::pi;
    const auto make = [&](double type, double mode, Document& doc) {
        document::GeneratorValues values = generator->defaults();
        values["tipo"] = type;
        values["modo"] = mode;
        values.texts["texto"] = "OO";  // dos letras de 10 × 14 con hueco de 4 × 8, a 12 mm una de otra
        REQUIRE(doc.parameters().empty());
        commands::AddRecipe command(generator->make(values, ""), "Letrero");
        command.execute(doc);
        for (std::size_t i = 0; i < doc.featureCount(); ++i) {
            INFO(doc.feature(i).name());
            REQUIRE(doc.status(doc.feature(i).id()).health != Health::Error);
        }
    };
    // El texto mide 22 × 14; con 5 mm de margen la placa es de 32 × 24 × 3 y esquinas de radio 3.
    const double plateArea = 32.0 * 24.0 - (4.0 - pi) * 9.0;
    const double letterArea = 2.0 * (10.0 * 14.0 - 4.0 * 8.0);

    // Por defecto, el texto en relieve va unido a la placa: un solo cuerpo.
    REQUIRE(generator->defaults().at("modo") == 3.0);
    Document single;
    make(0.0, 3.0, single);
    REQUIRE(single.result().bodies.size() == 1);
    REQUIRE_THAT(single.result().bodies[0].shape.volume(), Catch::Matchers::WithinAbs(plateArea * 3.0 + letterArea * 1.2, 1e-2));
    REQUIRE(single.result().bodies[0].shape.bounds().max.z == Catch::Approx(4.2).margin(1e-6));
    REQUIRE(single.bodyInfo().at(single.result().bodies[0].key).name == "Placa");
    REQUIRE(single.bodyInfo().at(single.result().bodies[0].key).component == "Letrero 1");
    Document singleKey;
    make(1.0, 3.0, singleKey);  // el llavero con anilla y agujero, también en un cuerpo
    REQUIRE(singleKey.result().bodies.size() == 1);
    REQUIRE(singleKey.result().bodies[0].shape.volume() > plateArea * 3.0 + letterArea * 1.2 + 100.0);
    REQUIRE(singleKey.result().bodies[0].shape.bounds().min.x < -16.0 - 9.0);

    Document relief;
    make(0.0, 0.0, relief);
    REQUIRE(relief.result().bodies.size() == 2);
    REQUIRE_THAT(relief.result().bodies[0].shape.volume(), Catch::Matchers::WithinAbs(plateArea * 3.0, 1e-3));
    REQUIRE_THAT(relief.result().bodies[1].shape.volume(), Catch::Matchers::WithinAbs(letterArea * 1.2, 1e-3));
    REQUIRE(relief.result().bodies[1].shape.bounds().min.z == Catch::Approx(3.0).margin(1e-6));  // sobre la placa
    REQUIRE(relief.result().bodies[1].shape.bounds().max.z == Catch::Approx(4.2).margin(1e-6));
    REQUIRE(relief.bodyInfo().at(relief.result().bodies[1].key).name == "Texto");
    REQUIRE(relief.bodyInfo().at(relief.result().bodies[0].key).component == "Letrero 1");
    // Letras centradas en la placa.
    const geometry::Box text = relief.result().bodies[1].shape.bounds();
    REQUIRE_THAT(0.5 * (text.min.x + text.max.x), Catch::Matchers::WithinAbs(0.0, 1e-6));

    Document engraved;
    make(0.0, 1.0, engraved);
    REQUIRE(engraved.result().bodies.size() == 1);
    REQUIRE_THAT(engraved.result().bodies[0].shape.volume(),
                 Catch::Matchers::WithinAbs(plateArea * 3.0 - letterArea * 1.2, 1e-3));

    Document loose;
    make(0.0, 2.0, loose);
    REQUIRE(loose.result().bodies.size() == 1);
    REQUIRE_THAT(loose.result().bodies[0].shape.volume(), Catch::Matchers::WithinAbs(letterArea * 1.2, 1e-3));

    // El llavero suma el disco de la anilla (Ø12, metido 2 mm en la placa) y le resta el agujero (Ø5).
    Document keychain;
    make(1.0, 0.0, keychain);
    REQUIRE(keychain.result().bodies.size() == 2);
    REQUIRE(keychain.result().bodies[0].shape.volume() > plateArea * 3.0 + 100.0);
    REQUIRE(keychain.result().bodies[0].shape.bounds().min.x < -16.0 - 9.0);  // la anilla asoma por la izquierda
    REQUIRE(keychain.bodyInfo().at(keychain.result().bodies[0].key).component == "Llavero 1");

    // Todo se recalcula al cambiar un parámetro (el grosor de la placa) y se deshace de una vez.
    auto params = relief.parameters();
    for (document::Parameter& p : params) {
        if (p.name == "grosor") {
            p.expression = "5";
        }
    }
    relief.setParameters(params);
    REQUIRE(relief.result().bodies[1].shape.bounds().min.z == Catch::Approx(5.0).margin(1e-6));
    document::setTextOutliner({});
}

TEST_CASE("El letrero admite varias líneas, alineadas a la izquierda, al centro o a la derecha", "[generator]")
{
    document::setTextOutliner(fakeLetters);
    const document::Generator* generator = document::findGenerator("letrero");
    REQUIRE(generator != nullptr);
    // Dos líneas: «OOO» (34 de ancho) y «O» (10); 20 de alto de texto → de una línea base a la siguiente, 20·1,1 + 2 = 24.
    const auto secondLine = [&](double alignment) {
        document::GeneratorValues values = generator->defaults();
        values["modo"] = 2.0;  // solo letras
        values["alineacion"] = alignment;
        values.texts["texto"] = "OOO\nO";
        const document::Recipe recipe = generator->make(values, "");
        const auto* letters = dynamic_cast<const SketchFeature*>(recipe.features[0].get());
        REQUIRE(letters != nullptr);
        double minX = 1e300, maxX = -1e300, minY = 1e300, maxY = -1e300, lowMin = 1e300, lowMax = -1e300;
        for (const sketch::Entity& e : letters->sketch().entities()) {
            for (const sketch::Vec2& p : sketch::polyline(e.geometry)) {
                minX = std::min(minX, p.x);
                maxX = std::max(maxX, p.x);
                minY = std::min(minY, p.y);
                maxY = std::max(maxY, p.y);
                if (p.y < 0.0) {  // la segunda línea queda por debajo del centro
                    lowMin = std::min(lowMin, p.x);
                    lowMax = std::max(lowMax, p.x);
                }
            }
        }
        // El texto entero mide 34 × 38 (14 de la primera línea + 24 hasta la base de la segunda) y va centrado en el origen.
        REQUIRE_THAT(maxX - minX, Catch::Matchers::WithinAbs(34.0, 1e-6));
        REQUIRE_THAT(maxY - minY, Catch::Matchers::WithinAbs(38.0, 1e-6));
        REQUIRE_THAT(minX + maxX, Catch::Matchers::WithinAbs(0.0, 1e-6));
        return std::pair{lowMin, lowMax};
    };
    const auto centered = secondLine(0.0);
    CHECK_THAT(centered.first, Catch::Matchers::WithinAbs(-5.0, 1e-6));
    CHECK_THAT(centered.second, Catch::Matchers::WithinAbs(5.0, 1e-6));
    const auto left = secondLine(1.0);
    CHECK_THAT(left.first, Catch::Matchers::WithinAbs(-17.0, 1e-6));
    CHECK_THAT(left.second, Catch::Matchers::WithinAbs(-7.0, 1e-6));
    const auto right = secondLine(2.0);
    CHECK_THAT(right.first, Catch::Matchers::WithinAbs(7.0, 1e-6));
    CHECK_THAT(right.second, Catch::Matchers::WithinAbs(17.0, 1e-6));
    // Un diseño anterior (sin alineación ni espacio entre líneas) sigue haciéndose, centrado
    document::GeneratorValues old = generator->defaults();
    old.erase("alineacion");
    old.erase("interlinea");
    old.texts["texto"] = "OO";
    REQUIRE_NOTHROW(generator->make(old, ""));
    document::setTextOutliner({});
}

TEST_CASE("El par de engranajes sale a la distancia correcta y con los dientes encajados", "[generator][gear]")
{
    const document::Generator* generator = document::findGenerator("par-engranajes");
    REQUIRE(generator != nullptr);
    const document::GeneratorValues values = generator->defaults();
    REQUIRE(generator->summary(values).find("distancia entre ejes 36.15 mm") != std::string::npos);

    Document doc;
    document::applyRecipe(doc, generator->make(values, ""));
    REQUIRE(doc.result().bodies.size() == 2);
    for (std::size_t i = 0; i < doc.featureCount(); ++i) {
        INFO(doc.feature(i).name());
        REQUIRE(doc.status(doc.feature(i).id()).health != Health::Error);
    }
    REQUIRE(doc.bodyInfo().at(doc.result().bodies[1].key).name == "Engranaje 2");
    // Módulo 2, 12 y 24 dientes: distancia entre ejes 2·(12 + 24)/2 = 36 más 0,15 de juego.
    const geometry::Box second = doc.result().bodies[1].shape.bounds();
    REQUIRE_THAT(0.5 * (second.min.x + second.max.x), Catch::Matchers::WithinAbs(36.15, 0.05));
    REQUIRE_THAT(0.5 * (second.min.y + second.max.y), Catch::Matchers::WithinAbs(0.0, 0.05));

    // Engranan: no se tocan (con el juego) ni se solapan.
    const auto overlap = [](Document& d) {
        return geometry::boolean(geometry::BooleanOp::Common, d.result().bodies[0].shape, d.result().bodies[1].shape)
            .volume();
    };
    REQUIRE(overlap(doc) < 0.5);

    // Sin el giro de la segunda rueda, un diente da contra otro: los cuerpos se solapan.
    auto params = doc.parameters();
    for (document::Parameter& p : params) {
        if (p.name == "giro_rueda_2") {
            p.expression = "0";
        }
    }
    doc.setParameters(params);
    REQUIRE(overlap(doc) > 5.0);
}

TEST_CASE("El par de engranajes helicoidales lleva la hélice en sentido contrario", "[generator][gear]")
{
    const document::Generator* generator = document::findGenerator("par-engranajes");
    document::GeneratorValues values = generator->defaults();
    values["helice"] = 15.0;
    Document doc;
    document::applyRecipe(doc, generator->make(values, ""));
    REQUIRE(doc.result().bodies.size() == 2);
    const auto* first = static_cast<const GearFeature*>(doc.findFeature(doc.feature(0).id()));
    const auto* second = static_cast<const GearFeature*>(doc.findFeature(doc.feature(1).id()));
    // Las dos ruedas usan la misma hélice (parámetro `helice`), la segunda con el signo contrario.
    REQUIRE(first->expressions.at("helixAngle") == "helice");
    REQUIRE(second->expressions.at("helixAngle") == "-helice");
    REQUIRE_THAT(doc.result().bodies[0].shape.volume() / doc.result().bodies[1].shape.volume(),
                 Catch::Matchers::WithinAbs(0.24, 0.03));  // 12 dientes frente a 24: un cuarto de la superficie
    for (std::size_t i = 0; i < doc.featureCount(); ++i) {
        REQUIRE(doc.status(doc.feature(i).id()).health != Health::Error);
    }
}

TEST_CASE("Las expresiones de ángulos están en grados", "[params]")
{
    Document doc;
    doc.setParameters({{"presion", "25"}});
    auto gear = std::make_unique<GearFeature>();
    gear->base.origin = OriginPlane::XY;
    gear->expressions["pressureAngle"] = "presion";
    gear->expressions["helixAngle"] = "10 + 5";
    const FeatureId id = doc.addFeature(std::move(gear));
    REQUIRE(doc.status(id).health == Health::Ok);
    // El valor guardado sigue siendo el de la operación (en radianes); lo calculado usó 25° y 15°.
    const auto bound = doc.findFeature(id)->clone();
    for (const Feature::NumericField& field : bound->numericFields()) {
        if (field.key == "pressureAngle") {
            field.set(25.0);
            REQUIRE_THAT(field.get(), Catch::Matchers::WithinAbs(25.0, 1e-9));
        }
    }
    REQUIRE(doc.result().bodies.size() == 1);
}

TEST_CASE("Las respuestas de un generador se guardan y se reabren", "[generator]")
{
    document::GeneratorValues values;
    values["a"] = 1.5;
    values["b"] = -7.0;
    values.texts["t"] = "Hola\tmundo\n\\fin";
    const document::GeneratorValues back = document::decodeValues(document::encodeValues(values));
    REQUIRE(back.at("a") == 1.5);
    REQUIRE(back.at("b") == -7.0);
    REQUIRE(back.texts.at("t") == "Hola\tmundo\n\\fin");
}

TEST_CASE("Reabrir un generador cambia sus operaciones en su sitio y se puede deshacer", "[generator]")
{
    const document::Generator* lightbox = document::findGenerator("lightbox");
    REQUIRE(lightbox != nullptr);
    document::GeneratorValues values = lightbox->defaults();
    values["cable"] = 0.0;
    const auto make = [&](const document::GeneratorValues& v) {
        document::Recipe recipe = lightbox->make(v, "");
        recipe.origin = {"lightbox", "", document::encodeValues(v), 0};
        return recipe;
    };

    Document doc;
    commands::AddRecipe add(make(values), "Lightbox");
    add.execute(doc);
    const std::size_t count = doc.featureCount();
    const FeatureId lead = doc.feature(0).origin.lead;
    REQUIRE(lead != 0);
    REQUIRE(doc.feature(0).id() == lead);
    for (std::size_t i = 0; i < count; ++i) {
        REQUIRE(doc.feature(i).origin.lead == lead);
        REQUIRE(doc.feature(i).origin.generator == "lightbox");
    }
    REQUIRE(document::decodeValues(doc.feature(0).origin.values).at("cable") == 0.0);
    REQUIRE(doc.feature(1).origin.values.empty());
    REQUIRE(document::generatedGroup(doc, lead).size() == count);

    // Otra medida: mismas operaciones, así que se sustituyen una a una y conservan su id.
    std::vector<FeatureId> ids;
    for (std::size_t i = 0; i < count; ++i) {
        ids.push_back(doc.feature(i).id());
    }
    const auto oldVolume = doc.result().bodies[0].shape.volume();
    values["pared"] = 4.0;
    {
        const document::Recipe recipe = make(values);
        REQUIRE(commands::ReplaceRecipe::problem(doc, lead, recipe).empty());
    }
    commands::ReplaceRecipe inPlace(lead, make(values), "Lightbox");
    inPlace.execute(doc);
    REQUIRE(doc.featureCount() == count);
    for (std::size_t i = 0; i < count; ++i) {
        REQUIRE(doc.feature(i).id() == ids[i]);
        REQUIRE(doc.status(ids[i]).health != Health::Error);
    }
    REQUIRE(doc.result().bodies.size() == 2);
    REQUIRE(doc.result().bodies[0].shape.volume() != Catch::Approx(oldVolume));
    REQUIRE(doc.bodyInfo().at(doc.result().bodies[0].key).name == "Caja");
    REQUIRE(document::decodeValues(doc.feature(0).origin.values).at("pared") == 4.0);
    for (const document::Parameter& p : doc.parameters()) {
        if (p.name == "pared") {
            REQUIRE(p.expression == "4");
        }
    }
    inPlace.undo(doc);
    REQUIRE(doc.result().bodies[0].shape.volume() == Catch::Approx(oldVolume));
    REQUIRE(document::decodeValues(doc.feature(0).origin.values).at("pared") == 2.0);
    inPlace.execute(doc);
    REQUIRE(doc.result().bodies[0].shape.volume() != Catch::Approx(oldVolume));

    // Con agujero para el cable hay más operaciones: se rehace el grupo entero (nada de fuera depende de él).
    values["cable"] = 6.0;
    commands::ReplaceRecipe rebuild(lead, make(values), "Lightbox");
    REQUIRE(commands::ReplaceRecipe::problem(doc, lead, make(values)).empty());
    rebuild.execute(doc);
    REQUIRE(doc.featureCount() > count);
    const FeatureId newLead = doc.feature(0).origin.lead;
    REQUIRE(document::generatedGroup(doc, newLead).size() == doc.featureCount());
    REQUIRE(doc.result().bodies.size() == 2);
    for (std::size_t i = 0; i < doc.featureCount(); ++i) {
        INFO(doc.feature(i).name());
        REQUIRE(doc.status(doc.feature(i).id()).health != Health::Error);
    }
    REQUIRE(doc.bodyInfo().at(doc.result().bodies[0].key).name == "Caja");
    REQUIRE(doc.bodyInfo().at(doc.result().bodies[0].key).component == "Lightbox 1");
    const std::size_t grown = doc.featureCount();

    // Se guarda y se vuelve a abrir con lo necesario para reabrirlo.
    const auto path = std::filesystem::temp_directory_path() / "nuf_generador.nuf";
    std::filesystem::remove(path);
    io::saveDocument(path, doc, {});
    Document reopened;
    io::ViewState view;
    io::loadDocument(path, reopened, view);
    std::filesystem::remove(path);
    REQUIRE(reopened.feature(0).origin.generator == "lightbox");
    REQUIRE(reopened.feature(0).origin.lead == newLead);
    REQUIRE(document::decodeValues(reopened.feature(0).origin.values).at("cable") == 6.0);

    // Deshacer y rehacer devuelven lo mismo, con los mismos ids.
    std::vector<FeatureId> grownIds;
    for (std::size_t i = 0; i < grown; ++i) {
        grownIds.push_back(doc.feature(i).id());
    }
    rebuild.undo(doc);
    REQUIRE(doc.featureCount() == count);
    REQUIRE(doc.result().bodies.size() == 2);
    for (std::size_t i = 0; i < count; ++i) {
        REQUIRE(doc.feature(i).id() == ids[i]);
    }
    rebuild.execute(doc);
    REQUIRE(doc.featureCount() == grown);
    for (std::size_t i = 0; i < grown; ++i) {
        REQUIRE(doc.feature(i).id() == grownIds[i]);
    }

    // Si algo de fuera depende de lo generado, no se puede cambiar su estructura (sí las medidas).
    auto pattern = std::make_unique<document::PatternFeature>();
    pattern->body = doc.result().bodies[0].key;
    pattern->count = 2;
    pattern->spacing = 300.0;
    doc.addFeature(std::move(pattern));
    values["cable"] = 0.0;
    REQUIRE_FALSE(commands::ReplaceRecipe::problem(doc, newLead, make(values)).empty());
    values["cable"] = 6.0;
    values["pared"] = 3.0;
    REQUIRE(commands::ReplaceRecipe::problem(doc, newLead, make(values)).empty());
    REQUIRE_FALSE(commands::ReplaceRecipe::problem(doc, 99999, make(values)).empty());
}

namespace {

double commonVolume(Document& doc, std::size_t a, std::size_t b)
{
    return geometry::boolean(geometry::BooleanOp::Common, doc.result().bodies[a].shape, doc.result().bodies[b].shape).volume();
}

} // namespace

TEST_CASE("Un engranaje interno tiene los dientes hacia dentro y un aro por fuera", "[gear][internal]")
{
    Document doc;
    auto g = std::make_unique<GearFeature>();
    g->base.origin = OriginPlane::XY;
    g->spec.type = geometry::GearType::Internal;
    g->spec.module = 2.0;
    g->spec.teeth = 36;
    g->spec.rim = 6.0;
    g->spec.thickness = 8.0;
    const FeatureId id = doc.addFeature(std::move(g));
    REQUIRE(doc.status(id).health == Health::Ok);
    REQUIRE(doc.result().bodies.size() == 1);
    const geometry::Shape& ring = doc.result().bodies.front().shape;
    REQUIRE(ring.isValid());
    // Ø primitivo 72: el hueco llega a rp + 1,25·m = 38,5 y el aro termina 6 mm más afuera.
    REQUIRE_THAT(ring.bounds().max.x, Catch::Matchers::WithinAbs(44.5, 1e-3));
    REQUIRE_THAT(ring.bounds().min.x, Catch::Matchers::WithinAbs(-44.5, 1e-3));
    REQUIRE_THAT(ring.bounds().max.z, Catch::Matchers::WithinAbs(8.0, 1e-6));
    // Disco exterior menos un hueco de tamaño parecido al del círculo primitivo.
    const double outer = kPi * 44.5 * 44.5 * 8.0;
    const double hollow = kPi * 36.0 * 36.0 * 8.0;
    REQUIRE(ring.volume() > outer - 1.1 * hollow);
    REQUIRE(ring.volume() < outer - 0.9 * hollow);
    bool flank = false, outside = false;
    for (int i = 0; i < ring.faceCount(); ++i) {
        flank = flank || ring.faceName(i).find(":lado:d1:flanco_d") != std::string::npos;
        outside = outside || ring.faceName(i).find(":lado:exterior") != std::string::npos;
    }
    REQUIRE(flank);
    REQUIRE(outside);

    // Con hélice sí se puede (E1, corona helicoidal); con más de 45° no: error claro.
    Document bad;
    auto helical = std::make_unique<GearFeature>();
    helical->base.origin = OriginPlane::XY;
    helical->spec.type = geometry::GearType::Internal;
    helical->spec.teeth = 36;
    helical->spec.helixAngle = 0.9;
    REQUIRE(bad.status(bad.addFeature(std::move(helical))).health == Health::Error);
}

TEST_CASE("El engranaje interno sale a la distancia correcta y el piñón engrana por dentro", "[generator][gear][internal]")
{
    const document::Generator* generator = document::findGenerator("engranaje-interno");
    REQUIRE(generator != nullptr);
    const document::GeneratorValues values = generator->defaults();
    REQUIRE(generator->summary(values).find("distancia entre ejes 23.85 mm") != std::string::npos);
    REQUIRE(generator->summary(values).find("AVISO") == std::string::npos);
    document::GeneratorValues close = values;
    close["dientes_pinon"] = 30.0;
    REQUIRE(generator->summary(close).find("AVISO") != std::string::npos);

    Document doc;
    document::applyRecipe(doc, generator->make(values, ""));
    REQUIRE(doc.result().bodies.size() == 2);
    for (std::size_t i = 0; i < doc.featureCount(); ++i) {
        INFO(doc.feature(i).name());
        REQUIRE(doc.status(doc.feature(i).id()).health != Health::Error);
    }
    REQUIRE(doc.bodyInfo().at(doc.result().bodies[0].key).name == "Corona");
    REQUIRE(doc.bodyInfo().at(doc.result().bodies[1].key).name == "Piñón");
    // Módulo 2, 36 y 12 dientes: 2·(36 − 12)/2 = 24 menos 0,15 de juego.
    const geometry::Box pinion = doc.result().bodies[1].shape.bounds();
    REQUIRE_THAT(0.5 * (pinion.min.x + pinion.max.x), Catch::Matchers::WithinAbs(23.85, 0.05));
    REQUIRE_THAT(0.5 * (pinion.min.y + pinion.max.y), Catch::Matchers::WithinAbs(0.0, 0.05));
    // El piñón no toca ni se solapa con la corona.
    REQUIRE(commonVolume(doc, 0, 1) < 0.5);
    REQUIRE(doc.result().bodies[0].shape.bounds().max.x > pinion.max.x);

    // El piñón gira medio paso: un diente cae sobre un diente de la corona y se solapan.
    auto bent = std::unique_ptr<Feature>(doc.findFeature(doc.feature(1).id())->clone());
    bent->expressions["rotation"] = "180/dientes_pinon";
    doc.replaceFeature(doc.feature(1).id(), std::move(bent));
    REQUIRE(commonVolume(doc, 0, 1) > 5.0);
}

TEST_CASE("Una cremallera tiene el largo, la base y los dientes de su perfil", "[gear][rack]")
{
    Document doc;
    auto g = std::make_unique<GearFeature>();
    g->base.origin = OriginPlane::XY;
    g->spec.type = geometry::GearType::Rack;
    g->spec.module = 2.0;
    g->spec.teeth = 10;
    g->spec.rim = 5.0;
    g->spec.thickness = 8.0;
    const FeatureId id = doc.addFeature(std::move(g));
    REQUIRE(doc.status(id).health == Health::Ok);
    const geometry::Shape& rack = doc.result().bodies.front().shape;
    REQUIRE(rack.isValid());
    const double p = kPi * 2.0;
    const geometry::Box box = rack.bounds();
    REQUIRE_THAT(box.min.x, Catch::Matchers::WithinAbs(0.0, 1e-6));
    REQUIRE_THAT(box.max.x, Catch::Matchers::WithinAbs(10.0 * p, 1e-6));
    REQUIRE_THAT(box.max.y, Catch::Matchers::WithinAbs(2.0, 1e-6));           // cabeza: +1 módulo
    REQUIRE_THAT(box.min.y, Catch::Matchers::WithinAbs(-(2.5 + 5.0), 1e-6));  // raíz −1,25 módulo y la base
    // Área: la base (5 mm de alto bajo la raíz) más diez trapecios de 2,25·m de alto, de ancho p/2 + 0,25·m·tan(presión)
    // de media (el ancho en la línea primitiva es p/2 y el punto medio de la altura queda 0,125·m bajo ella).
    const double tooth = 2.25 * 2.0 * (p / 2.0 + 0.25 * 2.0 * std::tan(20.0 * kPi / 180.0));
    const double expected = (10.0 * p * 5.0 + 10.0 * tooth) * 8.0;
    REQUIRE_THAT(rack.volume(), WithinRel(expected, 1e-3));

    // Con más ángulo del que permite el perfil, avisa.
    Document bad;
    auto steep = std::make_unique<GearFeature>();
    steep->base.origin = OriginPlane::XY;
    steep->spec.type = geometry::GearType::Rack;
    steep->spec.pressureAngle = 35.0 * kPi / 180.0;
    REQUIRE(bad.status(bad.addFeature(std::move(steep))).health == Health::Error);
}

TEST_CASE("La cremallera engrana con su rueda: distancia, hueco bajo el diente y sin solape", "[generator][gear][rack]")
{
    const document::Generator* generator = document::findGenerator("cremallera");
    REQUIRE(generator != nullptr);
    for (const double z : {20.0, 21.0}) {
        document::GeneratorValues values = generator->defaults();
        values["dientes_rueda"] = z;
        Document doc;
        document::applyRecipe(doc, generator->make(values, ""));
        REQUIRE(doc.result().bodies.size() == 2);
        for (std::size_t i = 0; i < doc.featureCount(); ++i) {
            INFO(doc.feature(i).name());
            REQUIRE(doc.status(doc.feature(i).id()).health != Health::Error);
        }
        REQUIRE(doc.bodyInfo().at(doc.result().bodies[1].key).name == "Cremallera");
        // La línea primitiva queda a rp + juego bajo el eje: la cabeza de la barra a m − (rp + juego).
        const double rp = 2.0 * z / 2.0;
        const geometry::Box rack = doc.result().bodies[1].shape.bounds();
        REQUIRE_THAT(rack.max.y, Catch::Matchers::WithinAbs(2.0 - rp - 0.15, 1e-3));
        REQUIRE(rack.min.x < 0.0);
        REQUIRE(rack.max.x > 0.0);
        REQUIRE(commonVolume(doc, 0, 1) < 0.5);
        // Sin el giro, los dientes de la rueda dan contra los de la barra (con 21 dientes el giro vale 12,86°, no 0).
        if (z == 21.0) {
            auto params = doc.parameters();
            for (document::Parameter& p : params) {
                if (p.name == "giro_rueda") {
                    p.expression = "0";
                }
            }
            doc.setParameters(params);
            REQUIRE(commonVolume(doc, 0, 1) > 2.0);
        }
    }
}

// El largo de la cremallera: «length» corta la barra a esa medida (el último diente queda cortado) sin cambiar el resto.
TEST_CASE("Una cremallera se puede cortar a un largo exacto", "[gear][rack]")
{
    const double p = kPi * 2.0;
    const auto make = [&](double length, int teeth) {
        Document doc;
        auto g = std::make_unique<GearFeature>();
        g->base.origin = OriginPlane::XY;
        g->spec.type = geometry::GearType::Rack;
        g->spec.module = 2.0;
        g->spec.teeth = teeth;
        g->spec.rim = 5.0;
        g->spec.thickness = 8.0;
        g->spec.length = length;
        const FeatureId id = doc.addFeature(std::move(g));
        return std::make_pair(doc.status(id), doc.result().bodies.empty() ? geometry::Shape() : doc.result().bodies.front().shape);
    };
    const auto [fullStatus, full] = make(0.0, 10);
    REQUIRE(fullStatus.health == Health::Ok);
    // Largos que caen en un diente, en un hueco y justo en un paso: siempre mide lo pedido y es un sólido válido.
    for (const double length : {5.5 * p + 1.0, 6.0 * p, 7.5 * p, 3.2 * p, 9.0 * p + 0.01}) {
        INFO("largo " << length);
        const auto [status, rack] = make(length, 10);
        REQUIRE(status.health == Health::Ok);
        REQUIRE_FALSE(rack.isNull());
        REQUIRE(rack.isValid());
        const geometry::Box box = rack.bounds();
        REQUIRE_THAT(box.min.x, Catch::Matchers::WithinAbs(0.0, 1e-6));
        REQUIRE_THAT(box.max.x, Catch::Matchers::WithinAbs(length, 1e-6));
        REQUIRE(rack.volume() < full.volume());
        REQUIRE(rack.volume() > 0.0);
    }
    // El largo completo es la barra de siempre, y pasarse de largo o quedarse en un paso es un error claro.
    const auto [sameStatus, same] = make(10.0 * p, 10);
    REQUIRE(sameStatus.health == Health::Ok);
    REQUIRE_THAT(same.volume(), WithinRel(full.volume(), 1e-9));
    const auto [longStatus, tooLong] = make(10.0 * p + 1.0, 10);
    REQUIRE(longStatus.health == Health::Error);
    REQUIRE(longStatus.message.find("no puede pasar") != std::string::npos);
    const auto [shortStatus, tooShort] = make(p * 0.9, 10);
    REQUIRE(shortStatus.health == Health::Error);
    REQUIRE(shortStatus.message.find("mayor que un paso") != std::string::npos);
}

TEST_CASE("El generador de cremallera da el largo por dientes, exacto o con dientes completos", "[generator][gear][rack]")
{
    const document::Generator* generator = document::findGenerator("cremallera");
    REQUIRE(generator != nullptr);
    const double p = kPi * 2.0;
    const auto rackBox = [&](document::GeneratorValues values) {
        Document doc;
        document::applyRecipe(doc, generator->make(values, ""));
        REQUIRE(doc.result().bodies.size() == 2);
        for (std::size_t i = 0; i < doc.featureCount(); ++i) {
            INFO(doc.feature(i).name());
            REQUIRE(doc.status(doc.feature(i).id()).health != Health::Error);
        }
        return doc.result().bodies[1].shape.bounds();
    };
    // Por dientes (lo de siempre): 10 dientes miden 10·p.
    document::GeneratorValues byTeeth = generator->defaults();
    {
        const geometry::Box box = rackBox(byTeeth);
        REQUIRE_THAT(box.max.x - box.min.x, Catch::Matchers::WithinAbs(10.0 * p, 1e-3));
    }
    // Largo exacto: mide lo pedido aunque no sea un número entero de dientes.
    document::GeneratorValues exact = generator->defaults();
    exact["largo_modo"] = 1.0;
    for (const double length : {100.0, 77.7, 40.0}) {
        exact["largo_exacto"] = length;
        const geometry::Box box = rackBox(exact);
        INFO("largo exacto " << length);
        REQUIRE_THAT(box.max.x - box.min.x, Catch::Matchers::WithinAbs(length, 1e-3));
    }
    // Largo con dientes completos: se ajusta al número entero de dientes más cercano.
    document::GeneratorValues rounded = generator->defaults();
    rounded["largo_modo"] = 2.0;
    rounded["largo_aprox"] = 100.0;  // 100 / 6,283 = 15,9 → 16 dientes
    {
        const geometry::Box box = rackBox(rounded);
        REQUIRE_THAT(box.max.x - box.min.x, Catch::Matchers::WithinAbs(16.0 * p, 1e-3));
    }
    // Un diseño de antes (sin el modo de largo entre sus respuestas) sigue saliendo por dientes.
    document::GeneratorValues old = generator->defaults();
    old.erase("largo_modo");
    old.erase("largo_exacto");
    old.erase("largo_aprox");
    {
        const geometry::Box box = rackBox(old);
        REQUIRE_THAT(box.max.x - box.min.x, Catch::Matchers::WithinAbs(10.0 * p, 1e-3));
    }
    // El resumen del panel dice el largo y los dientes de cada modo.
    REQUIRE(generator->summary(exact).find("40.0 mm") != std::string::npos);
    REQUIRE(generator->summary(rounded).find("16 dientes") != std::string::npos);
}

TEST_CASE("El largo de la cremallera se guarda en el archivo y se reabre igual", "[gear][io][rack]")
{
    const document::Generator* generator = document::findGenerator("cremallera");
    document::GeneratorValues values = generator->defaults();
    values["largo_modo"] = 1.0;
    values["largo_exacto"] = 83.3;
    Document doc;
    document::applyRecipe(doc, generator->make(values, ""));
    const auto path = std::filesystem::temp_directory_path() / "nuf_cremallera_largo.nuf";
    std::filesystem::remove(path);
    io::saveDocument(path, doc, {});
    Document reopened;
    io::ViewState view;
    io::loadDocument(path, reopened, view);
    std::filesystem::remove(path);
    const auto* rack = static_cast<const GearFeature*>(reopened.findFeature(doc.feature(1).id()));
    REQUIRE(rack->expressions.at("length") == "largo");  // el largo vive en la expresión, ligada al parámetro
    const geometry::Box box = reopened.result().bodies[1].shape.bounds();
    REQUIRE_THAT(box.max.x - box.min.x, Catch::Matchers::WithinAbs(83.3, 1e-3));
    // Cambiar el largo en la tabla de parámetros rehace la barra (queda ligada).
    auto params = reopened.parameters();
    for (document::Parameter& param : params) {
        if (param.name == "largo") {
            param.expression = "50";
        }
    }
    reopened.setParameters(params);
    const geometry::Box shorter = reopened.result().bodies[1].shape.bounds();
    REQUIRE_THAT(shorter.max.x - shorter.min.x, Catch::Matchers::WithinAbs(50.0, 1e-3));
}

// La pestaña y el menú Herramientas agrupan los generadores por familia: cada uno declara la suya y las familias van
// seguidas (no se mezclan en la lista).
TEST_CASE("Cada generador tiene su familia y las familias no se mezclan", "[generator]")
{
    std::vector<std::string> seen;
    for (const document::Generator* generator : document::generators()) {
        const std::string category = generator->category();
        INFO(generator->id());
        REQUIRE_FALSE(category.empty());
        REQUIRE(category != "Otros");
        if (seen.empty() || seen.back() != category) {
            REQUIRE(std::ranges::find(seen, category) == seen.end());  // una familia no vuelve a aparecer después de otra
            seen.push_back(category);
        }
    }
    REQUIRE(seen == std::vector<std::string>{"Cajas", "Letreros", "Engranajes"});
    REQUIRE(document::findGenerator("lightbox")->category() == document::findGenerator("caja-con-tapa")->category());
    for (const char* id : {"par-engranajes", "engranaje-interno", "cremallera"}) {
        REQUIRE(document::findGenerator(id) != nullptr);
        REQUIRE(document::findGenerator(id)->category() == "Engranajes");
    }
}

TEST_CASE("El tipo y el aro del engranaje se guardan y la rueda sigue siendo exterior", "[gear][io]")
{
    const document::Generator* generator = document::findGenerator("cremallera");
    Document doc;
    document::applyRecipe(doc, generator->make(generator->defaults(), ""));
    const auto path = std::filesystem::temp_directory_path() / "nuf_cremallera.nuf";
    std::filesystem::remove(path);
    io::saveDocument(path, doc, {});
    Document reopened;
    io::ViewState view;
    io::loadDocument(path, reopened, view);
    std::filesystem::remove(path);
    const auto* rack = static_cast<const GearFeature*>(reopened.findFeature(doc.feature(1).id()));
    REQUIRE(rack->spec.type == geometry::GearType::Rack);
    REQUIRE_THAT(rack->spec.rim, Catch::Matchers::WithinAbs(5.0, 1e-9));
    REQUIRE(static_cast<const GearFeature*>(reopened.findFeature(doc.feature(0).id()))->spec.type == geometry::GearType::External);
    REQUIRE_THAT(reopened.result().bodies[1].shape.volume(), WithinRel(doc.result().bodies[1].shape.volume(), 1e-9));
}

TEST_CASE("El espejo refleja varios cuerpos a la vez y se guarda", "[mirror][io]")
{
    Document doc = boxDocument();
    addMove(doc, {0, 40, 0}, 0.0, true);  // un segundo cuerpo (f3), copia del primero
    REQUIRE(doc.result().bodies.size() == 2);
    auto m = std::make_unique<MirrorFeature>();
    m->body = "f2";
    m->alsoBodies = {"f3"};
    m->plane.origin = OriginPlane::YZ;
    m->operation = Operation::NewBody;
    const FeatureId id = doc.addFeature(std::move(m));
    REQUIRE(doc.status(id).health == Health::Ok);
    REQUIRE(doc.result().bodies.size() == 4);
    for (std::size_t i = 2; i < 4; ++i) {
        REQUIRE_THAT(doc.result().bodies[i].shape.volume(), WithinRel(kBox, 1e-6));
        REQUIRE_THAT(doc.result().bodies[i].shape.bounds().min.x, Catch::Matchers::WithinAbs(-20.0, 1e-6));
    }
    // Una copia está 40 mm más allá en Y que la otra
    REQUIRE_THAT(std::abs(doc.result().bodies[3].shape.bounds().min.y - doc.result().bodies[2].shape.bounds().min.y),
                 Catch::Matchers::WithinAbs(40.0, 1e-6));
    // Con un cuerpo que ya no existe en la lista avisa y refleja los demás
    auto lost = std::make_unique<MirrorFeature>();
    lost->body = "f2";
    lost->alsoBodies = {"f99"};
    lost->plane.origin = OriginPlane::XY;
    lost->operation = Operation::NewBody;
    const FeatureId warned = doc.addFeature(std::move(lost));
    REQUIRE(doc.status(warned).health == Health::Warning);

    const auto path = std::filesystem::temp_directory_path() / "nuf_espejo_varios.nuf";
    std::filesystem::remove(path);
    io::saveDocument(path, doc, {});
    Document reopened;
    io::ViewState view;
    io::loadDocument(path, reopened, view);
    std::filesystem::remove(path);
    const auto* again = dynamic_cast<const MirrorFeature*>(reopened.findFeature(id));
    REQUIRE(again != nullptr);
    REQUIRE(again->alsoBodies == std::vector<std::string>{"f3"});
    REQUIRE(reopened.result().bodies.size() == doc.result().bodies.size());
}

TEST_CASE("Dividir un cuerpo con un plano da dos cuerpos que suman el volumen, y se guarda", "[split][io]")
{
    Document doc = boxDocument();  // una caja de 20 × 10 × 10 (kBox)
    const double whole = doc.result().bodies.front().shape.volume();
    auto split = std::make_unique<SplitFeature>();
    split->body = "f2";
    split->plane.origin = OriginPlane::YZ;  // x = 0 + offset
    split->offset = 5.0;
    const FeatureId id = doc.addFeature(std::move(split));
    REQUIRE(doc.status(id).health == Health::Ok);
    REQUIRE(doc.result().bodies.size() == 2);
    const double a = doc.result().bodies[0].shape.volume();
    const double b = doc.result().bodies[1].shape.volume();
    REQUIRE_THAT(a + b, WithinRel(whole, 1e-6));
    // Cortado a x = 5: una parte mide 5 de ancho y la otra el resto (la caja va de 0 a 20 en X)
    REQUIRE_THAT(std::min(a, b), WithinRel(whole * 5.0 / 20.0, 1e-6));
    REQUIRE(doc.result().bodies[1].name.rfind("Parte", 0) == 0);
    // Un plano que no atraviesa el cuerpo da error claro
    auto outside = std::make_unique<SplitFeature>();
    outside->body = "f2";
    outside->plane.origin = OriginPlane::YZ;
    outside->offset = 500.0;
    REQUIRE(doc.status(doc.addFeature(std::move(outside))).health == Health::Error);

    const auto path = std::filesystem::temp_directory_path() / "nuf_dividir.nuf";
    std::filesystem::remove(path);
    io::saveDocument(path, doc, {});
    Document reopened;
    io::ViewState view;
    io::loadDocument(path, reopened, view);
    std::filesystem::remove(path);
    const auto* again = dynamic_cast<const SplitFeature*>(reopened.findFeature(id));
    REQUIRE(again != nullptr);
    REQUIRE_THAT(again->offset, Catch::Matchers::WithinAbs(5.0, 1e-9));
    REQUIRE(reopened.result().bodies.size() == 2);
}

TEST_CASE("Un barrido con escala al final se afina: un tronco de pirámide por la sección y el largo", "[sweep]")
{
    Document doc;
    const FeatureId id = addSweepDocument(doc, {{{0, 0}, {0, 30}}});
    auto changed = std::unique_ptr<Feature>(doc.findFeature(id)->clone());
    static_cast<SweepFeature&>(*changed).spec.endScale = 0.5;
    doc.replaceFeature(id, std::move(changed));
    REQUIRE(doc.status(id).health == Health::Ok);
    const geometry::Shape& bar = doc.result().bodies.front().shape;
    REQUIRE(bar.isValid());
    // Lado de 4 al principio y de 2 al final (escala lineal): V = h·(A + √(A·B) + B)/3 con A = 16 y B = 4
    REQUIRE_THAT(bar.volume(), WithinRel(30.0 * (16.0 + std::sqrt(16.0 * 4.0) + 4.0) / 3.0, 0.02));
    // Una escala imposible da error claro
    auto bad = std::unique_ptr<Feature>(doc.findFeature(id)->clone());
    static_cast<SweepFeature&>(*bad).spec.endScale = 0.0;
    doc.replaceFeature(id, std::move(bad));
    REQUIRE(doc.status(id).health == Health::Error);
}

TEST_CASE("Un patrón repite varios cuerpos a la vez, cada uno con sus copias", "[pattern][io]")
{
    Document doc = boxDocument();
    addMove(doc, {0, 40, 0}, 0.0, true);  // un segundo cuerpo (f3)
    REQUIRE(doc.result().bodies.size() == 2);
    auto p = std::make_unique<PatternFeature>();
    p->body = "f2";
    p->alsoBodies = {"f3"};
    p->layout = PatternLayout::Linear;
    p->axis = 0;
    p->count = 3;
    p->spacing = 30.0;
    p->operation = Operation::Join;
    const FeatureId id = doc.addFeature(std::move(p));
    REQUIRE(doc.status(id).health == Health::Ok);
    REQUIRE(doc.result().bodies.size() == 2);  // cada uno se queda con sus tres piezas, unidas
    for (const auto& b : doc.result().bodies) {
        REQUIRE_THAT(b.shape.volume(), WithinRel(3.0 * kBox, 1e-6));
    }
    // Con «Cuerpo nuevo», un cuerpo nuevo por cada uno
    Document fresh = boxDocument();
    addMove(fresh, {0, 40, 0}, 0.0, true);
    auto q = std::make_unique<PatternFeature>();
    q->body = "f2";
    q->alsoBodies = {"f3"};
    q->count = 2;
    q->spacing = 30.0;
    q->operation = Operation::NewBody;
    REQUIRE(fresh.status(fresh.addFeature(std::move(q))).health == Health::Ok);
    REQUIRE(fresh.result().bodies.size() == 4);

    const auto path = std::filesystem::temp_directory_path() / "nuf_patron_varios.nuf";
    std::filesystem::remove(path);
    io::saveDocument(path, doc, {});
    Document reopened;
    io::ViewState view;
    io::loadDocument(path, reopened, view);
    std::filesystem::remove(path);
    const auto* again = dynamic_cast<const PatternFeature*>(reopened.findFeature(id));
    REQUIRE(again != nullptr);
    REQUIRE(again->alsoBodies == std::vector<std::string>{"f3"});
}
