#include <nuf/sketch/Sketch.hpp>
#include <nuf/sketch/Snap.hpp>
#include <nuf/sketch/Solver.hpp>
#include <nuf/sketch/Tools.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>

using namespace nuf::sketch;
using Catch::Matchers::WithinAbs;

namespace {

SnapOptions alignedTo(std::vector<AlignPoint> points, double tolerance = 0.5)
{
    SnapOptions options;
    options.tolerance = 0.5;
    options.alignPoints = std::move(points);
    options.alignTolerance = tolerance;
    return options;
}

} // namespace

TEST_CASE("Inferencias: el cursor se pega a la horizontal de un punto y dibuja la guía", "[sketch][inference]")
{
    Sketch s;
    const EntityId p = s.addPoint({0, 5});
    const Snap snapped = snap(s, {20, 5.3}, alignedTo({{{0, 5}, Ref::of(p)}}));
    REQUIRE(snapped.kind == SnapKind::None);
    REQUIRE_THAT(snapped.point.y, WithinAbs(5.0, 1e-12));
    REQUIRE_THAT(snapped.point.x, WithinAbs(20.0, 1e-12));
    REQUIRE(snapped.alignY == Ref::of(p));
    REQUIRE(snapped.alignX.empty());
    REQUIRE(snapped.guides.size() == 1);
    REQUIRE_THAT(snapped.guides.front().from.x, WithinAbs(0.0, 1e-12));
}

TEST_CASE("Inferencias: lejos de la recta no hay alineación, y sin tolerancia tampoco", "[sketch][inference]")
{
    Sketch s;
    const EntityId p = s.addPoint({0, 5});
    REQUIRE(snap(s, {20, 7}, alignedTo({{{0, 5}, Ref::of(p)}})).guides.empty());
    REQUIRE(snap(s, {20, 5.1}, alignedTo({{{0, 5}, Ref::of(p)}}, 0.0)).guides.empty());
}

TEST_CASE("Inferencias: dos puntos de referencia dan el cruce de sus rectas", "[sketch][inference]")
{
    Sketch s;
    const EntityId a = s.addPoint({0, 5});
    const EntityId b = s.addPoint({12, 30});
    const Snap snapped = snap(s, {12.2, 5.2}, alignedTo({{{0, 5}, Ref::of(a)}, {{12, 30}, Ref::of(b)}}));
    REQUIRE_THAT(snapped.point.x, WithinAbs(12.0, 1e-12));
    REQUIRE_THAT(snapped.point.y, WithinAbs(5.0, 1e-12));
    REQUIRE(snapped.alignY == Ref::of(a));
    REQUIRE(snapped.alignX == Ref::of(b));
    REQUIRE(snapped.guides.size() == 2);
}

TEST_CASE("Inferencias: un punto existente cerca gana a la alineación", "[sketch][inference]")
{
    Sketch s;
    const EntityId a = s.addPoint({0, 5});
    const EntityId target = s.addPoint({20, 5.2});
    const Snap snapped = snap(s, {20.1, 5.3}, alignedTo({{{0, 5}, Ref::of(a)}}));
    REQUIRE(snapped.kind == SnapKind::Point);
    REQUIRE(snapped.ref == Ref::of(target));
    REQUIRE(snapped.guides.empty());
}

TEST_CASE("Inferencias: Ctrl (sin snap) no alinea", "[sketch][inference]")
{
    Sketch s;
    const EntityId a = s.addPoint({0, 5});
    SnapOptions options = alignedTo({{{0, 5}, Ref::of(a)}});
    options.enabled = false;
    const Snap snapped = snap(s, {20, 5.3}, options);
    REQUIRE(snapped.free);
    REQUIRE(snapped.guides.empty());
}

TEST_CASE("Inferencias: una línea que acaba alineada con un punto crea la restricción entre ambos", "[sketch][inference]")
{
    Sketch s;
    const EntityId p = s.addPoint({0, 5});
    const SnapOptions options = alignedTo({{{0, 5}, Ref::of(p)}});
    auto tool = makeLineTool();
    const Snap start = snap(s, {3, 20}, options);
    tool->move(start);
    tool->click(s, start);
    const Snap end = snap(s, {30, 5.2}, options);
    REQUIRE(end.alignY == Ref::of(p));
    tool->move(end);
    REQUIRE(tool->click(s, end));
    REQUIRE(std::ranges::count(s.constraints(), ConstraintType::Horizontal, &Constraint::type) == 1);
    auto solver = makePlaneGcsSolver();
    const SolveResult result = solver->solve(s);
    REQUIRE(result.status == SolveStatus::Solved);
    REQUIRE(result.conflicting.empty());
    const auto lines = std::ranges::count_if(s.entities(), [](const Entity& e) { return std::holds_alternative<Line>(e.geometry); });
    REQUIRE(lines >= 1);
    const Entity& line = s.entities().back();
    REQUIRE_THAT(std::get<Line>(line.geometry).end.y, WithinAbs(5.0, 1e-6));
}

TEST_CASE("Inferencias: alinear con el mismo inicio de la línea solo la hace horizontal", "[sketch][inference]")
{
    Sketch s;
    const EntityId p = s.addPoint({0, 5});
    const SnapOptions options = alignedTo({{{0, 5}, Ref::of(p)}});
    auto tool = makeLineTool();
    const Snap start = snap(s, {3, 5}, options);  // ya alineado con el punto
    tool->move(start);
    tool->click(s, start);
    const Snap end = snap(s, {30, 5.2}, options);
    tool->move(end);
    REQUIRE(tool->click(s, end));
    // La línea es horizontal; no se repite la restricción con el punto de referencia.
    const auto horizontals = std::ranges::count(s.constraints(), ConstraintType::Horizontal, &Constraint::type);
    REQUIRE(horizontals >= 1);
    auto solver = makePlaneGcsSolver();
    const SolveResult result = solver->solve(s);
    REQUIRE(result.conflicting.empty());
    REQUIRE(result.redundant.empty());
}

namespace {

// Dos líneas encadenadas: la segunda sale del final de la primera.
Sketch chainWithSecondEnd(Vec2 firstEnd, Vec2 secondEnd)
{
    Sketch s;
    SnapOptions options;
    options.tolerance = 0.5;
    auto tool = makeLineTool();
    for (Vec2 p : {Vec2{0, 0}, firstEnd, secondEnd}) {
        const Snap at = snap(s, p, options);
        tool->move(at);
        tool->click(s, at);
    }
    return s;
}

} // namespace

TEST_CASE("Inferencias: la segunda línea de una cadena a casi 90° queda perpendicular y con su restricción", "[sketch][inference]")
{
    // Primera línea inclinada 30°; la segunda sale a ~89° respecto a ella.
    const Vec2 firstEnd{20 * 0.8660254, 20 * 0.5};
    const double angle = (30.0 + 89.0) * 3.14159265358979 / 180.0;
    Sketch s = chainWithSecondEnd(firstEnd, {firstEnd.x + 10 * std::cos(angle), firstEnd.y + 10 * std::sin(angle)});
    REQUIRE(std::ranges::count(s.constraints(), ConstraintType::Perpendicular, &Constraint::type) == 1);
    auto solver = makePlaneGcsSolver();
    REQUIRE(solver->solve(s).status == SolveStatus::Solved);
    const auto& lines = s.entities();
    const Line& a = std::get<Line>(lines[lines.size() - 2].geometry);
    const Line& b = std::get<Line>(lines.back().geometry);
    const double dot = (a.end.x - a.start.x) * (b.end.x - b.start.x) + (a.end.y - a.start.y) * (b.end.y - b.start.y);
    REQUIRE_THAT(dot, WithinAbs(0.0, 1e-6));
}

TEST_CASE("Inferencias: la segunda línea casi paralela a la primera queda paralela", "[sketch][inference]")
{
    // Primera línea a 30°; la segunda sigue a ~31° (misma dirección).
    const Vec2 firstEnd{20 * 0.8660254, 20 * 0.5};
    const double angle = 31.0 * 3.14159265358979 / 180.0;
    Sketch s = chainWithSecondEnd(firstEnd, {firstEnd.x + 10 * std::cos(angle), firstEnd.y + 10 * std::sin(angle)});
    REQUIRE(std::ranges::count(s.constraints(), ConstraintType::Parallel, &Constraint::type) == 1);
    auto solver = makePlaneGcsSolver();
    REQUIRE(solver->solve(s).status == SolveStatus::Solved);
}

TEST_CASE("Inferencias: un ángulo lejos de 0° y 90° no infiere nada, y la vista previa lo avisa solo si infiere", "[sketch][inference]")
{
    const Vec2 firstEnd{20 * 0.8660254, 20 * 0.5};
    const double angle = (30.0 + 60.0 - 40.0) * 3.14159265358979 / 180.0;  // 50° respecto a la primera: ni ∥ ni ⊥
    Sketch s = chainWithSecondEnd(firstEnd, {firstEnd.x + 10 * std::cos(angle), firstEnd.y + 10 * std::sin(angle)});
    REQUIRE(std::ranges::count(s.constraints(), ConstraintType::Perpendicular, &Constraint::type) == 0);
    REQUIRE(std::ranges::count(s.constraints(), ConstraintType::Parallel, &Constraint::type) == 0);

    Sketch t;
    SnapOptions options;
    options.tolerance = 0.5;
    auto tool = makeLineTool();
    for (Vec2 p : {Vec2{0, 0}, firstEnd}) {
        const Snap at = snap(t, p, options);
        tool->move(at);
        tool->click(t, at);
    }
    const double near90 = (30.0 + 90.5) * 3.14159265358979 / 180.0;
    tool->move(snap(t, {firstEnd.x + 10 * std::cos(near90), firstEnd.y + 10 * std::sin(near90)}, options));
    REQUIRE(!tool->preview().badge.empty());
    REQUIRE(tool->preview().guides.size() == 1);
    tool->move(snap(t, {firstEnd.x + 10 * std::cos(angle), firstEnd.y + 10 * std::sin(angle)}, options));
    REQUIRE(tool->preview().badge.empty());
}
