#include <nuf/commands/CommandStack.hpp>
#include <nuf/commands/FeatureCommands.hpp>
#include <nuf/document/Document.hpp>
#include <nuf/document/Features.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <numbers>

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

// Sobre una cara del resultado actual (como lo hace la interfaz: guarda
// también su plano, por si la cara desaparece).
std::unique_ptr<SketchFeature> sketchOnFace(const Document& doc, sketch::Sketch s, geometry::ElementRef face)
{
    SketchAttachment at;
    at.origin.reset();
    const auto hit = doc.result().findFace(face);
    at.lastPlane = *doc.result().bodies[hit->body].shape.facePlane(hit->face);
    at.face = std::move(face);
    return std::make_unique<SketchFeature>(std::move(s), at);
}

// Extruye todos los perfiles del sketch.
std::unique_ptr<ExtrudeFeature> extrudeAll(const Document& doc, FeatureId sketchId, double distance,
                                           Operation op = Operation::Join)
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

double totalVolume(const ModelState& state)
{
    double v = 0.0;
    for (const Body& b : state.bodies) {
        v += b.shape.volume();
    }
    return v;
}

// Referencia a la tapa superior que creó la extrusión «id».
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

} // namespace

TEST_CASE("Documento: sketch + extrusión crean un cuerpo", "[document][model]")
{
    Document doc;
    const FeatureId sk = doc.addFeature(sketchOn(rectangleSketch(0, 0, 20, 10)));
    const FeatureId ex = doc.addFeature(extrudeAll(doc, sk, 5));
    REQUIRE(doc.featureCount() == 2);
    REQUIRE(doc.findFeature(sk)->name() == "Sketch1");
    REQUIRE(doc.findFeature(ex)->name() == "Extrusión1");
    REQUIRE(doc.status(ex).health == Health::Ok);
    REQUIRE(doc.result().bodies.size() == 1);
    REQUIRE(doc.result().bodies.front().name == "Extrusión 1");
    REQUIRE_THAT(totalVolume(doc.result()), WithinRel(1000.0, 1e-9));
    REQUIRE(doc.defaultName(FeatureKind::Extrude) == "Extrusión2");
}

TEST_CASE("Documento: un sketch sobre una cara la sigue al editar la extrusión", "[document][model]")
{
    Document doc;
    const FeatureId sk = doc.addFeature(sketchOn(rectangleSketch(0, 0, 20, 20)));
    const FeatureId ex = doc.addFeature(extrudeAll(doc, sk, 10));

    sketch::Sketch circle;
    circle.addCircle({10, 10}, 3);  // en la cara superior, x e y del sketch son las del modelo
    const FeatureId sk2 = doc.addFeature(sketchOnFace(doc, std::move(circle), topFace(doc, ex)));
    REQUIRE(doc.status(sk2).health == Health::Ok);
    REQUIRE_THAT(doc.result().sketches.at(sk2).plane.origin.z, WithinAbs(10.0, 1e-9));

    // Cortar 4 mm hacia dentro desde la cara.
    auto pocket = extrudeAll(doc, sk2, -4, Operation::Cut);
    const FeatureId cut = doc.addFeature(std::move(pocket));
    REQUIRE(doc.status(cut).health == Health::Ok);
    const double hole = std::numbers::pi * 9.0 * 4.0;
    REQUIRE_THAT(totalVolume(doc.result()), WithinRel(4000.0 - hole, 1e-6));

    // Editar la primera extrusión: todo se recalcula y el sketch sube.
    auto taller = extrudeAll(doc, sk, 25);
    doc.replaceFeature(ex, std::move(taller));
    REQUIRE(doc.status(sk2).health == Health::Ok);
    REQUIRE_THAT(doc.result().sketches.at(sk2).plane.origin.z, WithinAbs(25.0, 1e-9));
    REQUIRE_THAT(totalVolume(doc.result()), WithinRel(400.0 * 25.0 - hole, 1e-6));
}

TEST_CASE("Documento: extruir hasta una cara curva sigue a la pieza al editarla", "[document][model]")
{
    const auto cylinderSketch = [](double radius) {
        sketch::Sketch circle;
        circle.addCircle({0, 0}, radius);
        return circle;
    };
    // ∫ sqrt(r² - x²) dx en [-2, 2]: lo que mide, a lo largo de Y, un cuadrado de 4 mm que sale del eje.
    const auto expected = [](double r) { return 4.0 * (2.0 * std::sqrt(r * r - 4.0) + r * r * std::asin(2.0 / r)); };

    Document doc;
    const FeatureId base = doc.addFeature(sketchOn(cylinderSketch(10)));
    const FeatureId cylinder = doc.addFeature(extrudeAll(doc, base, 40));
    geometry::ElementRef wall;
    const Body& body = doc.result().bodies.front();
    const std::string prefix = "f" + std::to_string(cylinder) + ":lado:";
    for (int i = 0; i < body.shape.faceCount(); ++i) {
        if (body.shape.faceName(i).starts_with(prefix)) {
            wall = body.shape.faceRef(i);
        }
    }
    REQUIRE(!wall.name.empty());

    // Un cuadrado en XZ (sobre el eje) que crece hasta la pared del cilindro, como cuerpo nuevo.
    sketch::Sketch square = rectangleSketch(-2, -2, 2, 2);
    const FeatureId sk = doc.addFeature(sketchOn(std::move(square), OriginPlane::XZ));
    auto toWall = extrudeAll(doc, sk, 10, Operation::NewBody);
    toWall->spec.side1.extent = geometry::Extent::ToFace;
    toWall->spec.side1.face = wall;
    const FeatureId post = doc.addFeature(std::move(toWall));
    REQUIRE(doc.status(post).health == Health::Ok);
    REQUIRE(doc.result().bodies.size() == 2);
    REQUIRE_THAT(doc.result().bodies.back().shape.volume(), WithinRel(expected(10), 1e-3));
    // La extrusión depende de la cara de destino (si su operación se suprime, se avisa).
    REQUIRE_FALSE(doc.findFeature(post)->dependencies().empty());

    // Ensanchar el cilindro: la extrusión llega ahora a la pared nueva.
    doc.replaceFeature(base, sketchOn(cylinderSketch(15)));
    REQUIRE(doc.status(post).health == Health::Ok);
    REQUIRE_THAT(doc.result().bodies.back().shape.volume(), WithinRel(expected(15), 1e-3));
}

TEST_CASE("Documento: marcador, supresión y errores sin perder datos", "[document][model]")
{
    Document doc;
    const FeatureId sk = doc.addFeature(sketchOn(rectangleSketch(0, 0, 20, 20)));
    const FeatureId ex = doc.addFeature(extrudeAll(doc, sk, 10));
    sketch::Sketch circle;
    circle.addCircle({10, 10}, 3);
    const FeatureId sk2 = doc.addFeature(sketchOnFace(doc, std::move(circle), topFace(doc, ex)));
    const FeatureId cut = doc.addFeature(extrudeAll(doc, sk2, -4, Operation::Cut));

    // Marcador tras el primer sketch: nada sólido.
    doc.setMarker(1);
    REQUIRE(doc.result().bodies.empty());
    REQUIRE(doc.status(ex).health == Health::RolledBack);
    // Una operación nueva entra en el marcador.
    REQUIRE(doc.defaultName(FeatureKind::Sketch) == "Sketch3");
    doc.setMarker(4);
    REQUIRE(doc.result().bodies.size() == 1);

    // Suprimir la extrusión: el sketch de la cara se queda donde estaba y el
    // corte avisa de que no corta nada.
    doc.setSuppressed(ex, true);
    REQUIRE(doc.status(ex).health == Health::Suppressed);
    REQUIRE(doc.status(sk2).health == Health::Warning);
    REQUIRE_THAT(doc.result().sketches.at(sk2).plane.origin.z, WithinAbs(10.0, 1e-9));
    REQUIRE(doc.status(cut).health == Health::Warning);
    doc.setSuppressed(ex, false);
    REQUIRE(doc.status(cut).health == Health::Ok);

    // Una extrusión con un perfil que no existe falla, pero el modelo sigue.
    auto broken = std::make_unique<ExtrudeFeature>();
    broken->profiles.push_back({sk, {900, 901}});
    const FeatureId bad = doc.addFeature(std::move(broken));
    REQUIRE(doc.status(bad).health == Health::Error);
    REQUIRE_FALSE(doc.status(bad).message.empty());
    REQUIRE(doc.result().bodies.size() == 1);
}

TEST_CASE("Documento: unir, cuerpo nuevo e intersecar", "[document][model]")
{
    Document doc;
    const FeatureId a = doc.addFeature(sketchOn(rectangleSketch(0, 0, 10, 10)));
    doc.addFeature(extrudeAll(doc, a, 10));
    // Unir sin tocar nada: cuerpo nuevo.
    const FeatureId b = doc.addFeature(sketchOn(rectangleSketch(50, 0, 60, 10)));
    doc.addFeature(extrudeAll(doc, b, 10));
    REQUIRE(doc.result().bodies.size() == 2);
    REQUIRE(doc.result().bodies[1].name == "Extrusión 2");
    // Un puente que toca los dos los une en uno.
    const FeatureId c = doc.addFeature(sketchOn(rectangleSketch(5, 2, 55, 8)));
    doc.addFeature(extrudeAll(doc, c, 4));
    REQUIRE(doc.result().bodies.size() == 1);
    REQUIRE_THAT(totalVolume(doc.result()), WithinRel(1000.0 + 1000.0 + 40.0 * 6.0 * 4.0, 1e-6));
    // Intersecar con un bloque bajo deja solo lo común.
    const FeatureId d = doc.addFeature(sketchOn(rectangleSketch(-5, -5, 100, 100)));
    doc.addFeature(extrudeAll(doc, d, 2, Operation::Intersect));
    REQUIRE_THAT(totalVolume(doc.result()), WithinRel((100.0 + 100.0 + 40.0 * 6.0) * 2.0, 1e-6));
}

TEST_CASE("Documento: revolución alrededor de una línea del sketch", "[document][model]")
{
    Document doc;
    sketch::Sketch s = rectangleSketch(10, 0, 15, 4);
    const sketch::EntityId axisLine = s.addLine({0, -10}, {0, 10}, true);  // construcción: no forma perfiles
    const FeatureId sk = doc.addFeature(sketchOn(std::move(s), OriginPlane::XZ));
    auto rev = std::make_unique<RevolveFeature>();
    for (const sketch::Profile& p : sketch::findProfiles(static_cast<const SketchFeature*>(doc.findFeature(sk))->sketch())) {
        rev->profiles.push_back(profileRef(sk, p));
    }
    rev->axis = {AxisRef::Kind::SketchLine, sk, axisLine, {}};
    rev->spec.extent = geometry::RevolveExtent::Full;
    const FeatureId r = doc.addFeature(std::move(rev));
    REQUIRE(doc.status(r).health == Health::Ok);
    REQUIRE(doc.findFeature(r)->name() == "Revolución1");
    REQUIRE_THAT(totalVolume(doc.result()), WithinRel(std::numbers::pi * (225.0 - 100.0) * 4.0, 1e-6));
}

TEST_CASE("Documento: un perfil se reconoce tras editar el sketch", "[document][model]")
{
    sketch::Sketch s = rectangleSketch(0, 0, 20, 10);
    const auto profiles = sketch::findProfiles(s);
    const ProfileRef ref = profileRef(7, profiles.front());
    // Se agrega un círculo aparte: el rectángulo sigue siendo el mismo perfil.
    s.addCircle({50, 50}, 2);
    const auto after = sketch::findProfiles(s);
    REQUIRE(after.size() == 2);
    const auto found = resolveProfile(after, ref);
    REQUIRE(found);
    REQUIRE_THAT(found->area, WithinRel(200.0, 1e-9));
    // Un perfil que no comparte curvas no se confunde.
    REQUIRE_FALSE(resolveProfile(after, ProfileRef{7, {1000, 1001, 1002}}));
}

TEST_CASE("Documento: la vista previa no toca el documento", "[document][model]")
{
    Document doc;
    const FeatureId sk = doc.addFeature(sketchOn(rectangleSketch(0, 0, 20, 10)));
    auto ex = extrudeAll(doc, sk, 5);
    ex->setId(doc.reserveId());
    const Preview p = preview(doc, *ex, doc.marker());
    REQUIRE(p.status.health == Health::Ok);
    REQUIRE(p.state.bodies.size() == 1);
    REQUIRE(doc.result().bodies.empty());
    REQUIRE(doc.featureCount() == 1);
}

TEST_CASE("Comandos del historial: agregar, editar, borrar, suprimir y mover", "[document][commands]")
{
    Document doc;
    commands::CommandStack stack(doc);
    stack.push(std::make_unique<commands::AddFeature>(sketchOn(rectangleSketch(0, 0, 20, 10))));
    const FeatureId sk = doc.feature(0).id();
    auto add = std::make_unique<commands::AddFeature>(extrudeAll(doc, sk, 5));
    auto* addPtr = add.get();
    stack.push(std::move(add));
    const FeatureId ex = addPtr->id();
    REQUIRE(stack.undoLabel() == "Agregar Extrusión1");

    // Deshacer y rehacer conserva id y nombre.
    stack.undo();
    REQUIRE(doc.featureCount() == 1);
    REQUIRE(doc.result().bodies.empty());
    stack.redo();
    REQUIRE(doc.findFeature(ex) != nullptr);
    REQUIRE(doc.findFeature(ex)->name() == "Extrusión1");
    REQUIRE_THAT(totalVolume(doc.result()), WithinRel(1000.0, 1e-9));

    // Editar.
    auto edited = extrudeAll(doc, sk, 8);
    edited->setName("Extrusión1");
    stack.push(std::make_unique<commands::EditFeature>(ex, std::move(edited)));
    REQUIRE_THAT(totalVolume(doc.result()), WithinRel(1600.0, 1e-9));
    stack.undo();
    REQUIRE_THAT(totalVolume(doc.result()), WithinRel(1000.0, 1e-9));
    stack.redo();
    REQUIRE_THAT(totalVolume(doc.result()), WithinRel(1600.0, 1e-9));

    // Suprimir.
    stack.push(std::make_unique<commands::SuppressFeature>(ex, true));
    REQUIRE(doc.result().bodies.empty());
    REQUIRE(stack.undoLabel() == "Suprimir Extrusión1");
    stack.undo();
    REQUIRE(doc.result().bodies.size() == 1);

    // Mover el sketch después de su extrusión no está permitido.
    REQUIRE_FALSE(doc.canMoveFeature(sk, 1));
    REQUIRE(doc.dependents(sk) == std::vector<FeatureId>{ex});

    // Borrar y deshacer: vuelve en su sitio.
    stack.push(std::make_unique<commands::DeleteFeature>(ex));
    REQUIRE(doc.featureCount() == 1);
    stack.undo();
    REQUIRE(doc.featureCount() == 2);
    REQUIRE(doc.feature(1).id() == ex);
    REQUIRE(doc.marker() == 2);

    // Marcador.
    stack.push(std::make_unique<commands::SetMarker>(1));
    REQUIRE(doc.result().bodies.empty());
    stack.undo();
    REQUIRE(doc.result().bodies.size() == 1);
}

TEST_CASE("Documento: referencias a operaciones dentro de los nombres", "[document][model]")
{
    REQUIRE(featuresIn({"f4:lado:f3:fin:e1|f3:lado:e5", 0}) == std::vector<FeatureId>{4, 3});
    REQUIRE(featuresIn({"f12:fin:e7", 0}) == std::vector<FeatureId>{12});
    REQUIRE(featuresIn({"bool:?3", 0}).empty());
}

TEST_CASE("Documento: lo proyectado sigue a la arista al cambiar la pieza", "[document][model]")
{
    Document doc;
    const FeatureId sk = doc.addFeature(sketchOn(rectangleSketch(0, 0, 20, 10)));
    const FeatureId ex = doc.addFeature(extrudeAll(doc, sk, 5));

    // Arista superior de la cara frontal (y = 0), proyectada sobre XY.
    const geometry::Shape& body = doc.result().bodies.front().shape;
    std::optional<int> edge;
    for (int i = 0; i < body.edgeCount(); ++i) {
        const auto line = body.edgeLine(i);
        const std::string& name = body.edgeName(i);
        if (line && name.find(":fin:") != std::string::npos && std::abs(line->first.y) < 1e-9
            && std::abs(line->second.x) > 0.5) {
            edge = i;
        }
    }
    REQUIRE(edge);
    const geometry::ElementRef ref = body.edgeRef(*edge);
    const std::vector<sketch::Geometry> projected = body.projectEdge(*edge, sketch::Plane::xy());
    REQUIRE(projected.size() == 1);

    sketch::Sketch s2;
    const sketch::EntityId linked = s2.addGeometry(projected.front());
    s2.entity(linked)->locked = true;
    s2.entity(linked)->link = sketch::ModelLink{ref.name, ref.occurrence, 0};
    const FeatureId sk2 = doc.addFeature(sketchOn(std::move(s2)));
    REQUIRE(doc.status(sk2).health == Health::Ok);

    // Más ancho: la línea proyectada se alarga hasta x = 30.
    doc.replaceFeature(sk, sketchOn(rectangleSketch(0, 0, 30, 10)));
    REQUIRE(doc.status(ex).health == Health::Ok);
    REQUIRE(doc.status(sk2).health == Health::Ok);
    const sketch::Entity* e = doc.result().sketches.at(sk2).sketch->entity(linked);
    const auto& line = std::get<sketch::Line>(e->geometry);
    REQUIRE_THAT(std::max(line.start.x, line.end.x), WithinAbs(30.0, 1e-9));
    // El sketch guardado no cambia (solo el calculado).
    const auto* stored = static_cast<const SketchFeature*>(doc.findFeature(sk2));
    const auto& old = std::get<sketch::Line>(stored->sketch().entity(linked)->geometry);
    REQUIRE_THAT(std::max(old.start.x, old.end.x), WithinAbs(20.0, 1e-9));

    // Si la arista desaparece, se mantiene como estaba y avisa.
    doc.setSuppressed(ex, true);
    REQUIRE(doc.status(sk2).health == Health::Warning);
}
