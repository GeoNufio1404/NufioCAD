#include <nuf/sketch/Sketch.hpp>
#include <nuf/sketch/Snap.hpp>
#include <nuf/sketch/Solver.hpp>
#include <nuf/sketch/Tools.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <string>
#include <variant>
#include <vector>

using namespace nuf::sketch;
using Catch::Matchers::WithinAbs;

namespace {

void clickAt(SketchTool& tool, Sketch& sketch, Vec2 p)
{
    SnapOptions options;
    options.enabled = false;  // sin snap: el punto tal cual
    const Snap at = snap(sketch, p, options);
    tool.move(at);
    tool.click(sketch, at);
}

std::size_t countOf(const Sketch& s, bool circles)
{
    std::size_t n = 0;
    for (const Entity& e : s.entities()) {
        const bool isCircle = std::holds_alternative<Circle>(e.geometry);
        const bool isLine = std::holds_alternative<Line>(e.geometry);
        if (!e.reference && !e.construction && (circles ? isCircle : isLine)) {
            ++n;
        }
    }
    return n;
}

} // namespace

TEST_CASE("Figuras abiertas: el círculo sigue editable tras el último clic", "[sketch][open]")
{
    Sketch s;
    auto tool = makeCircleTool();
    clickAt(*tool, s, {0, 0});
    clickAt(*tool, s, {5, 0});
    REQUIRE(countOf(s, true) == 1);
    // Sigue mostrando el diámetro, con su valor actual.
    REQUIRE(tool->valueFields() == std::vector<std::string>{"Diámetro"});
    REQUIRE_THAT(tool->currentValues().at(0), WithinAbs(10.0, 1e-9));
    // Escribir otro diámetro cambia el círculo, no añade otro.
    REQUIRE(tool->applyValues(s, {16.0}));
    REQUIRE(countOf(s, true) == 1);
    REQUIRE(makePlaneGcsSolver()->solve(s).status == SolveStatus::Solved);
    for (const Entity& e : s.entities()) {
        if (const auto* c = std::get_if<Circle>(&e.geometry); c && !e.reference) {
            REQUIRE_THAT(c->radius, WithinAbs(8.0, 1e-6));
        }
    }
    // Enter sin valores la deja como está y cierra la edición.
    REQUIRE_FALSE(tool->applyValues(s, {std::nullopt}));
    REQUIRE(countOf(s, true) == 1);
    REQUIRE(tool->valueFields().empty());
}

TEST_CASE("Figuras abiertas: un clic nuevo empieza otra figura y la anterior queda", "[sketch][open]")
{
    Sketch s;
    auto tool = makeCircleTool();
    clickAt(*tool, s, {0, 0});
    clickAt(*tool, s, {5, 0});
    clickAt(*tool, s, {30, 0});  // centro de la segunda
    clickAt(*tool, s, {34, 0});
    REQUIRE(countOf(s, true) == 2);
    REQUIRE_THAT(tool->currentValues().at(0), WithinAbs(8.0, 1e-9));  // edita la segunda
}

TEST_CASE("Figuras abiertas: Esc cierra la figura abierta y el siguiente Esc sale", "[sketch][open]")
{
    Sketch s;
    auto tool = makeCircleTool();
    clickAt(*tool, s, {0, 0});
    clickAt(*tool, s, {5, 0});
    REQUIRE(tool->cancel());        // cierra la edición (sigue en la herramienta)
    REQUIRE(countOf(s, true) == 1); // la figura se queda
    REQUIRE_FALSE(tool->cancel());  // ahora sí sale
}

TEST_CASE("Figuras abiertas: el rectángulo cambia de ancho y alto", "[sketch][open]")
{
    for (const bool centered : {false, true}) {
        Sketch s;
        auto tool = centered ? makeCenterRectangleTool() : makeRectangleTool();
        clickAt(*tool, s, {0, 0});
        clickAt(*tool, s, {10, 6});
        REQUIRE(countOf(s, false) == 4);
        const auto now = tool->currentValues();
        REQUIRE(now.size() == 2);
        REQUIRE_THAT(now[0], WithinAbs(centered ? 20.0 : 10.0, 1e-9));
        REQUIRE(tool->applyValues(s, {std::nullopt, 12.0}));  // solo el alto
        REQUIRE(countOf(s, false) == 4);
        REQUIRE(makePlaneGcsSolver()->solve(s).status == SolveStatus::Solved);
        double top = -1e9;
        double bottom = 1e9;
        for (const Entity& e : s.entities()) {
            if (const auto* l = std::get_if<Line>(&e.geometry); l && !e.reference && !e.construction) {
                top = std::max({top, l->start.y, l->end.y});
                bottom = std::min({bottom, l->start.y, l->end.y});
            }
        }
        REQUIRE_THAT(top - bottom, WithinAbs(12.0, 1e-6));
        // El ancho se conserva.
        REQUIRE_THAT(tool->currentValues().at(0), WithinAbs(centered ? 20.0 : 10.0, 1e-6));
    }
}

TEST_CASE("Figuras abiertas: el polígono cambia el número de lados y el tamaño", "[sketch][open]")
{
    for (const PolygonMode mode : {PolygonMode::Inscribed, PolygonMode::Circumscribed, PolygonMode::Edge}) {
        Sketch s;
        auto tool = makePolygonTool(mode);
        clickAt(*tool, s, {0, 0});
        clickAt(*tool, s, {10, 0});
        REQUIRE(countOf(s, false) == 6);
        REQUIRE(tool->valueFields().size() == 2);
        // Más lados, mismo tamaño.
        REQUIRE(tool->applyValues(s, {8.0, std::nullopt}));
        REQUIRE(countOf(s, false) == 8);
        // Otro tamaño, mismos lados.
        const double before = tool->currentValues().at(1);
        REQUIRE(tool->applyValues(s, {std::nullopt, before * 2.0}));
        REQUIRE(countOf(s, false) == 8);
        REQUIRE_THAT(tool->currentValues().at(1), WithinAbs(before * 2.0, 1e-9));
        REQUIRE(makePlaneGcsSolver()->solve(s).status == SolveStatus::Solved);
        // Cerrar con Enter y que la figura siga completa.
        REQUIRE_FALSE(tool->applyValues(s, {std::nullopt, std::nullopt}));
        REQUIRE(countOf(s, false) == 8);
    }
}

TEST_CASE("Figuras abiertas: tras deshacer, la figura abierta se suelta sin romper nada", "[sketch][open]")
{
    Sketch s;
    auto tool = makeCircleTool();
    const Sketch empty = s;
    clickAt(*tool, s, {0, 0});
    clickAt(*tool, s, {5, 0});
    s = empty;  // «deshacer»
    REQUIRE_FALSE(tool->applyValues(s, {20.0}));
    REQUIRE(countOf(s, true) == 0);
    REQUIRE(tool->valueFields().empty());
}

TEST_CASE("Figuras abiertas: círculo por 2 y por 3 puntos", "[sketch][open]")
{
    {
        Sketch s;
        auto tool = makeTwoPointCircleTool();
        clickAt(*tool, s, {0, 0});
        clickAt(*tool, s, {10, 0});
        REQUIRE(countOf(s, true) == 1);
        REQUIRE_THAT(tool->currentValues().at(0), WithinAbs(10.0, 1e-9));
        REQUIRE(tool->applyValues(s, {20.0}));
        REQUIRE(countOf(s, true) == 1);
        REQUIRE(tool->amendedLast());
        REQUIRE_THAT(tool->currentValues().at(0), WithinAbs(20.0, 1e-9));
    }
    {
        Sketch s;
        auto tool = makeThreePointCircleTool();
        clickAt(*tool, s, {10, 0});
        clickAt(*tool, s, {0, 10});
        clickAt(*tool, s, {-10, 0});
        REQUIRE(countOf(s, true) == 1);
        REQUIRE_THAT(tool->currentValues().at(0), WithinAbs(20.0, 1e-9));
        REQUIRE(tool->applyValues(s, {30.0}));
        REQUIRE(countOf(s, true) == 1);
        REQUIRE(makePlaneGcsSolver()->solve(s).status == SolveStatus::Solved);
        REQUIRE_FALSE(tool->applyValues(s, {std::nullopt}));
        REQUIRE(tool->valueFields().empty());
    }
}

TEST_CASE("Figuras abiertas: rectángulo por 3 puntos y ranura", "[sketch][open]")
{
    {
        Sketch s;
        auto tool = makeThreePointRectangleTool();
        clickAt(*tool, s, {0, 0});
        clickAt(*tool, s, {10, 0});
        clickAt(*tool, s, {10, 4});
        REQUIRE(countOf(s, false) == 4);
        const auto now = tool->currentValues();
        REQUIRE_THAT(now.at(0), WithinAbs(10.0, 1e-9));
        REQUIRE_THAT(now.at(1), WithinAbs(4.0, 1e-9));
        REQUIRE(tool->applyValues(s, {std::nullopt, 9.0}));
        REQUIRE(countOf(s, false) == 4);
        REQUIRE(tool->amendedLast());
        REQUIRE_THAT(tool->currentValues().at(1), WithinAbs(9.0, 1e-9));
        REQUIRE_THAT(tool->currentValues().at(0), WithinAbs(10.0, 1e-9));
    }
    {
        Sketch s;
        auto tool = makeSlotTool(SlotMode::CenterToCenter);
        clickAt(*tool, s, {0, 0});
        clickAt(*tool, s, {20, 0});
        clickAt(*tool, s, {20, 3});  // ancho 6
        REQUIRE(countOf(s, false) == 2);
        REQUIRE_THAT(tool->currentValues().at(0), WithinAbs(6.0, 1e-9));
        REQUIRE(tool->applyValues(s, {10.0}));
        REQUIRE(countOf(s, false) == 2);  // los dos lados, sin duplicar
        REQUIRE_THAT(tool->currentValues().at(0), WithinAbs(10.0, 1e-9));
        REQUIRE_FALSE(tool->applyValues(s, {std::nullopt}));
    }
}

TEST_CASE("Figuras abiertas: la elipse sigue editable y se rehace con otros semiejes", "[sketch][open]")
{
    Sketch s;
    auto tool = makeEllipseTool();
    clickAt(*tool, s, {0, 0});
    clickAt(*tool, s, {10, 0});
    clickAt(*tool, s, {0, 4});
    REQUIRE(tool->valueFields() == std::vector<std::string>{"Semieje mayor", "Semieje menor"});
    REQUIRE_THAT(tool->currentValues().at(0), WithinAbs(10.0, 1e-9));
    REQUIRE_THAT(tool->currentValues().at(1), WithinAbs(4.0, 1e-9));

    // Solo se cambia el menor: el mayor se queda; sigue habiendo una sola elipse.
    REQUIRE(tool->applyValues(s, {std::nullopt, 6.0}));
    REQUIRE(tool->amendedLast());
    REQUIRE(makePlaneGcsSolver()->solve(s).status == SolveStatus::Solved);
    std::size_t ellipses = 0;
    for (const Entity& e : s.entities()) {
        if (const auto* el = std::get_if<Ellipse>(&e.geometry); el && !e.reference) {
            ++ellipses;
            REQUIRE_THAT(el->majorRadius, WithinAbs(10.0, 1e-6));
            REQUIRE_THAT(el->minorRadius, WithinAbs(6.0, 1e-6));
        }
    }
    REQUIRE(ellipses == 1);

    // Enter sin valores la deja como está y cierra la edición.
    REQUIRE_FALSE(tool->applyValues(s, {std::nullopt, std::nullopt}));
    REQUIRE(tool->valueFields().empty());
}

TEST_CASE("Línea con un círculo o arco lejano: tangente y perpendicular", "[sketch][inference]")
{
    const auto run = [](Vec2 start, Vec2 cursor, bool commit) {
        struct Result {
            std::string badge;
            std::vector<ConstraintType> made;
            Vec2 end{};
        } result;
        Sketch s;
        const EntityId circle = s.addCircle({0.0, 0.0}, 5.0);
        auto tool = makeLineTool();
        tool->setAwakeLines({AwakeLine{circle, {}, Circle{{0.0, 0.0}, 5.0}}});
        SnapOptions options;
        options.enabled = false;
        const Snap first = snap(s, start, options);
        tool->move(first);
        tool->click(s, first);
        Snap second = snap(s, cursor, options);
        second.free = false;  // como el cursor normal: sin Ctrl
        tool->move(second);
        const Preview preview = tool->preview();
        result.badge = preview.badge;
        if (!preview.geometry.empty()) {
            if (const auto* line = std::get_if<Line>(&preview.geometry.front())) {
                result.end = line->end;
            }
        }
        if (commit) {
            tool->click(s, second);
            for (const Constraint& c : s.constraints()) {
                result.made.push_back(c.type);
            }
        }
        return result;
    };

    // Hacia el centro, a 1° de la recta: perpendicular al círculo (la recta pasa por el centro).
    // (Desde (20, 10) la dirección al centro no es horizontal ni vertical, así que no la endereza otra regla.)
    const double toward = std::atan2(-10.0, -20.0) + 1.0 * std::numbers::pi / 180.0;
    const auto radial = run({20.0, 10.0}, {20.0 + 10.0 * std::cos(toward), 10.0 + 10.0 * std::sin(toward)}, true);
    REQUIRE(radial.badge == "⊥");
    // El tramo queda exactamente sobre la recta que pasa por el centro (y = x / 2).
    REQUIRE_THAT(radial.end.y, WithinAbs(radial.end.x / 2.0, 1e-9));
    REQUIRE(std::ranges::find(radial.made, ConstraintType::PointOnObject) != radial.made.end());

    // Por la tangente (el ángulo de la tangente desde (20, 0) a un círculo de radio 5 es asin(5/20)).
    const double half = std::asin(5.0 / 20.0);
    const double dx = -std::cos(half);
    const double dy = std::sin(half);
    const auto tangent = run({20.0, 0.0}, {20.0 + 10.0 * dx, 10.0 * dy + 0.2}, true);
    REQUIRE(tangent.badge == "T");
    REQUIRE(std::ranges::find(tangent.made, ConstraintType::Tangent) != tangent.made.end());

    // Lejos de ambas direcciones no infiere nada.
    const auto none = run({20.0, 0.0}, {10.0, 6.0}, false);
    REQUIRE(none.badge.empty());
}
