// Ajustes de impresión de los engranajes: juego entre dientes (adelgaza el diente) y chaflán en la base.
#include <nuf/geometry/Modeling.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <numbers>
#include <string>

using namespace nuf;

namespace {

sketch::Plane xy()
{
    return sketch::Plane::fromPointAndNormal({0, 0, 0}, {0, 0, 1});
}

geometry::GearSpec spur()
{
    geometry::GearSpec spec;
    spec.teeth = 20;
    spec.module = 2.0;
    spec.thickness = 10.0;
    spec.bore = 5.0;
    return spec;
}

int countNamed(const geometry::Shape& shape, const std::string& part)
{
    int n = 0;
    for (int i = 0; i < shape.faceCount(); ++i) {
        n += shape.faceName(i).find(part) != std::string::npos ? 1 : 0;
    }
    return n;
}

} // namespace

TEST_CASE("El juego adelgaza cada diente sin cambiar el diámetro exterior", "[gear][tuning]")
{
    const geometry::Shape plain = geometry::gear("g", xy(), spur());
    geometry::GearSpec loose = spur();
    loose.backlash = 0.2;
    const geometry::Shape thin = geometry::gear("g", xy(), loose);
    REQUIRE(thin.isValid());
    REQUIRE(thin.faceCount() == plain.faceCount());
    REQUIRE_THAT(thin.bounds().max.x, Catch::Matchers::WithinAbs(plain.bounds().max.x, 1e-6));
    // Cada diente pierde ~0,2 mm de grueso a lo largo de su altura (2,25 módulos) y de todo el ancho.
    const double lost = plain.volume() - thin.volume();
    const double expected = 20 * 0.2 * 2.25 * 2.0 * 10.0;
    REQUIRE(lost > 0.6 * expected);
    REQUIRE(lost < 1.4 * expected);
    // Más juego, más delgado.
    loose.backlash = 0.4;
    REQUIRE(geometry::gear("g", xy(), loose).volume() < thin.volume());
}

TEST_CASE("El juego también adelgaza el diente de la corona y el de la cremallera", "[gear][tuning]")
{
    geometry::GearSpec ring;
    ring.type = geometry::GearType::Internal;
    ring.teeth = 40;
    ring.thickness = 8.0;
    const geometry::Shape plainRing = geometry::gear("c", xy(), ring);
    ring.backlash = 0.2;
    const geometry::Shape looseRing = geometry::gear("c", xy(), ring);
    REQUIRE(looseRing.isValid());
    REQUIRE(looseRing.volume() < plainRing.volume());

    geometry::GearSpec rack;
    rack.type = geometry::GearType::Rack;
    rack.teeth = 6;
    rack.thickness = 8.0;
    const geometry::Shape plainRack = geometry::gear("r", xy(), rack);
    rack.backlash = 0.2;
    const geometry::Shape looseRack = geometry::gear("r", xy(), rack);
    REQUIRE(looseRack.isValid());
    REQUIRE(looseRack.volume() < plainRack.volume());
}

TEST_CASE("Un juego negativo o que deja el diente sin cuerpo se rechaza", "[gear][tuning]")
{
    geometry::GearSpec spec = spur();
    spec.backlash = -0.1;
    REQUIRE_THROWS_AS(geometry::gear("g", xy(), spec), geometry::GeometryError);
    spec.backlash = 5.0;  // más que el grueso del diente en la primitiva (π·módulo/2 = 3,14)
    REQUIRE_THROWS_AS(geometry::gear("g", xy(), spec), geometry::GeometryError);
}

TEST_CASE("El chaflán de la base achaflana el contorno de la primera capa y no el agujero", "[gear][tuning]")
{
    const geometry::Shape plain = geometry::gear("g", xy(), spur());
    geometry::GearSpec spec = spur();
    spec.baseChamfer = 0.2;
    const geometry::Shape chamfered = geometry::gear("g", xy(), spec);
    REQUIRE(chamfered.isValid());
    REQUIRE(chamfered.solidCount() == 1);
    REQUIRE_THAT(chamfered.bounds().max.x, Catch::Matchers::WithinAbs(plain.bounds().max.x, 0.2));  // la caja de una superficie curva es aproximada
    REQUIRE_THAT(chamfered.bounds().max.z, Catch::Matchers::WithinAbs(10.0, 1e-3));
    // Quita un poco de material, y solo en la base.
    const double lost = plain.volume() - chamfered.volume();
    REQUIRE(lost > 0.0);
    REQUIRE(lost < 20 * 14.0 * 0.2 * 0.2);  // el contorno mide menos de ~14 mm por diente
    REQUIRE(countNamed(chamfered, ":chaflan:") > 0);
    // Las caras de la tapa final y el agujero siguen con su nombre.
    REQUIRE(countNamed(chamfered, "g:fin") == 1);
    REQUIRE(countNamed(chamfered, "g:lado:agujero") == 1);
}

TEST_CASE("El chaflán de la base sirve en la cremallera", "[gear][tuning]")
{
    geometry::GearSpec rack;
    rack.type = geometry::GearType::Rack;
    rack.teeth = 6;
    rack.thickness = 8.0;
    const double before = geometry::gear("r", xy(), rack).volume();
    rack.baseChamfer = 0.2;
    const geometry::Shape chamfered = geometry::gear("r", xy(), rack);
    REQUIRE(chamfered.isValid());
    REQUIRE(chamfered.volume() < before);
}

TEST_CASE("El chaflán de la base con hélice se rechaza con un mensaje claro", "[gear][tuning]")
{
    geometry::GearSpec helical = spur();
    helical.helixAngle = 0.35;
    helical.baseChamfer = 0.15;
    REQUIRE_THROWS_AS(geometry::gear("h", xy(), helical), geometry::GeometryError);
}

TEST_CASE("Un chaflán de la base negativo o enorme se rechaza", "[gear][tuning]")
{
    geometry::GearSpec spec = spur();
    spec.baseChamfer = -1.0;
    REQUIRE_THROWS_AS(geometry::gear("g", xy(), spec), geometry::GeometryError);
    spec.baseChamfer = 3.0;  // más que el módulo
    REQUIRE_THROWS_AS(geometry::gear("g", xy(), spec), geometry::GeometryError);
}

#include <nuf/document/Document.hpp>
#include <nuf/document/Features.hpp>
#include <nuf/io/DocumentFile.hpp>

#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <filesystem>

TEST_CASE("El juego y el chaflán de la base se guardan y se ligan a parámetros", "[gear][tuning][io]")
{
    using namespace nuf::document;
    Document doc;
    doc.setParameters({{"juego", "0.2"}});
    auto gear = std::make_unique<GearFeature>();
    gear->base.origin = OriginPlane::XY;
    gear->spec.teeth = 20;
    gear->spec.thickness = 10.0;
    gear->spec.baseChamfer = 0.2;
    gear->expressions["backlash"] = "juego";
    const FeatureId id = doc.addFeature(std::move(gear));
    const double volume = doc.result().bodies.front().shape.volume();

    const auto path = std::filesystem::temp_directory_path() / "nuf_engranaje_ajustes.nuf";
    std::filesystem::remove(path);
    io::saveDocument(path, doc, {});
    Document reopened;
    io::ViewState view;
    io::loadDocument(path, reopened, view);
    std::filesystem::remove(path);
    const auto* again = static_cast<const GearFeature*>(reopened.findFeature(id));
    REQUIRE(again->expressions.at("backlash") == "juego");  // el valor sale del parámetro al calcular
    REQUIRE_THAT(again->spec.baseChamfer, Catch::Matchers::WithinAbs(0.2, 1e-12));
    REQUIRE_THAT(reopened.result().bodies.front().shape.volume(), Catch::Matchers::WithinRel(volume, 1e-9));
    // Cambiar el parámetro rehace el diente.
    reopened.setParameters({{"juego", "0.4"}});
    REQUIRE(reopened.result().bodies.front().shape.volume() < volume);
}

#include <nuf/commands/AddRecipe.hpp>
#include <nuf/document/Generators.hpp>

namespace {

double totalVolume(nuf::document::Document& doc)
{
    double sum = 0.0;
    for (const auto& body : doc.result().bodies) {
        sum += body.shape.volume();
    }
    return sum;
}

} // namespace

TEST_CASE("Los generadores de engranajes dan juego del diente y chaflán de la base, y los diseños viejos siguen igual", "[generator][gear][tuning]")
{
    using namespace nuf::document;
    for (const char* id : {"par-engranajes", "engranaje-interno", "cremallera"}) {
        INFO(id);
        const Generator* generator = findGenerator(id);
        REQUIRE(generator != nullptr);
        GeneratorValues values = generator->defaults();
        REQUIRE(values.count("adelgazar") == 1);
        REQUIRE(values.count("chaflan_base") == 1);

        Document plain;
        applyRecipe(plain, generator->make(values, ""));
        const double base = totalVolume(plain);

        // Un diseño guardado antes de estos campos no trae las claves: sale igual que con 0.
        GeneratorValues old = values;
        old.erase("adelgazar");
        old.erase("chaflan_base");
        Document legacy;
        applyRecipe(legacy, generator->make(old, ""));
        REQUIRE_THAT(totalVolume(legacy), Catch::Matchers::WithinRel(base, 1e-9));

        values["adelgazar"] = 0.2;
        Document thin;
        applyRecipe(thin, generator->make(values, ""));
        for (std::size_t i = 0; i < thin.featureCount(); ++i) {
            INFO(thin.feature(i).name());
            REQUIRE(thin.status(thin.feature(i).id()).health != Health::Error);
        }
        REQUIRE(totalVolume(thin) < base);

        values["adelgazar"] = 0.0;
        values["chaflan_base"] = 0.2;  // con los dientes por defecto de los tres generadores el kernel lo admite
        Document chamfered;
        applyRecipe(chamfered, generator->make(values, ""));
        for (std::size_t i = 0; i < chamfered.featureCount(); ++i) {
            INFO(chamfered.feature(i).name());
            REQUIRE(chamfered.status(chamfered.feature(i).id()).health != Health::Error);
        }
        REQUIRE(totalVolume(chamfered) < base);
    }
}

TEST_CASE("La espina de pescado tiene el mismo volumen que el engranaje helicoidal y dos mitades con la hélice contraria", "[gear][herringbone]")
{
    geometry::GearSpec helical = spur();
    helical.helixAngle = 0.35;
    const geometry::Shape single = geometry::gear("h", xy(), helical);
    geometry::GearSpec fish = helical;
    fish.herringbone = true;
    const geometry::Shape both = geometry::gear("h", xy(), fish);
    REQUIRE(both.isValid());
    REQUIRE(both.solidCount() == 1);
    // Mismo volumen (la hélice no cambia cuánto material hay) y mismo tamaño.
    REQUIRE_THAT(both.volume(), Catch::Matchers::WithinRel(single.volume(), 0.01));
    REQUIRE_THAT(both.bounds().max.z, Catch::Matchers::WithinAbs(10.0, 1e-3));
    REQUIRE_THAT(both.bounds().min.z, Catch::Matchers::WithinAbs(0.0, 1e-3));
    REQUIRE_THAT(both.bounds().max.x, Catch::Matchers::WithinAbs(single.bounds().max.x, 0.05));
    // Dos mitades de caras laterales: las de la segunda llevan «:m2».
    REQUIRE(countNamed(both, ":m2") > 0);
    REQUIRE(countNamed(both, ":m2") * 2 + 3 >= both.faceCount() - 3);
    // Las dos tapas y el agujero siguen con su nombre.
    REQUIRE(countNamed(both, "h:inicio") == 1);
    REQUIRE(countNamed(both, "h:fin") == 1);
    REQUIRE(countNamed(both, "h:lado:agujero") == 1);
}

TEST_CASE("La espina de pescado pide hélice y solo se hace en ruedas exteriores", "[gear][herringbone]")
{
    geometry::GearSpec spec = spur();
    spec.herringbone = true;  // sin ángulo de hélice
    REQUIRE_THROWS_AS(geometry::gear("h", xy(), spec), geometry::GeometryError);
    spec.helixAngle = 0.3;
    spec.type = geometry::GearType::Internal;
    REQUIRE_THROWS_AS(geometry::gear("h", xy(), spec), geometry::GeometryError);
}

TEST_CASE("El par de engranajes en espina de pescado sale válido y la opción se guarda", "[generator][gear][herringbone][io]")
{
    using namespace nuf::document;
    const Generator* generator = findGenerator("par-engranajes");
    REQUIRE(generator != nullptr);
    GeneratorValues values = generator->defaults();
    REQUIRE(values.count("espina") == 1);
    values["helice"] = 15.0;
    values["espina"] = 1.0;
    Document doc;
    applyRecipe(doc, generator->make(values, ""));
    REQUIRE(doc.result().bodies.size() == 2);
    for (std::size_t i = 0; i < doc.featureCount(); ++i) {
        INFO(doc.feature(i).name());
        REQUIRE(doc.status(doc.feature(i).id()).health != Health::Error);
        REQUIRE(static_cast<const GearFeature&>(doc.feature(i)).spec.herringbone);
    }
    // Se guarda y se vuelve a leer.
    const auto path = std::filesystem::temp_directory_path() / "nuf_espina.nuf";
    std::filesystem::remove(path);
    io::saveDocument(path, doc, {});
    Document reopened;
    io::ViewState view;
    io::loadDocument(path, reopened, view);
    std::filesystem::remove(path);
    REQUIRE(static_cast<const GearFeature&>(reopened.feature(0)).spec.herringbone);
}
