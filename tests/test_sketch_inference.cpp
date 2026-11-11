#include <nuf/sketch/Sketch.hpp>
#include <nuf/sketch/Snap.hpp>
#include <nuf/sketch/Solver.hpp>
#include <nuf/sketch/Tools.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>

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
