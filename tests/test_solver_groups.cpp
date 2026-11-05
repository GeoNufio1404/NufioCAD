#include <nuf/sketch/Sketch.hpp>
#include <nuf/sketch/Solver.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <set>

using namespace nuf::sketch;
using Catch::Matchers::WithinAbs;

namespace {

struct Rect {
    EntityId a, b, c, d;
};

// Un rectángulo cerrado de w×h con esquina en (x, y), horizontal/vertical y,
// si «sized», acotado en ancho y alto (totalmente restringido salvo posición).
Rect addRect(Sketch& s, double x, double y, double w, double h, bool sized)
{
    Rect r{s.addLine({x, y}, {x + w, y}), s.addLine({x + w, y}, {x + w, y + h}),
           s.addLine({x + w, y + h}, {x, y + h}), s.addLine({x, y + h}, {x, y})};
    s.addConstraint(Constraint::coincident(Ref::end(r.a), Ref::start(r.b)));
    s.addConstraint(Constraint::coincident(Ref::end(r.b), Ref::start(r.c)));
    s.addConstraint(Constraint::coincident(Ref::end(r.c), Ref::start(r.d)));
    s.addConstraint(Constraint::coincident(Ref::end(r.d), Ref::start(r.a)));
    s.addConstraint(Constraint::horizontal(r.a));
    s.addConstraint(Constraint::horizontal(r.c));
    s.addConstraint(Constraint::vertical(r.b));
    s.addConstraint(Constraint::vertical(r.d));
    if (sized) {
        s.addConstraint(Constraint::length(r.a, w));
        s.addConstraint(Constraint::length(r.b, h));
    }
    return r;
}

Vec2 startOf(const Sketch& s, EntityId id)
{
    return std::get<Line>(s.entity(id)->geometry).start;
}

} // namespace

TEST_CASE("Figuras independientes se resuelven por separado con el mismo resultado", "[solver][groups]")
{
    Sketch together;
    const Rect one = addRect(together, 0, 0, 10, 5, true);
    const Rect two = addRect(together, 30, 0, 8, 8, false);
    auto solver = makePlaneGcsSolver();
    const SolveResult both = solver->solve(together);
    REQUIRE(both.status == SolveStatus::Solved);

    // Los grados de libertad son los de cada una por su lado.
    Sketch alone;
    addRect(alone, 0, 0, 10, 5, true);
    Sketch other;
    addRect(other, 30, 0, 8, 8, false);
    const int expected = solver->solve(alone).degreesOfFreedom + solver->solve(other).degreesOfFreedom;
    REQUIRE(both.degreesOfFreedom == expected);

    // Solo la segunda (sin cotas) queda libre: la primera solo puede moverse entera.
    const std::set<EntityId> loose(both.underconstrained.begin(), both.underconstrained.end());
    REQUIRE(loose.contains(two.a));
    REQUIRE(loose.contains(two.b));
    (void)one;
}

TEST_CASE("Arrastrar mueve solo el grupo de lo que se arrastra y conserva lo demás", "[solver][groups]")
{
    Sketch s;
    const Rect one = addRect(s, 0, 0, 10, 5, false);
    const Rect two = addRect(s, 30, 0, 8, 8, false);
    auto solver = makePlaneGcsSolver();
    REQUIRE(solver->solve(s).status == SolveStatus::Solved);
    const Vec2 untouched = startOf(s, two.a);

    // Se estira el primer rectángulo desde su esquina inferior izquierda.
    Sketch moved = s;
    const SolveResult result = solver->drag(moved, {DragTarget{Ref::start(one.a), {-2.0, -3.0}}});
    REQUIRE(result.status == SolveStatus::Solved);
    REQUIRE_THAT(startOf(moved, one.a).x, WithinAbs(-2.0, 1e-4));
    REQUIRE_THAT(startOf(moved, one.a).y, WithinAbs(-3.0, 1e-4));
    // El otro rectángulo ni se enteró.
    REQUIRE(startOf(moved, two.a).x == untouched.x);
    REQUIRE(startOf(moved, two.a).y == untouched.y);
    // Y sigue marcado como sin restringir en el resultado del arrastre.
    const std::set<EntityId> loose(result.underconstrained.begin(), result.underconstrained.end());
    REQUIRE(loose.contains(two.a));
}

TEST_CASE("Un conflicto en un grupo deja todo el sketch como estaba", "[solver][groups]")
{
    Sketch s;
    addRect(s, 0, 0, 10, 5, true);
    const Rect bad = addRect(s, 30, 0, 8, 8, false);
    // Dos longitudes distintas para el mismo lado, y la horizontal ya lo hace imposible.
    s.addConstraint(Constraint::length(bad.a, 8.0));
    s.addConstraint(Constraint::length(bad.a, 9.0));
    const Sketch before = s;
    auto solver = makePlaneGcsSolver();
    const SolveResult result = solver->solve(s);
    REQUIRE(result.status == SolveStatus::Conflicting);
    REQUIRE_FALSE(result.conflicting.empty());
    // Ni la figura sana se movió.
    REQUIRE(startOf(s, bad.a).x == startOf(before, bad.a).x);
    REQUIRE(s.entities().size() == before.entities().size());
}

TEST_CASE("Entidades sin restricciones y restricciones con los ejes siguen funcionando", "[solver][groups]")
{
    Sketch s;
    const EntityId free = s.addLine({1, 1}, {5, 2});
    const EntityId onAxis = s.addLine({0, 0}, {6, 0.3});
    s.addConstraint(Constraint::horizontal(onAxis));
    s.addConstraint(Constraint::coincident(Ref::start(onAxis), Ref::of(Sketch::kOrigin)));
    auto solver = makePlaneGcsSolver();
    const SolveResult result = solver->solve(s);
    REQUIRE(result.status == SolveStatus::Solved);
    REQUIRE_THAT(std::get<Line>(s.entity(onAxis)->geometry).end.y, WithinAbs(0.0, 1e-6));
    // La suelta no se tocó y se reporta como sin restringir.
    REQUIRE(startOf(s, free).x == 1.0);
    REQUIRE(std::ranges::find(result.underconstrained, free) != result.underconstrained.end());
}
