// Elipses, arcos de elipse y splines (Fase 2b): modelo, solver y perfiles.

#include <nuf/sketch/Profiles.hpp>
#include <nuf/sketch/Sketch.hpp>
#include <nuf/sketch/Solver.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <numbers>

using namespace nuf::sketch;
using Catch::Matchers::WithinAbs;

namespace {

constexpr double kTol = 1e-6;
constexpr double kPi = std::numbers::pi;

double dist(Vec2 a, Vec2 b) { return std::hypot(b.x - a.x, b.y - a.y); }

// ¿Está p sobre la elipse? (ecuación en ejes propios)
double ellipseResidual(const Ellipse& e, Vec2 p)
{
    const double dx = p.x - e.center.x;
    const double dy = p.y - e.center.y;
    const double c = std::cos(e.rotation);
    const double s = std::sin(e.rotation);
    const double x = dx * c + dy * s;
    const double y = -dx * s + dy * c;
    return x * x / (e.majorRadius * e.majorRadius) + y * y / (e.minorRadius * e.minorRadius) - 1.0;
}

SolveResult solve(Sketch& s)
{
    auto result = makePlaneGcsSolver()->solve(s);
    REQUIRE(result.status == SolveStatus::Solved);
    REQUIRE(result.conflicting.empty());
    REQUIRE(result.redundant.empty());
    return result;
}

// Curvatura con signo de una B-spline en su inicio (por diferencias finitas).
double startCurvature(const BSpline& b)
{
    const double h = 1e-4;
    const Vec2 p0 = b.pointAt(0.0);
    const Vec2 p1 = b.pointAt(h);
    const Vec2 p2 = b.pointAt(2 * h);
    const Vec2 d1{(p1.x - p0.x) / h, (p1.y - p0.y) / h};
    const Vec2 d2{(p2.x - 2 * p1.x + p0.x) / (h * h), (p2.y - 2 * p1.y + p0.y) / (h * h)};
    return (d1.x * d2.y - d1.y * d2.x) / std::pow(std::hypot(d1.x, d1.y), 3);
}

} // namespace

TEST_CASE("Spline: evaluación de Bézier y de cónica racional", "[sketch][curves]")
{
    BSpline cubic{{{0, 0}, {1, 2}, {3, 2}, {4, 0}}, {}, 3};
    REQUIRE_THAT(cubic.pointAt(0).x, WithinAbs(0.0, kTol));
    REQUIRE_THAT(cubic.pointAt(1).x, WithinAbs(4.0, kTol));
    // Un solo tramo cúbico = Bézier: B(½) = (P0 + 3P1 + 3P2 + P3) / 8.
    REQUIRE_THAT(cubic.pointAt(0.5).x, WithinAbs((0 + 3 + 9 + 4) / 8.0, kTol));
    REQUIRE_THAT(cubic.pointAt(0.5).y, WithinAbs((0 + 6 + 6 + 0) / 8.0, kTol));

    // Varios tramos: sigue pasando por los extremos.
    BSpline longer{{{0, 0}, {1, 3}, {2, -1}, {4, 2}, {5, 0}, {7, 1}}, {}, 3};
    REQUIRE(longer.knots().size() == 4);
    REQUIRE_THAT(longer.pointAt(1).x, WithinAbs(7.0, kTol));

    // Cuarto de circunferencia exacto como cuadrática racional.
    BSpline conic{{{1, 0}, {1, 1}, {0, 1}}, {1.0, std::numbers::sqrt2 / 2.0, 1.0}, 2};
    for (double u : {0.1, 0.37, 0.5, 0.8}) {
        REQUIRE_THAT(dist(conic.pointAt(u), {0, 0}), WithinAbs(1.0, 1e-12));
    }
}

TEST_CASE("Elipse: cotas de semiejes en el solver", "[sketch][curves]")
{
    Sketch s;
    EntityId e = s.addEllipse({{3, 2}, 10, 4, 0.3});
    REQUIRE(solve(s).degreesOfFreedom == 5);

    s.addConstraint(Constraint::coincident(Ref::center(e), Ref::of(Sketch::kOrigin)));
    s.addConstraint(Constraint::majorRadius(e, 15));
    s.addConstraint(Constraint::minorRadius(e, 6));
    auto result = solve(s);
    REQUIRE(result.degreesOfFreedom == 1);  // queda el giro
    const auto& el = std::get<Ellipse>(s.entity(e)->geometry);
    REQUIRE_THAT(el.majorRadius, WithinAbs(15.0, kTol));
    REQUIRE_THAT(el.minorRadius, WithinAbs(6.0, kTol));
    REQUIRE_THAT(el.center.x, WithinAbs(0.0, kTol));
}

TEST_CASE("Elipse: punto sobre ella y recta tangente", "[sketch][curves]")
{
    Sketch s;
    EntityId e = s.addEllipse({{0, 0}, 10, 5, 0.0});
    s.addConstraint(Constraint::fix(Ref::of(e)));
    EntityId p = s.addPoint({4, 7});
    s.addConstraint(Constraint::pointOnObject(Ref::of(p), e));
    EntityId line = s.addLine({-20, 7}, {20, 7});
    s.addConstraint(Constraint::horizontal(line));
    s.addConstraint(Constraint::tangent(line, e));
    solve(s);
    const auto& el = std::get<Ellipse>(s.entity(e)->geometry);
    REQUIRE_THAT(ellipseResidual(el, *s.point(Ref::of(p))), WithinAbs(0.0, 1e-6));
    // Tangente horizontal por arriba: y = semieje menor.
    REQUIRE_THAT(s.point(Ref::start(line))->y, WithinAbs(5.0, 1e-6));
}

TEST_CASE("Arco de elipse unido a una línea", "[sketch][curves]")
{
    Sketch s;
    EntityId arc = s.addEllipseArc({{{0, 0}, 8, 4, 0.0}, 0.0, kPi});
    EntityId base = s.addLine({-8.5, 0.3}, {8.4, -0.2});
    s.addConstraint(Constraint::coincident(Ref::start(arc), Ref::end(base)));
    s.addConstraint(Constraint::coincident(Ref::end(arc), Ref::start(base)));
    solve(s);
    const auto& a = std::get<EllipseArc>(s.entity(arc)->geometry);
    REQUIRE_THAT(dist(a.startPoint(), *s.point(Ref::end(base))), WithinAbs(0.0, 1e-6));
    REQUIRE(findProfiles(s).size() == 1);
}

TEST_CASE("Spline: punto sobre ella con polos y tangencia en el extremo", "[sketch][curves]")
{
    Sketch s;
    EntityId spline = s.addBSpline({{{0, 0}, {5, 8}, {12, 8}, {18, 0}}, {}, 3});
    EntityId p = s.addPoint({9, 7});
    s.addConstraint(Constraint::pointOnObject(Ref::of(p), spline));
    solve(s);
    // El punto queda sobre la curva (distancia a su polilínea fina).
    const auto& b = std::get<BSpline>(s.entity(spline)->geometry);
    double best = 1e9;
    for (int i = 0; i <= 200000; ++i) {
        best = std::min(best, dist(b.pointAt(i / 200000.0), *s.point(Ref::of(p))));
    }
    REQUIRE(best < 1e-4);

    // Línea que entra tangente al inicio de la spline.
    EntityId line = s.addLine({-10, -3}, {0, 0});
    s.addConstraint(Constraint::horizontal(line));
    s.addConstraint(Constraint::tangentAt(Ref::end(line), Ref::start(spline)));
    s.addConstraint(Constraint::fix(Ref::pole(spline, 2)));
    const Vec2 fixedPole = std::get<BSpline>(s.entity(spline)->geometry).poles[2];
    solve(s);
    const auto& t = std::get<BSpline>(s.entity(spline)->geometry);
    // Tangente = primer tramo del polígono de control: horizontal.
    REQUIRE_THAT(t.poles[1].y - t.poles[0].y, WithinAbs(0.0, 1e-6));
    REQUIRE_THAT(t.poles[2].x, WithinAbs(fixedPole.x, 1e-12));  // el polo fijo no se mueve
    REQUIRE_THAT(t.poles[2].y, WithinAbs(fixedPole.y, 1e-12));
}

TEST_CASE("Curvatura G2: recta-spline y arco-spline", "[sketch][curves]")
{
    SECTION("Tras una recta, curvatura cero (tres primeros polos alineados)")
    {
        Sketch s;
        EntityId line = s.addLine({-10, 0}, {0, 0});
        s.addConstraint(Constraint::fix(Ref::of(line)));
        EntityId spline = s.addBSpline({{{0, 0}, {4, 1}, {8, 4}, {12, 9}, {15, 15}}, {}, 3});
        s.addConstraint(Constraint::curvatureAt(Ref::end(line), Ref::start(spline)));
        solve(s);
        const auto& b = std::get<BSpline>(s.entity(spline)->geometry);
        REQUIRE_THAT(b.poles[1].y, WithinAbs(0.0, 1e-6));
        REQUIRE_THAT(b.poles[2].y, WithinAbs(0.0, 1e-6));
        REQUIRE_THAT(startCurvature(b), WithinAbs(0.0, 1e-3));
    }
    SECTION("Tras un arco, la misma curvatura que el arco")
    {
        Sketch s;
        // Arco de radio 10 que termina en (10, 0) subiendo (sentido antihorario).
        EntityId arc = s.addArc({0, 0}, 10, -kPi / 2, 0.0);
        s.addConstraint(Constraint::fix(Ref::of(arc)));
        EntityId spline = s.addBSpline({{{10, 0}, {10, 3}, {9, 7}, {6, 11}, {2, 13}}, {}, 3});
        s.addConstraint(Constraint::curvatureAt(Ref::end(arc), Ref::start(spline)));
        solve(s);
        const auto& b = std::get<BSpline>(s.entity(spline)->geometry);
        REQUIRE_THAT(startCurvature(b), WithinAbs(0.1, 2e-3));
    }
}

TEST_CASE("Perfiles con elipse y con spline", "[sketch][curves]")
{
    Sketch s;
    s.addEllipse({{0, 0}, 10, 4, 0.5});
    auto profiles = findProfiles(s);
    REQUIRE(profiles.size() == 1);
    REQUIRE_THAT(profiles[0].area, WithinAbs(kPi * 40.0, 1e-9));

    // Forma de «D» con una spline como lado curvo.
    Sketch d;
    d.addLine({0, 0}, {0, 20});
    d.addBSpline({{{0, 20}, {15, 20}, {15, 0}, {0, 0}}, {}, 3});
    REQUIRE(findProfiles(d).size() == 1);
}
