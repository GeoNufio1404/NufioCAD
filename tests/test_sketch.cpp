#include <nuf/sketch/Sketch.hpp>
#include <nuf/sketch/Solver.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

using namespace nuf::sketch;
using Catch::Matchers::WithinAbs;

namespace {

constexpr double kTol = 1e-6;

void requirePoint(const Sketch& sketch, Ref ref, double x, double y)
{
    auto p = sketch.point(ref);
    REQUIRE(p.has_value());
    REQUIRE_THAT(p->x, WithinAbs(x, kTol));
    REQUIRE_THAT(p->y, WithinAbs(y, kTol));
}

bool contains(const std::vector<ConstraintId>& ids, ConstraintId id)
{
    return std::ranges::find(ids, id) != ids.end();
}

// Rectángulo dibujado «a mano» (torcido) con sus restricciones de forma:
// esquinas unidas, lados horizontales y verticales.
struct Rectangle {
    EntityId bottom, right, top, left;
};

Rectangle drawRectangle(Sketch& s)
{
    Rectangle r;
    r.bottom = s.addLine({0.3, -0.2}, {9.0, 0.4});
    r.right = s.addLine({9.2, 0.1}, {8.7, 5.5});
    r.top = s.addLine({9.1, 5.2}, {-0.4, 4.6});
    r.left = s.addLine({0.1, 5.0}, {0.2, 0.3});
    s.addConstraint(Constraint::coincident(Ref::end(r.bottom), Ref::start(r.right)));
    s.addConstraint(Constraint::coincident(Ref::end(r.right), Ref::start(r.top)));
    s.addConstraint(Constraint::coincident(Ref::end(r.top), Ref::start(r.left)));
    s.addConstraint(Constraint::coincident(Ref::end(r.left), Ref::start(r.bottom)));
    s.addConstraint(Constraint::horizontal(r.bottom));
    s.addConstraint(Constraint::horizontal(r.top));
    s.addConstraint(Constraint::vertical(r.left));
    s.addConstraint(Constraint::vertical(r.right));
    return r;
}

} // namespace

TEST_CASE("Un sketch nuevo trae el origen y los ejes", "[sketch]")
{
    Sketch s;
    REQUIRE(s.entities().size() == 3);
    requirePoint(s, Ref::of(Sketch::kOrigin), 0.0, 0.0);
    REQUIRE(s.entity(Sketch::kAxisX)->reference);
    REQUIRE_THROWS_AS(s.removeEntity(Sketch::kOrigin), std::invalid_argument);

    auto result = makePlaneGcsSolver()->solve(s);
    REQUIRE(result.status == SolveStatus::Solved);
    REQUIRE(result.degreesOfFreedom == 0);
}

TEST_CASE("Una línea libre tiene 4 grados de libertad", "[sketch]")
{
    Sketch s;
    EntityId line = s.addLine({0, 0}, {10, 5});

    auto result = makePlaneGcsSolver()->solve(s);
    REQUIRE(result.status == SolveStatus::Solved);
    REQUIRE(result.degreesOfFreedom == 4);
    REQUIRE(result.underconstrained == std::vector<EntityId>{line});

    s.addConstraint(Constraint::horizontal(line));
    result = makePlaneGcsSolver()->solve(s);
    REQUIRE(result.degreesOfFreedom == 3);
    REQUIRE_THAT(s.point(Ref::start(line))->y, WithinAbs(s.point(Ref::end(line))->y, kTol));
}

TEST_CASE("Rectángulo totalmente restringido desde el origen", "[sketch]")
{
    Sketch s;
    Rectangle r = drawRectangle(s);
    auto solver = makePlaneGcsSolver();

    auto result = solver->solve(s);
    REQUIRE(result.status == SolveStatus::Solved);
    REQUIRE(result.degreesOfFreedom == 4);  // posición, ancho y alto

    s.addConstraint(Constraint::coincident(Ref::start(r.bottom), Ref::of(Sketch::kOrigin)));
    s.addConstraint(Constraint::length(r.bottom, 40.0));
    s.addConstraint(Constraint::distance(Ref::start(r.left), Ref::end(r.left), 25.0));

    result = solver->solve(s);
    REQUIRE(result.status == SolveStatus::Solved);
    REQUIRE(result.degreesOfFreedom == 0);
    REQUIRE(result.underconstrained.empty());
    REQUIRE(result.conflicting.empty());
    REQUIRE(result.redundant.empty());

    requirePoint(s, Ref::start(r.bottom), 0.0, 0.0);
    requirePoint(s, Ref::end(r.bottom), 40.0, 0.0);
    requirePoint(s, Ref::end(r.right), 40.0, 25.0);
    requirePoint(s, Ref::end(r.top), 0.0, 25.0);
}

TEST_CASE("Cambiar una cota mueve la geometría", "[sketch]")
{
    Sketch s;
    Rectangle r = drawRectangle(s);
    s.addConstraint(Constraint::coincident(Ref::start(r.bottom), Ref::of(Sketch::kOrigin)));
    ConstraintId width = s.addConstraint(Constraint::length(r.bottom, 40.0));
    s.addConstraint(Constraint::length(r.left, 25.0));
    auto solver = makePlaneGcsSolver();
    REQUIRE(solver->solve(s).status == SolveStatus::Solved);

    s.constraint(width)->value = 60.0;
    REQUIRE(solver->solve(s).status == SolveStatus::Solved);
    requirePoint(s, Ref::end(r.right), 60.0, 25.0);
}

TEST_CASE("Restricciones en conflicto no mueven la geometría", "[sketch]")
{
    Sketch s;
    EntityId line = s.addLine({1, 1}, {8, 3});
    ConstraintId h = s.addConstraint(Constraint::horizontal(line));
    ConstraintId v = s.addConstraint(Constraint::vertical(line));
    ConstraintId len = s.addConstraint(Constraint::length(line, 10.0));

    auto result = makePlaneGcsSolver()->solve(s);
    REQUIRE(result.status == SolveStatus::Conflicting);
    REQUIRE_FALSE(result.conflicting.empty());
    for (ConstraintId id : result.conflicting) {
        REQUIRE((id == h || id == v || id == len));
    }
    requirePoint(s, Ref::end(line), 8.0, 3.0);
}

TEST_CASE("Una restricción repetida se marca como redundante", "[sketch]")
{
    Sketch s;
    EntityId line = s.addLine({0, 0}, {10, 1});
    ConstraintId first = s.addConstraint(Constraint::horizontal(line));
    ConstraintId second = s.addConstraint(Constraint::horizontal(line));

    auto result = makePlaneGcsSolver()->solve(s);
    REQUIRE(result.status == SolveStatus::Solved);
    REQUIRE((contains(result.redundant, first) || contains(result.redundant, second)));
}

TEST_CASE("Círculo tangente a una línea", "[sketch]")
{
    Sketch s;
    EntityId line = s.addLine({0, 0}, {20, 0});
    EntityId circle = s.addCircle({10, 7}, 3.0);
    s.addConstraint(Constraint::coincident(Ref::start(line), Ref::of(Sketch::kOrigin)));
    s.addConstraint(Constraint::horizontal(line));
    s.addConstraint(Constraint::radius(circle, 5.0));
    s.addConstraint(Constraint::tangent(line, circle));

    auto result = makePlaneGcsSolver()->solve(s);
    REQUIRE(result.status == SolveStatus::Solved);
    const auto& c = std::get<Circle>(s.entity(circle)->geometry);
    REQUIRE_THAT(c.radius, WithinAbs(5.0, kTol));
    // Queda del mismo lado de la línea en que se dibujó.
    REQUIRE_THAT(c.center.y, WithinAbs(5.0, kTol));
}

TEST_CASE("Arco unido a los extremos de una línea", "[sketch]")
{
    Sketch s;
    EntityId line = s.addLine({0, 0}, {10, 0});
    EntityId arc = s.addArc({5, -2.5}, 6.0, 0.5, std::numbers::pi - 0.5);
    s.addConstraint(Constraint::fix(Ref::of(line)));
    s.addConstraint(Constraint::coincident(Ref::start(arc), Ref::end(line)));
    s.addConstraint(Constraint::coincident(Ref::end(arc), Ref::start(line)));
    // No un semicírculo exacto: ahí la solución es una raíz doble y el
    // solver solo la alcanza con ~1e-5 de error.
    s.addConstraint(Constraint::radius(arc, 6.5));

    auto result = makePlaneGcsSolver()->solve(s);
    REQUIRE(result.status == SolveStatus::Solved);
    REQUIRE(result.degreesOfFreedom == 0);
    const auto& a = std::get<Arc>(s.entity(arc)->geometry);
    REQUIRE_THAT(a.center.x, WithinAbs(5.0, kTol));
    REQUIRE_THAT(a.center.y, WithinAbs(-std::sqrt(6.5 * 6.5 - 25.0), kTol));
    requirePoint(s, Ref::start(arc), 10.0, 0.0);
    requirePoint(s, Ref::end(arc), 0.0, 0.0);
}

TEST_CASE("Ángulo respecto al eje X", "[sketch]")
{
    Sketch s;
    EntityId line = s.addLine({0, 0}, {10, 3});
    s.addConstraint(Constraint::coincident(Ref::start(line), Ref::of(Sketch::kOrigin)));
    s.addConstraint(Constraint::length(line, 10.0));
    s.addConstraint(Constraint::angle(Sketch::kAxisX, line, std::numbers::pi / 6));

    auto result = makePlaneGcsSolver()->solve(s);
    REQUIRE(result.status == SolveStatus::Solved);
    REQUIRE(result.degreesOfFreedom == 0);
    requirePoint(s, Ref::end(line), 10.0 * std::cos(std::numbers::pi / 6), 5.0);
}

TEST_CASE("Punto medio y simetría", "[sketch]")
{
    Sketch s;
    EntityId line = s.addLine({-4, 1}, {6, 2});
    EntityId p = s.addPoint({3, 3});
    s.addConstraint(Constraint::fix(Ref::of(line)));
    s.addConstraint(Constraint::midpoint(Ref::of(p), line));
    REQUIRE(makePlaneGcsSolver()->solve(s).status == SolveStatus::Solved);
    requirePoint(s, Ref::of(p), 1.0, 1.5);

    Sketch t;
    EntityId a = t.addPoint({-3, 2});
    EntityId b = t.addPoint({4, 1});
    t.addConstraint(Constraint::fix(Ref::of(a)));
    t.addConstraint(Constraint::symmetric(Ref::of(a), Ref::of(b), Ref::of(Sketch::kAxisY)));
    REQUIRE(makePlaneGcsSolver()->solve(t).status == SolveStatus::Solved);
    requirePoint(t, Ref::of(b), 3.0, 2.0);
}

TEST_CASE("Las cotas de referencia siguen a la geometría", "[sketch]")
{
    Sketch s;
    EntityId line = s.addLine({0, 0}, {10, 0});
    s.addConstraint(Constraint::coincident(Ref::start(line), Ref::of(Sketch::kOrigin)));
    s.addConstraint(Constraint::horizontal(line));
    ConstraintId driving = s.addConstraint(Constraint::length(line, 30.0));

    Constraint ref = Constraint::distanceX(Ref::of(Sketch::kOrigin), Ref::end(line), 0.0);
    ref.driving = false;
    ConstraintId reference = s.addConstraint(ref);

    auto solver = makePlaneGcsSolver();
    auto result = solver->solve(s);
    REQUIRE(result.status == SolveStatus::Solved);
    REQUIRE(result.redundant.empty());  // la de referencia no restringe
    REQUIRE_THAT(s.constraint(reference)->value, WithinAbs(30.0, kTol));

    s.constraint(driving)->value = 12.5;
    solver->solve(s);
    REQUIRE_THAT(s.constraint(reference)->value, WithinAbs(12.5, kTol));
}

TEST_CASE("Argumentos no válidos se rechazan", "[sketch]")
{
    Sketch s;
    EntityId line = s.addLine({0, 0}, {1, 0});
    EntityId other = s.addLine({0, 1}, {1, 2});
    EntityId circle = s.addCircle({5, 5}, 1.0);

    REQUIRE_THROWS_AS(s.addConstraint(Constraint::radius(line, 2.0)), std::invalid_argument);
    REQUIRE_THROWS_AS(s.addConstraint(Constraint::radius(circle, -2.0)), std::invalid_argument);
    REQUIRE_THROWS_AS(s.addConstraint(Constraint::tangent(line, other)), std::invalid_argument);
    REQUIRE_THROWS_AS(s.addConstraint(Constraint::parallel(line, circle)), std::invalid_argument);
    REQUIRE_THROWS_AS(s.addConstraint(Constraint::coincident(Ref::of(line), Ref::start(other))),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(s.addConstraint(Constraint::horizontal(999)), std::invalid_argument);
    REQUIRE_THROWS_AS(s.addCircle({0, 0}, 0.0), std::invalid_argument);
    REQUIRE(s.constraints().empty());
}

TEST_CASE("Borrar una entidad borra sus restricciones", "[sketch]")
{
    Sketch s;
    EntityId a = s.addLine({0, 0}, {1, 0});
    EntityId b = s.addLine({1, 0}, {1, 1});
    s.addConstraint(Constraint::perpendicular(a, b));
    ConstraintId keep = s.addConstraint(Constraint::horizontal(a));

    s.removeEntity(b);
    REQUIRE(s.entity(b) == nullptr);
    REQUIRE(s.constraints().size() == 1);
    REQUIRE(s.constraints().front().id == keep);

    // Los ids no se reutilizan.
    REQUIRE(s.addPoint({0, 0}) > b);
}

TEST_CASE("Arrastrar respeta las restricciones", "[sketch][drag]")
{
    Sketch s;
    EntityId line = s.addLine({0, 0}, {10, 0});
    s.addConstraint(Constraint::coincident(Ref::start(line), Ref::of(Sketch::kOrigin)));
    s.addConstraint(Constraint::horizontal(line));
    auto solver = makePlaneGcsSolver();

    // El extremo libre sigue al cursor solo en X: la línea es horizontal.
    auto result = solver->drag(s, {{Ref::end(line), {25, 7}}});
    REQUIRE(result.status == SolveStatus::Solved);
    requirePoint(s, Ref::end(line), 25.0, 0.0);
    requirePoint(s, Ref::start(line), 0.0, 0.0);
    REQUIRE(result.degreesOfFreedom == 1);  // el arrastre no cuenta como restricción

    // Con la longitud acotada ya no se mueve.
    s.addConstraint(Constraint::length(line, 25.0));
    solver->drag(s, {{Ref::end(line), {40, 0}}});
    requirePoint(s, Ref::end(line), 25.0, 0.0);

    // El contorno de un círculo arrastrado cambia el radio.
    EntityId circle = s.addCircle({50, 50}, 5);
    s.addConstraint(Constraint::fix(Ref::center(circle)));
    solver->drag(s, {{Ref::of(circle), {50, 62}}});
    REQUIRE_THAT(std::get<Circle>(s.entity(circle)->geometry).radius, WithinAbs(12.0, 1e-6));
}
