// Tornillo sin fin y su rueda (E4).
#include <nuf/document/Document.hpp>
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
