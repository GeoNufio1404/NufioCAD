#include <nuf/sketch/Pick.hpp>

#include <catch2/catch_test_macros.hpp>

#include <numbers>

using namespace nuf::sketch;

TEST_CASE("Pick: puntos antes que curvas", "[sketch][pick]")
{
    Sketch s;
    EntityId line = s.addLine({10, 10}, {30, 10});
    EntityId circle = s.addCircle({0, 40}, 5);
    EntityId arc = s.addArc({50, 0}, 10, 0.0, std::numbers::pi / 2);

    Ref end = pick(s, {29.6, 10.3}, 1.0);
    REQUIRE(end.entity == line);
    REQUIRE(end.part == Part::End);

    Ref body = pick(s, {20, 10.5}, 1.0);
    REQUIRE(body.entity == line);
    REQUIRE(body.part == Part::Whole);

    REQUIRE(pick(s, {0, 45.4}, 1.0).entity == circle);
    REQUIRE(pick(s, {0.2, 40.1}, 1.0).part == Part::Center);

    // Dentro del barrido del arco (45°) sí; en la parte que falta (225°) no.
    const double d = 10.0 / std::numbers::sqrt2;
    REQUIRE(pick(s, {50 + d, d}, 1.0).entity == arc);
    REQUIRE(pick(s, {50 - d, -d}, 1.0).empty());
}

TEST_CASE("Pick: origen y ejes como rectas infinitas", "[sketch][pick]")
{
    Sketch s;
    REQUIRE(pick(s, {0.3, -0.2}, 1.0).entity == Sketch::kOrigin);
    // Lejos de los extremos (0,0)-(1,0) del eje X, pero sobre la recta.
    REQUIRE(pick(s, {250.0, 0.4}, 1.0).entity == Sketch::kAxisX);
    REQUIRE(pick(s, {-0.4, -80.0}, 1.0).entity == Sketch::kAxisY);
    REQUIRE(pick(s, {20.0, 20.0}, 1.0).empty());
}
