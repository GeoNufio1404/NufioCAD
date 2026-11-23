#include <nuf/sketch/Sketch.hpp>
#include <nuf/sketch/Snap.hpp>
#include <nuf/sketch/Tools.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>

using namespace nuf::sketch;
using Catch::Matchers::WithinAbs;

namespace {

SnapOptions pointSnapOptions()
{
    SnapOptions result;
    result.tolerance = 0.5;
    result.gridStep = 1.0;
    result.enabled = true;
    return result;
}

std::size_t countPoints(const Sketch& sketch)
{
    return std::ranges::count_if(sketch.entities(), [](const Entity& e) {
        return !e.reference && std::holds_alternative<Point>(e.geometry);
    });
}

} // namespace

TEST_CASE("Herramienta Punto: cada clic deja un punto y la herramienta sigue activa", "[sketch][tools][point]")
{
    Sketch sketch;
    const SnapOptions options = pointSnapOptions();
    auto tool = makePointTool();

    const Snap first = snap(sketch, {10.0, 5.0}, options);
    tool->move(first);
    REQUIRE(tool->click(sketch, first));
    const Snap second = snap(sketch, {-4.0, 7.0}, options);
    tool->move(second);
    REQUIRE(tool->click(sketch, second));

    REQUIRE(countPoints(sketch) == 2);
    // Sin nada a medias: Esc devuelve false y la interfaz sale de la herramienta.
    REQUIRE_FALSE(tool->cancel());
}

TEST_CASE("Herramienta Punto: sobre una curva queda unido a ella, y sobre otro punto no se repite", "[sketch][tools][point]")
{
    Sketch sketch;
    const SnapOptions options = pointSnapOptions();
    const EntityId line = sketch.addLine({0.0, 0.0}, {20.0, 0.0});
    auto tool = makePointTool();

    // Sobre la línea (no en una esquina de la rejilla ni en un extremo): punto sobre objeto.
    const Snap onLine = snap(sketch, {6.3, 0.2}, options);
    REQUIRE(onLine.kind == SnapKind::OnCurve);
    REQUIRE(tool->click(sketch, onLine));
    REQUIRE(countPoints(sketch) == 1);
    const auto onObject = std::ranges::count_if(sketch.constraints(), [&](const Constraint& c) {
        return c.type == ConstraintType::PointOnObject && c.second == Ref::of(line);
    });
    REQUIRE(onObject == 1);

    // Sobre el extremo de la línea ya hay un punto: no se pone otro encima.
    const Snap atEnd = snap(sketch, {20.0, 0.1}, options);
    REQUIRE(atEnd.kind == SnapKind::Point);
    REQUIRE_FALSE(tool->click(sketch, atEnd));
    REQUIRE(countPoints(sketch) == 1);
}
