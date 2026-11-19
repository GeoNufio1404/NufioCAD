// Tornillo sin fin y su rueda (E4).
#include <nuf/document/Document.hpp>
#include <nuf/document/Features.hpp>
#include <nuf/document/Generators.hpp>
#include <nuf/geometry/Modeling.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numbers>

using namespace nuf;

TEST_CASE("Un tornillo sin fin tiene el volumen de un cilindro de su diámetro primitivo, y valida sus datos", "[worm]")
{
    geometry::WormSpec spec;
    spec.module = 2.0;
    spec.pitchDiameter = 20.0;
    const auto plane = sketch::Plane::fromPointAndNormal({0, 0, 0}, {0, 0, 1});
    for (int starts = 1; starts <= 4; ++starts) {
        INFO("entradas " << starts);
        spec.starts = starts;
        spec.length = std::max(6.0 * std::numbers::pi * spec.module, 2.0 * starts * std::numbers::pi * spec.module);
        const auto shape = geometry::worm("w", plane, spec);
        REQUIRE(shape.isValid());
        const double cylinder = std::numbers::pi * 100.0 * spec.length;
        CHECK(shape.volume() > 0.8 * cylinder);
        CHECK(shape.volume() < 1.1 * cylinder);
        const auto box = shape.bounds();
        CHECK(box.max.z == Catch::Approx(spec.length).margin(1e-6));
        CHECK(box.max.x >= 12.0 - 0.6);
    }
    CHECK(geometry::wormLeadAngle(spec) > 0.0);
    spec.starts = 5;
    CHECK_THROWS_AS(geometry::worm("w", plane, spec), geometry::GeometryError);
    spec.starts = 1;
    spec.pitchDiameter = 6.0;
    CHECK_THROWS_AS(geometry::worm("w", plane, spec), geometry::GeometryError);
}

// Exploración (oculta): con qué sentido de hélice y giro de la rueda engrana sin chocar.
TEST_CASE("EXPLORAR el engrane del tornillo con la rueda", "[.worm-explore]")
{
    const double pi = std::numbers::pi;
    geometry::WormSpec w;
    w.module = 2.0;
    w.starts = 1;
    w.pitchDiameter = 20.0;
    const int z = 30;
    const double px = pi * w.module;
    w.length = 8.0 * px;
    const double rp = w.module * z / 2.0;
    const double a = (w.pitchDiameter + 2.0 * rp) / 2.0;
    const double thickness = 10.0;
    for (int leftWorm = 0; leftWorm < 2; ++leftWorm) {
        for (int sign = -1; sign <= 1; sign += 2) {
            w.leftHanded = leftWorm == 1;
            sketch::Plane wp;
            wp.origin = {a, -w.length / 2.0, thickness / 2.0};
            wp.xAxis = {-1, 0, 0};
            wp.normal = {0, 1, 0};
            wp.yAxis = {0, 0, 1};
            const auto worm = geometry::worm("w", wp, w);
            geometry::GearSpec g;
            g.module = w.module;
            g.teeth = z;
            g.thickness = thickness;
            g.helixAngle = sign * geometry::wormLeadAngle(w);
            const double twist = thickness * std::tan(g.helixAngle) / rp;
            const double pitchAngle = 2.0 * pi / z;
            const auto wheel0 = geometry::gear("g", sketch::Plane::fromPointAndNormal({0, 0, 0}, {0, 0, 1}), g);
            double best = 1e300;
            int bestK = 0;
            for (int k = 0; k < 8; ++k) {
                const double rot = pi / z - twist / 2.0 + pitchAngle * k / 8.0;
                const auto wheel = geometry::moveBody("g", wheel0, {0, 0, 0}, {{0, 0, 0}, {0, 0, 1}}, rot, false);
                double v = 0;
                try {
                    v = geometry::boolean(geometry::BooleanOp::Common, wheel, worm).volume();
                } catch (...) {
                    v = -1;
                }
                if (k == 0) {
                    std::printf("mano=%d signo=%d k=0 comun=%.3f\n", leftWorm, sign, v);
                }
                if (v >= 0 && v < best) {
                    best = v;
                    bestK = k;
                }
            }
            std::printf("mano=%d signo=%d mejor k=%d comun=%.3f\n", leftWorm, sign, bestK, best);
        }
    }
}

TEST_CASE("El tornillo sin fin y su rueda engranan sin chocar, con cualquier sentido y número de entradas", "[worm][params]")
{
    const document::Generator* generator = document::findGenerator("tornillo-sin-fin");
    REQUIRE(generator != nullptr);
    for (int starts = 1; starts <= 2; ++starts) {
        for (int hand = 0; hand <= 1; ++hand) {
            INFO("entradas " << starts << " mano " << hand);
            document::GeneratorValues values = generator->defaults();
            values["entradas"] = starts;
            values["mano"] = hand;
            values["adelgazar"] = 0.0;
            document::Document doc;
            document::applyRecipe(doc, generator->make(values, ""));
            REQUIRE(doc.result().bodies.size() == 2);
            const auto& wheel = doc.result().bodies[0].shape;
            const auto& screw = doc.result().bodies[1].shape;
            REQUIRE(wheel.isValid());
            REQUIRE(screw.isValid());
            const double common = geometry::boolean(geometry::BooleanOp::Common, wheel, screw).volume();
            // Casi nada de material compartido (un diente entero serían decenas de mm³)
            CHECK(common < 3.0);
            // Y se tocan: con los ejes algo más juntos chocarían
            CHECK(wheel.volume() > 1000.0);
        }
    }
    // Prueba de que la comprobación vale: con el tornillo 1 mm más cerca sí chocan.
    document::GeneratorValues close = generator->defaults();
    close["adelgazar"] = 0.0;
    close["juego"] = -0.85;
    document::Document doc;
    document::applyRecipe(doc, generator->make(close, ""));
    CHECK(geometry::boolean(geometry::BooleanOp::Common, doc.result().bodies[0].shape, doc.result().bodies[1].shape).volume() > 3.0);
}

TEST_CASE("Un engranaje cónico es un sólido válido con el largo axial que sale del ángulo del cono, y valida sus datos", "[bevel]")
{
    const auto plane = sketch::Plane::fromPointAndNormal({0, 0, 0}, {0, 0, 1});
    geometry::GearSpec spec;
    spec.type = geometry::GearType::Bevel;
    spec.module = 2.0;
    spec.teeth = 15;
    spec.mateTeeth = 30;
    spec.thickness = 8.0;
    spec.bore = 5.0;
    const auto bevel = geometry::gear("b", plane, spec);
    REQUIRE(bevel.isValid());
    const double coneAngle = std::atan2(15.0, 30.0);
    CHECK(bevel.bounds().max.z == Catch::Approx(8.0 * std::cos(coneAngle)).margin(1e-6));
    // El extremo grande es el de una rueda recta de esos dientes y ese módulo (con la cabeza reducida): cabe en su diámetro
    CHECK(bevel.bounds().max.x <= 16.0 + 2.0 + 1e-6);
    // Menos material que el prisma de esa rueda
    geometry::GearSpec spur;
    spur.module = 2.0;
    spur.teeth = 15;
    spur.thickness = 8.0 * std::cos(coneAngle);
    spur.bore = 5.0;
    CHECK(bevel.volume() < geometry::gear("s", plane, spur).volume());
    CHECK(bevel.volume() > 0.4 * geometry::gear("s", plane, spur).volume());
    // El sentido contrario
    spec.thickness = -8.0;
    CHECK(geometry::gear("b", plane, spec).bounds().min.z == Catch::Approx(-8.0 * std::cos(coneAngle)).margin(1e-6));
    spec.thickness = 8.0;
    spec.mateTeeth = 4;
    CHECK_THROWS_AS(geometry::gear("b", plane, spec), geometry::GeometryError);
    spec.mateTeeth = 30;
    spec.helixAngle = 0.2;
    CHECK_THROWS_AS(geometry::gear("b", plane, spec), geometry::GeometryError);
    spec.helixAngle = 0.0;
    spec.thickness = 40.0;  // más de la mitad de la distancia al vértice
    CHECK_THROWS_AS(geometry::gear("b", plane, spec), geometry::GeometryError);
}

TEST_CASE("El par de engranajes cónicos engrana sin chocar, y choca con la corona mal girada", "[bevel][params]")
{
    const document::Generator* generator = document::findGenerator("engranajes-conicos");
    REQUIRE(generator != nullptr);
    for (const auto& [z1, z2] : {std::pair{15.0, 30.0}, std::pair{20.0, 20.0}, std::pair{24.0, 12.0}}) {
        INFO("dientes " << z1 << " y " << z2);
        document::GeneratorValues values = generator->defaults();
        values["dientes_1"] = z1;
        values["dientes_2"] = z2;
        values["adelgazar"] = 0.0;
        document::Document doc;
        document::applyRecipe(doc, generator->make(values, ""));
        REQUIRE(doc.result().bodies.size() == 2);
        const auto& pinion = doc.result().bodies[0].shape;
        const auto& crown = doc.result().bodies[1].shape;
        REQUIRE(pinion.isValid());
        REQUIRE(crown.isValid());
        const double common = geometry::boolean(geometry::BooleanOp::Common, pinion, crown).volume();
        // Las dos ruedas se rozan en los flancos (el diente es una aproximación): casi nada compartido frente a un diente entero
        CHECK(common < 0.02 * pinion.volume());
        // Con la corona girada medio paso, un diente cae sobre otro
        document::Recipe wrong = generator->make(values, "");
        dynamic_cast<document::GearFeature*>(wrong.features[1].get())->expressions["rotation"] = "giro_2 + 180/dientes_2";
        document::Document bad;
        document::applyRecipe(bad, wrong);
        CHECK(geometry::boolean(geometry::BooleanOp::Common, bad.result().bodies[0].shape, bad.result().bodies[1].shape).volume() > 2.0 * common);
    }
}

TEST_CASE("La corona y la cremallera helicoidales y en espina de pescado engranan sin chocar y conservan su volumen", "[gear][herringbone][rack]")
{
    const double pi = std::numbers::pi;
    const auto xy = sketch::Plane::fromPointAndNormal({0, 0, 0}, {0, 0, 1});
    const double beta = 20.0 * pi / 180.0;
    const auto count = [](const geometry::Shape& shape, const std::string& part) {
        int n = 0;
        for (int i = 0; i < shape.faceCount(); ++i) {
            n += shape.faceName(i).find(part) != std::string::npos ? 1 : 0;
        }
        return n;
    };
    for (const bool herringbone : {false, true}) {
        INFO("espina de pescado " << herringbone);
        geometry::GearSpec ring;
        ring.type = geometry::GearType::Internal;
        ring.teeth = 40;
        ring.module = 2.0;
        ring.thickness = 10.0;
        ring.rim = 5.0;
        const double straightRing = geometry::gear("c", xy, ring).volume();
        ring.helixAngle = beta;
        ring.herringbone = herringbone;
        const auto crown = geometry::gear("c", xy, ring);
        REQUIRE(crown.isValid());
        CHECK(crown.volume() == Catch::Approx(straightRing).epsilon(0.002));  // girar las secciones no cambia el área
        CHECK((count(crown, ":m2") > 0) == herringbone);
        CHECK(count(crown, ":lado:exterior") == 1);
        geometry::GearSpec pinion;
        pinion.teeth = 20;
        pinion.module = 2.0;
        pinion.thickness = 10.0;
        pinion.helixAngle = beta;  // en un engranaje interno los dos llevan el mismo sentido
        pinion.herringbone = herringbone;
        const auto small = geometry::moveBody("p", geometry::gear("p", xy, pinion), {20, 0, 0}, {{0, 0, 0}, {0, 0, 1}}, 0.0, false);
        CHECK(std::abs(geometry::boolean(geometry::BooleanOp::Common, crown, small).volume()) < 0.5);
        pinion.helixAngle = -beta;
        const auto wrong = geometry::moveBody("p", geometry::gear("p", xy, pinion), {20, 0, 0}, {{0, 0, 0}, {0, 0, 1}}, 0.0, false);
        CHECK(std::abs(geometry::boolean(geometry::BooleanOp::Common, crown, wrong).volume()) > 20.0);

        geometry::GearSpec rack;
        rack.type = geometry::GearType::Rack;
        rack.teeth = 10;
        rack.module = 2.0;
        rack.thickness = 10.0;
        rack.rim = 5.0;
        const double straightRack = geometry::gear("r", xy, rack).volume();
        rack.helixAngle = beta;
        rack.herringbone = herringbone;
        const auto bar = geometry::gear("r", xy, rack);
        REQUIRE(bar.isValid());
        CHECK(bar.volume() == Catch::Approx(straightRack).epsilon(0.001));  // inclinar los dientes no cambia el volumen
        CHECK((count(bar, ":m2") > 0) == herringbone);
        geometry::GearSpec wheel;
        wheel.teeth = 20;
        wheel.module = 2.0;
        wheel.thickness = 10.0;
        wheel.helixAngle = beta;  // la barra lleva el mismo ángulo y signo que la rueda
        wheel.herringbone = herringbone;
        const auto placed = geometry::moveBody("r", bar, {-5.0 * pi * 2.0, -20.0, 0}, {{0, 0, 0}, {0, 0, 1}}, 0.0, false);
        CHECK(std::abs(geometry::boolean(geometry::BooleanOp::Common, placed, geometry::gear("w", xy, wheel)).volume()) < 0.5);
        rack.helixAngle = -beta;
        const auto bad = geometry::moveBody("r", geometry::gear("r", xy, rack), {-5.0 * pi * 2.0, -20.0, 0}, {{0, 0, 0}, {0, 0, 1}}, 0.0, false);
        CHECK(std::abs(geometry::boolean(geometry::BooleanOp::Common, bad, geometry::gear("w", xy, wheel)).volume()) > 20.0);
    }
}

TEST_CASE("Los generadores de corona y de cremallera admiten hélice y espina de pescado y engranan", "[generator][gear][herringbone]")
{
    for (const char* id : {"engranaje-interno", "cremallera"}) {
        for (const double spine : {0.0, 1.0}) {
            INFO(id << " espina " << spine);
            const document::Generator* generator = document::findGenerator(id);
            REQUIRE(generator != nullptr);
            document::GeneratorValues values = generator->defaults();
            values["helice"] = 20.0;
            values["espina"] = spine;
            values["adelgazar"] = 0.0;
            values["chaflan_base"] = 0.0;
            document::Document doc;
            document::applyRecipe(doc, generator->make(values, ""));
            REQUIRE(doc.result().bodies.size() == 2);
            for (const auto& body : doc.result().bodies) {
                REQUIRE(body.shape.isValid());
            }
            const double common = geometry::boolean(geometry::BooleanOp::Common, doc.result().bodies[0].shape, doc.result().bodies[1].shape).volume();
            CHECK(std::abs(common) < 3.0);
        }
    }
}

TEST_CASE("Los cuerpos y componentes se numeran según lo que se hizo", "[bodies][generator]")
{
    document::Document doc;
    const document::Generator* pair = document::findGenerator("par-engranajes");
    const document::Generator* box = document::findGenerator("caja-con-tapa");
    REQUIRE(pair != nullptr);
    document::applyRecipe(doc, pair->make(pair->defaults(), ""));
    document::GeneratorValues second = pair->defaults();
    document::applyRecipe(doc, pair->make(second, document::freeSuffix(doc, *pair, second)));
    REQUIRE(doc.result().bodies.size() == 4);
    const auto info = [&](std::size_t i) { return doc.bodyInfo().at(doc.result().bodies[i].key); };
    CHECK(info(0).component == "Par de engranajes 1");
    CHECK(info(1).component == "Par de engranajes 1");
    CHECK(info(2).component == "Par de engranajes 2");
    CHECK(info(0).name == "Engranaje 1");
    CHECK(info(3).name == "Engranaje 2");
    if (box != nullptr) {
        document::applyRecipe(doc, box->make(box->defaults(), document::freeSuffix(doc, *box, box->defaults())));
        CHECK(info(4).component == "Caja con tapa 1");
        CHECK(info(4).name == "Caja");
        CHECK(info(5).name == "Tapa");
    }
}
