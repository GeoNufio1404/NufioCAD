// Fase 21: piezas fijas, Alinear, uniones con límites y enlaces de movimiento.

#include <nuf/commands/DuplicateBodies.hpp>
#include <nuf/document/Document.hpp>
#include <nuf/document/Features.hpp>
#include <nuf/document/Generators.hpp>
#include <nuf/document/Interference.hpp>
#include <nuf/io/DocumentFile.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <filesystem>
#include <numbers>

using namespace nuf;
using namespace nuf::document;
using Catch::Matchers::WithinAbs;

namespace {

constexpr double kPi = std::numbers::pi;

FeatureId addBox(Document& doc, double x, double y, double z, double w, double d, double h)
{
    auto box = std::make_unique<BoxFeature>();
    box->base.origin = OriginPlane::XY;
    box->x = x;
    box->y = y;
    box->z = z;
    box->width = w;
    box->depth = d;
    box->height = h;
    return doc.addFeature(std::move(box));
}

FeatureId addCylinder(Document& doc, double x, double y, double z, double diameter, double height)
{
    auto cylinder = std::make_unique<CylinderFeature>();
    cylinder->base.origin = OriginPlane::XY;
    cylinder->x = x;
    cylinder->y = y;
    cylinder->z = z;
    cylinder->diameter = diameter;
    cylinder->height = height;
    return doc.addFeature(std::move(cylinder));
}

// La cara plana de un cuerpo cuya normal mira hacia «z» (+1 arriba, −1 abajo).
geometry::ElementRef flatFace(const geometry::Shape& shape, double z)
{
    for (int i = 0; i < shape.faceCount(); ++i) {
        if (const auto plane = shape.facePlane(i); plane && plane->normal.z * z > 0.9) {
            return shape.faceRef(i);
        }
    }
    FAIL("no hay cara plana");
    return {};
}

// La primera cara cilíndrica (la de eje).
geometry::ElementRef roundFace(const geometry::Shape& shape)
{
    for (int i = 0; i < shape.faceCount(); ++i) {
        if (const auto frame = shape.faceFrame(i); frame && frame->kind == geometry::Frame::Kind::Axis) {
            return shape.faceRef(i);
        }
    }
    FAIL("no hay cara cilíndrica");
    return {};
}

const Body& body(const Document& doc, std::size_t index)
{
    return doc.result().bodies.at(index);
}

sketch::Vec3 centerOf(const geometry::Shape& shape)
{
    const geometry::Box b = shape.bounds();
    return {(b.min.x + b.max.x) / 2.0, (b.min.y + b.max.y) / 2.0, (b.min.z + b.max.z) / 2.0};
}

FeatureId addAlign(Document& doc, const ElementPick& moving, const ElementPick& target, bool flip = false)
{
    auto align = std::make_unique<AlignFeature>();
    align->moving = moving;
    align->target = target;
    align->flip = flip;
    return doc.addFeature(std::move(align));
}

// Una placa de 100×100×5 y un bloque de 20×10×5 lejos de ella.
Document plateAndBlock()
{
    Document doc;
    addBox(doc, 0, 0, 0, 100, 100, 5);
    addBox(doc, 200, 0, 0, 20, 10, 5);
    return doc;
}

} // namespace

TEST_CASE("Los elementos dan su marco: plano, eje de cilindro y centro de arista", "[assembly]")
{
    Document doc;
    addCylinder(doc, 10, 20, 0, 8, 30);
    const geometry::Shape& cylinder = body(doc, 0).shape;
    const auto top = cylinder.faceFrame(*cylinder.findFace(flatFace(cylinder, 1.0)));
    REQUIRE(top);
    CHECK(top->kind == geometry::Frame::Kind::PlaneFace);
    CHECK_THAT(top->origin.z, WithinAbs(30.0, 1e-6));
    CHECK_THAT(top->direction.z, WithinAbs(1.0, 1e-6));
    const auto side = cylinder.faceFrame(*cylinder.findFace(roundFace(cylinder)));
    REQUIRE(side);
    CHECK(side->kind == geometry::Frame::Kind::Axis);
    CHECK_THAT(side->origin.x, WithinAbs(10.0, 1e-6));
    CHECK_THAT(side->origin.y, WithinAbs(20.0, 1e-6));
    CHECK_THAT(side->origin.z, WithinAbs(15.0, 1e-6));  // a media altura
    CHECK_THAT(std::abs(side->direction.z), WithinAbs(1.0, 1e-6));
    // Una arista circular: su centro y la normal de su plano.
    bool circle = false;
    for (int i = 0; i < cylinder.edgeCount(); ++i) {
        if (const auto frame = cylinder.edgeFrame(i); frame && std::abs(frame->origin.z - 30.0) < 1e-6) {
            CHECK_THAT(frame->origin.x, WithinAbs(10.0, 1e-6));
            circle = true;
        }
    }
    CHECK(circle);
}

TEST_CASE("Alinear coloca una pieza sobre otra, centro con centro", "[assembly][align]")
{
    Document doc = plateAndBlock();
    const FeatureId id = addAlign(doc, {flatFace(body(doc, 1).shape, -1.0), false}, {flatFace(body(doc, 0).shape, 1.0), false});
    REQUIRE(doc.status(id).health == Health::Ok);
    const geometry::Box block = body(doc, 1).shape.bounds();
    CHECK_THAT(block.min.z, WithinAbs(5.0, 1e-6));   // apoyado sobre la placa
    CHECK_THAT(centerOf(body(doc, 1).shape).x, WithinAbs(50.0, 1e-6));
    CHECK_THAT(centerOf(body(doc, 1).shape).y, WithinAbs(50.0, 1e-6));
    CHECK_THAT(body(doc, 0).shape.bounds().max.z, WithinAbs(5.0, 1e-6));  // la placa no se movió
    // Volteado, queda del mismo lado: la cara de abajo mira hacia arriba.
    Document flipped = plateAndBlock();
    addAlign(flipped, {flatFace(body(flipped, 1).shape, -1.0), false}, {flatFace(body(flipped, 0).shape, 1.0), false}, true);
    CHECK_THAT(body(flipped, 1).shape.bounds().max.z, WithinAbs(5.0, 1e-6));
}

TEST_CASE("Alinear dos ejes de cilindro los deja uno sobre el otro", "[assembly][align]")
{
    Document doc;
    addCylinder(doc, 0, 0, 0, 10, 20);
    addCylinder(doc, 50, 30, 0, 6, 10);
    const FeatureId id = addAlign(doc, {roundFace(body(doc, 1).shape), false}, {roundFace(body(doc, 0).shape), false});
    REQUIRE(doc.status(id).health == Health::Ok);
    const sketch::Vec3 c = centerOf(body(doc, 1).shape);
    CHECK_THAT(c.x, WithinAbs(0.0, 1e-6));
    CHECK_THAT(c.y, WithinAbs(0.0, 1e-6));
    CHECK_THAT(c.z, WithinAbs(10.0, 1e-6));  // a media altura del otro
    // Con el borde circular de arriba del que salta y el de abajo del otro: se apoya, y no se pasa de un lado.
    Document edges;
    addCylinder(edges, 0, 0, 0, 10, 20);
    addCylinder(edges, 50, 30, 0, 6, 10);
    const geometry::Shape& moving = body(edges, 1).shape;
    const geometry::Shape& target = body(edges, 0).shape;
    std::optional<geometry::ElementRef> from;
    std::optional<geometry::ElementRef> to;
    for (int i = 0; i < moving.edgeCount(); ++i) {
        if (const auto f = moving.edgeFrame(i); f && std::abs(f->origin.z - 0.0) < 1e-6) {
            from = moving.edgeRef(i);
        }
    }
    for (int i = 0; i < target.edgeCount(); ++i) {
        if (const auto f = target.edgeFrame(i); f && std::abs(f->origin.z - 20.0) < 1e-6) {
            to = target.edgeRef(i);
        }
    }
    REQUIRE(from);
    REQUIRE(to);
    const FeatureId e = addAlign(edges, {*from, true}, {*to, true});
    REQUIRE(edges.status(e).health == Health::Ok);
    CHECK_THAT(centerOf(body(edges, 1).shape).x, WithinAbs(0.0, 1e-6));
    CHECK_THAT(body(edges, 1).shape.bounds().min.z, WithinAbs(20.0, 1e-6));
}

TEST_CASE("Una pieza fija no se mueve: se mueve la otra", "[assembly][align][fixed]")
{
    Document doc = plateAndBlock();
    const auto plateKey = body(doc, 0).key;
    const auto blockKey = body(doc, 1).key;
    const FeatureId id = addAlign(doc, {flatFace(body(doc, 1).shape, -1.0), false}, {flatFace(body(doc, 0).shape, 1.0), false});
    REQUIRE(!doc.isFixed(blockKey));
    // Se fija el bloque: la placa sube hasta él (su cara de arriba queda bajo el bloque, centrada).
    doc.setBodyInfo({{blockKey, {"", "", true}}});
    REQUIRE(doc.isFixed(blockKey));
    REQUIRE(doc.status(id).health == Health::Ok);
    CHECK_THAT(body(doc, 1).shape.bounds().min.x, WithinAbs(200.0, 1e-6));  // el bloque se queda donde estaba
    CHECK_THAT(body(doc, 1).shape.bounds().min.z, WithinAbs(0.0, 1e-6));
    CHECK_THAT(body(doc, 0).shape.bounds().max.z, WithinAbs(0.0, 1e-6));    // la placa quedó debajo
    CHECK_THAT(centerOf(body(doc, 0).shape).x, WithinAbs(210.0, 1e-6));
    CHECK_THAT(centerOf(body(doc, 0).shape).y, WithinAbs(5.0, 1e-6));
    // Con las dos fijas no hay nada que mover.
    doc.setBodyInfo({{blockKey, {"", "", true}}, {plateKey, {"", "", true}}});
    CHECK(doc.status(id).health == Health::Error);
    // Soltar el bloque vuelve a lo de antes.
    doc.setBodyInfo({{plateKey, {"", "", true}}});
    REQUIRE(doc.status(id).health == Health::Ok);
    CHECK_THAT(body(doc, 1).shape.bounds().min.z, WithinAbs(5.0, 1e-6));
    CHECK_THAT(body(doc, 0).shape.bounds().max.z, WithinAbs(5.0, 1e-6));
}

TEST_CASE("Lo fijo y lo alineado se guardan y se abren", "[assembly][io]")
{
    Document doc = plateAndBlock();
    const auto plateKey = body(doc, 0).key;
    const FeatureId id = addAlign(doc, {flatFace(body(doc, 1).shape, -1.0), false}, {flatFace(body(doc, 0).shape, 1.0), false});
    doc.setBodyInfo({{plateKey, {"Base", "", true}}});
    const auto path = std::filesystem::temp_directory_path() / "nuf_alinear.nuf";
    std::filesystem::remove(path);
    io::saveDocument(path, doc, {});
    Document reopened;
    io::ViewState view;
    io::loadDocument(path, reopened, view);
    std::filesystem::remove(path);
    CHECK(reopened.isFixed(plateKey));
    REQUIRE(reopened.findFeature(id)->kind() == FeatureKind::Align);
    REQUIRE(reopened.status(id).health == Health::Ok);
    CHECK_THAT(centerOf(reopened.result().bodies[1].shape).x, WithinAbs(50.0, 1e-6));
}

namespace {

// Una placa con un bloque encima unido con una unión de ese tipo; devuelve el id de la unión.
FeatureId plateWithBlock(Document& doc, double x, JointType type, double angle, double slide = 0.0)
{
    addBox(doc, x, 0, 0, 100, 100, 5);
    addBox(doc, x + 300, 0, 0, 20, 10, 5);
    const std::size_t plate = doc.result().bodies.size() - 2;
    auto joint = std::make_unique<JointFeature>();
    joint->fixedFace = flatFace(body(doc, plate).shape, 1.0);
    joint->movingFace = flatFace(body(doc, plate + 1).shape, -1.0);
    joint->type = type;
    joint->angle = angle;
    joint->slide = slide;
    return doc.addFeature(std::move(joint));
}

} // namespace

TEST_CASE("Una unión giratoria deja el eje quieto y respeta sus límites", "[assembly][joint]")
{
    Document doc;
    const FeatureId id = plateWithBlock(doc, 0, JointType::Revolute, kPi / 2.0);
    REQUIRE(doc.status(id).health == Health::Ok);
    const geometry::Box b = body(doc, 1).shape.bounds();
    CHECK_THAT(b.max.x - b.min.x, WithinAbs(10.0, 1e-6));  // de 20×10 a 10×20
    CHECK_THAT(centerOf(body(doc, 1).shape).x, WithinAbs(50.0, 1e-6));
    // Con un tope de 30°, pedir 90° la deja en 30° y avisa.
    auto limited = std::unique_ptr<Feature>(doc.findFeature(id)->clone());
    auto& joint = static_cast<JointFeature&>(*limited);
    joint.limitAngle = true;
    joint.angleMin = 0.0;
    joint.angleMax = kPi / 6.0;
    doc.replaceFeature(id, std::move(limited));
    CHECK(doc.status(id).health == Health::Warning);
    const geometry::Box c = body(doc, 1).shape.bounds();
    CHECK_THAT(c.max.x - c.min.x, WithinAbs(20.0 * std::cos(kPi / 6.0) + 10.0 * std::sin(kPi / 6.0), 1e-6));
}

TEST_CASE("Las uniones nuevas: deslizante, cilíndrica, plana y esférica", "[assembly][joint]")
{
    Document slider;
    plateWithBlock(slider, 0, JointType::Slider, 0.0, 12.0);
    CHECK_THAT(body(slider, 1).shape.bounds().min.x, WithinAbs(12.0 + 40.0, 1e-6));  // centrada sobre la placa y corrida 12

    Document cylindrical;
    const FeatureId c = plateWithBlock(cylindrical, 0, JointType::Cylindrical, kPi / 2.0);
    auto moved = std::unique_ptr<Feature>(cylindrical.findFeature(c)->clone());
    static_cast<JointFeature&>(*moved).offset = 7.0;  // se desliza por el eje (hacia arriba)
    cylindrical.replaceFeature(c, std::move(moved));
    CHECK_THAT(body(cylindrical, 1).shape.bounds().min.z, WithinAbs(5.0 + 7.0, 1e-6));

    Document planar;
    const FeatureId p = plateWithBlock(planar, 0, JointType::Planar, 0.0);
    auto planarMoved = std::unique_ptr<Feature>(planar.findFeature(p)->clone());
    static_cast<JointFeature&>(*planarMoved).slide = 3.0;
    static_cast<JointFeature&>(*planarMoved).slideY = -4.0;
    planar.replaceFeature(p, std::move(planarMoved));
    CHECK_THAT(centerOf(body(planar, 1).shape).x, WithinAbs(53.0, 1e-6));
    CHECK_THAT(centerOf(body(planar, 1).shape).y, WithinAbs(46.0, 1e-6));

    Document ball;
    const FeatureId s = plateWithBlock(ball, 0, JointType::Ball, 0.0);
    auto tilted = std::unique_ptr<Feature>(ball.findFeature(s)->clone());
    static_cast<JointFeature&>(*tilted).tiltY = kPi / 2.0;
    ball.replaceFeature(s, std::move(tilted));
    CHECK(ball.status(s).health == Health::Ok);
    const geometry::Box tilt = body(ball, 1).shape.bounds();
    CHECK_THAT(tilt.max.z - tilt.min.z, WithinAbs(20.0, 1e-6));  // el bloque quedó de canto
}

TEST_CASE("Un enlace de movimiento mueve varias uniones en la proporción pedida", "[assembly][link]")
{
    Document doc;
    const FeatureId a = plateWithBlock(doc, 0, JointType::Revolute, 0.0);
    const FeatureId b = plateWithBlock(doc, 1000, JointType::Revolute, 0.0);
    REQUIRE(doc.status(a).health == Health::Ok);
    REQUIRE(doc.status(b).health == Health::Ok);
    auto link = std::make_unique<MotionLinkFeature>();
    link->members = {{a, 1.0}, {b, -2.0}};
    link->position = kPi / 4.0;
    const FeatureId id = doc.addFeature(std::move(link));
    REQUIRE(doc.status(id).health == Health::Ok);
    // El primero giró 45° y el segundo −90°: el segundo queda de 10×20 y el primero en diagonal.
    const geometry::Box first = body(doc, 1).shape.bounds();
    const geometry::Box second = body(doc, 3).shape.bounds();
    CHECK_THAT(first.max.x - first.min.x, WithinAbs((20.0 + 10.0) / std::sqrt(2.0), 1e-6));
    CHECK_THAT(second.max.x - second.min.x, WithinAbs(10.0, 1e-6));
    CHECK_THAT(second.max.y - second.min.y, WithinAbs(20.0, 1e-6));
    // Los centros no se mueven: giran sobre su eje.
    CHECK_THAT(centerOf(body(doc, 1).shape).x, WithinAbs(50.0, 1e-6));
    CHECK_THAT(centerOf(body(doc, 3).shape).x, WithinAbs(1050.0, 1e-6));
    // Con un solo miembro, o con una unión rígida, da error claro.
    auto lonely = std::make_unique<MotionLinkFeature>();
    lonely->members = {{a, 1.0}};
    CHECK(doc.status(doc.addFeature(std::move(lonely))).health == Health::Error);
    // Se guarda y se abre.
    const auto path = std::filesystem::temp_directory_path() / "nuf_enlace.nuf";
    std::filesystem::remove(path);
    io::saveDocument(path, doc, {});
    Document reopened;
    io::ViewState view;
    io::loadDocument(path, reopened, view);
    std::filesystem::remove(path);
    REQUIRE(reopened.findFeature(id)->kind() == FeatureKind::MotionLink);
    REQUIRE(reopened.status(id).health == Health::Ok);
    const geometry::Box again = reopened.result().bodies[3].shape.bounds();
    CHECK_THAT(again.max.y - again.min.y, WithinAbs(20.0, 1e-6));
}

TEST_CASE("Duplicar copia varios cuerpos de una vez, con nombres y componente nuevos", "[assembly][duplicate]")
{
    Document doc;
    addBox(doc, 0, 0, 0, 10, 10, 10);
    addBox(doc, 20, 0, 0, 10, 10, 10);
    addBox(doc, 40, 0, 0, 10, 10, 10);
    const std::string a = body(doc, 0).key;
    const std::string b = body(doc, 1).key;
    const std::string c = body(doc, 2).key;
    doc.setBodyInfo({{a, {"Tapa", "Caja 1", false}}, {b, {"Base 1", "Caja 1", false}}, {c, {"Suelto", "", false}}});

    commands::DuplicateBodies duplicate({a, b, c}, {0, 100, 0});
    duplicate.execute(doc);
    REQUIRE(doc.result().bodies.size() == 6);
    REQUIRE(duplicate.newKeys().size() == 3);
    const auto find = [&](const std::string& key) -> const Body& {
        for (const Body& x : doc.result().bodies) {
            if (x.key == key) {
                return x;
            }
        }
        FAIL("falta el cuerpo");
        return doc.result().bodies.front();
    };
    // Cada copia, desplazada 100 en Y, con el siguiente número en su nombre.
    CHECK_THAT(find(duplicate.newKeys()[0]).shape.bounds().min.y, WithinAbs(100.0, 1e-6));
    CHECK_THAT(find(duplicate.newKeys()[2]).shape.bounds().min.x, WithinAbs(40.0, 1e-6));
    CHECK(find(duplicate.newKeys()[0]).name == "Tapa 2");
    CHECK(find(duplicate.newKeys()[1]).name == "Base 2");
    CHECK(find(duplicate.newKeys()[2]).name == "Suelto 2");
    // Las dos primeras van a un componente nuevo (el mismo), la tercera a ninguno.
    CHECK(doc.componentOf(duplicate.newKeys()[0]) == "Caja 2");
    CHECK(doc.componentOf(duplicate.newKeys()[1]) == "Caja 2");
    CHECK(doc.componentOf(duplicate.newKeys()[2]).empty());
    // Los originales no cambian de sitio ni de componente.
    CHECK_THAT(find(a).shape.bounds().min.y, WithinAbs(0.0, 1e-6));
    CHECK(doc.componentOf(a) == "Caja 1");
    // Duplicar otra vez el componente da el siguiente número.
    commands::DuplicateBodies again({a, b}, {0, 200, 0});
    again.execute(doc);
    CHECK(doc.componentOf(again.newKeys()[0]) == "Caja 3");
    CHECK(find(again.newKeys()[0]).name == "Tapa 3");
    // Deshacer lo quita todo; rehacer lo vuelve a poner igual.
    again.undo(doc);
    CHECK(doc.result().bodies.size() == 6);
    CHECK(doc.componentOf(again.newKeys()[0]).empty());
    duplicate.undo(doc);
    CHECK(doc.result().bodies.size() == 3);
    duplicate.execute(doc);
    CHECK(doc.result().bodies.size() == 6);
    CHECK(find(duplicate.newKeys()[0]).name == "Tapa 2");
}

TEST_CASE("Duplicar un cuerpo fijo da una copia suelta", "[assembly][duplicate][fixed]")
{
    Document doc;
    addBox(doc, 0, 0, 0, 10, 10, 10);
    const std::string a = body(doc, 0).key;
    doc.setBodyInfo({{a, {"Base", "", true}}});
    commands::DuplicateBodies duplicate({a}, {20, 0, 0});
    duplicate.execute(doc);
    REQUIRE(doc.result().bodies.size() == 2);
    CHECK(doc.isFixed(a));
    CHECK_FALSE(doc.isFixed(duplicate.newKeys()[0]));
}

TEST_CASE("Las interferencias dan los cuerpos que se pisan y su volumen común", "[assembly][interference]")
{
    Document doc;
    addBox(doc, 0, 0, 0, 10, 10, 10);
    addBox(doc, 5, 0, 0, 10, 10, 10);   // se pisa con la primera: 5×10×10 en común
    addBox(doc, 15, 0, 0, 10, 10, 10);  // toca a la segunda por una cara: no cuenta
    addBox(doc, 100, 0, 0, 10, 10, 10); // lejos
    const auto pairs = findInterferences(doc.result());
    REQUIRE(pairs.size() == 1);
    CHECK_THAT(pairs[0].volume, WithinAbs(500.0, 1e-6));
    CHECK(pairs[0].first == body(doc, 0).key);
    CHECK(pairs[0].second == body(doc, 1).key);
    CHECK_THAT(pairs[0].shape.volume(), WithinAbs(500.0, 1e-6));
    // Los ocultos no estorban.
    CHECK(findInterferences(doc.result(), 1e-3, {body(doc, 1).key}).empty());
    // Con un mínimo mayor que lo común, tampoco.
    CHECK(findInterferences(doc.result(), 600.0).empty());
}

TEST_CASE("Las interferencias a lo largo del giro encuentran el peor momento", "[assembly][interference][link]")
{
    Document doc;
    const FeatureId a = plateWithBlock(doc, 0, JointType::Revolute, 0.0);
    const FeatureId b = plateWithBlock(doc, 1000, JointType::Revolute, 0.0);
    addBox(doc, 48, 56, 5, 4, 6, 5);  // un poste junto al bloque: solo lo toca al girar
    auto link = std::make_unique<MotionLinkFeature>();
    link->members = {{a, 1.0}, {b, -2.0}};
    const FeatureId id = doc.addFeature(std::move(link));
    REQUIRE(doc.status(id).health == Health::Ok);
    CHECK(findInterferences(doc.result()).empty());  // quieto no se pisan

    int checked = 0;
    const auto hit = findInterferencesAlong(doc, id, kPi / 2.0, 9, &checked);
    CHECK(checked == 10);
    REQUIRE(hit.size() == 1);
    // Lo peor no es de canto (80 mm³ a 90°) sino a 70°, cuando la esquina del bloque entra más en el poste.
    CHECK(hit[0].volume > 85.0);
    CHECK_THAT(hit[0].at, WithinAbs(7.0 * kPi / 18.0, 1e-9));
    // Un giro corto, de 5°, no llega al poste.
    CHECK(findInterferencesAlong(doc, id, kPi / 36.0, 5, &checked).empty());
    // Se puede cancelar entre posición y posición.
    int calls = 0;
    findInterferencesAlong(doc, id, kPi / 2.0, 9, &checked, 1e-3, [&] { return ++calls > 3; });
    CHECK(checked == 3);
}

TEST_CASE("El tren planetario engrana a la vez con el sol y la corona, y gira al mover el enlace", "[assembly][planetary]")
{
    const Generator* generator = findGenerator("tren-planetario");
    REQUIRE(generator != nullptr);
    GeneratorValues values = generator->defaults();
    Document doc;
    applyRecipe(doc, generator->make(values, ""));
    // Sol, 3 planetas, corona, portasatélites, 3 ejes y el eje central: 10 cuerpos.
    REQUIRE(doc.result().bodies.size() == 10);
    for (std::size_t i = 0; i < doc.featureCount(); ++i) {
        INFO(doc.feature(i).name() << ": " << doc.status(doc.feature(i).id()).message);
        CHECK(doc.status(doc.feature(i).id()).health != Health::Error);
    }
    // La corona, la placa y los ejes salen fijos; el sol y los planetas no.
    const auto& bodies = doc.result().bodies;
    CHECK_FALSE(doc.isFixed(bodies[0].key));  // sol
    CHECK_FALSE(doc.isFixed(bodies[1].key));  // planeta 1
    CHECK(doc.isFixed(bodies[4].key));        // corona: lo único fijo
    CHECK_FALSE(doc.isFixed(bodies[5].key));  // portasatélites: gira con los planetas
    CHECK_FALSE(doc.isFixed(bodies[9].key));
    CHECK(bodies[4].name == "Corona");
    // El enlace existe, apunta a las 5 uniones (sol, 3 planetas y portasatélites); con 12, 12 y 36 dientes el planeta gira
    // sobre su eje −36·12/(48·12) = −0,75 veces lo del sol y el portasatélites 12/48 = 0,25.
    const MotionLinkFeature* link = nullptr;
    FeatureId linkId = 0;
    for (std::size_t i = 0; i < doc.featureCount(); ++i) {
        if (const auto* candidate = dynamic_cast<const MotionLinkFeature*>(&doc.feature(i))) {
            link = candidate;
            linkId = candidate->id();
        }
    }
    REQUIRE(link != nullptr);
    REQUIRE(link->members.size() == 5);
    CHECK_THAT(link->members[1].ratio, WithinAbs(-0.75, 1e-9));
    CHECK_THAT(link->members[4].ratio, WithinAbs(0.25, 1e-9));
    for (const auto& member : link->members) {
        CHECK(doc.findFeature(member.joint) != nullptr);
    }
    // En reposo no se pisan los dientes: los tres engranes están bien puestos (el aire lo da «adelgazar»).
    const auto rest = findInterferences(doc.result(), 1.0);
    INFO("parejas que se pisan en reposo: " << rest.size());
    CHECK(rest.empty());
    // La unión del portasatélites no lo movió de su sitio (su centro sigue bajo el tren).
    CHECK_THAT(bodies[5].shape.bounds().min.z, WithinAbs(-4.0, 1e-6));
    CHECK_THAT(bodies[5].shape.bounds().max.z, WithinAbs(-1.0, 1e-6));
    // A lo largo de 60° del sol (dos pasos de sus dientes) tampoco se pisan en ninguna posición.
    int checked = 0;
    const auto along = findInterferencesAlong(doc, linkId, 60.0 * kPi / 180.0, 12, &checked, 1.0);
    CHECK(checked == 13);
    INFO("parejas que se pisan en el giro: " << along.size());
    CHECK(along.empty());
}

TEST_CASE("El tren planetario avisa cuando los planetas no caben a partes iguales", "[assembly][planetary]")
{
    const Generator* generator = findGenerator("tren-planetario");
    REQUIRE(generator != nullptr);
    GeneratorValues values = generator->defaults();
    // 12 + 36 = 48 dientes entre 3 planetas: entero. Con 5 planetas, no.
    CHECK(generator->summary(values).find("AVISO") == std::string::npos);
    values["planetas"] = 5.0;
    CHECK(generator->summary(values).find("AVISO") != std::string::npos);
}
