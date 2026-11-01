// Herramientas de curvas (Fase 2b): elipse, arco de elipse, splines, cónica,
// ranura en arco y arco tangente arrastrando desde una línea.

#include <nuf/sketch/Profiles.hpp>
#include <nuf/sketch/Sketch.hpp>
#include <nuf/sketch/Snap.hpp>
#include <nuf/sketch/Solver.hpp>
#include <nuf/sketch/Tools.hpp>

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

struct Session {
    Sketch sketch;
    SnapOptions options{0.5, 0.0};

    Snap at(double x, double y) const { return snap(sketch, {x, y}, options); }
    bool click(SketchTool& tool, double x, double y)
    {
        const Snap s = at(x, y);
        tool.move(s);
        return tool.click(sketch, s);
    }
};

template <typename T>
const T& only(const Sketch& s)
{
    for (const Entity& e : s.entities()) {
        if (const auto* g = std::get_if<T>(&e.geometry); g && !e.reference && !e.construction) {
            return *g;
        }
    }
    throw std::runtime_error("No hay ninguna entidad de ese tipo");
}

double distanceToCurve(const BSpline& b, Vec2 p)
{
    double best = 1e9;
    for (int i = 0; i <= 100000; ++i) {
        const Vec2 q = b.pointAt(i / 100000.0);
        best = std::min(best, std::hypot(q.x - p.x, q.y - p.y));
    }
    return best;
}

SolveResult solve(Sketch& s)
{
    auto result = makePlaneGcsSolver()->solve(s);
    REQUIRE(result.status == SolveStatus::Solved);
    REQUIRE(result.conflicting.empty());
    REQUIRE(result.redundant.empty());
    return result;
}

} // namespace

TEST_CASE("Interpolación: la spline pasa por los puntos", "[sketch][curvetools]")
{
    const std::vector<Vec2> points{{0, 0}, {10, 12}, {25, 4}, {32, 18}, {40, 10}};
    const BSpline b = interpolatingSpline(points);
    REQUIRE(b.poles.size() == points.size());
    for (const Vec2& p : points) {
        REQUIRE(distanceToCurve(b, p) < 1e-3);
    }
    // Con dos puntos es una recta.
    REQUIRE(interpolatingSpline({{0, 0}, {5, 5}}).poles.size() == 2);
}

TEST_CASE("Elipse por centro, eje mayor y menor", "[sketch][curvetools]")
{
    Session ui;
    auto tool = makeEllipseTool();
    ui.click(*tool, 0, 0);    // origen
    ui.click(*tool, 10, 3);
    REQUIRE(ui.click(*tool, 3, 7));
    const Ellipse& e = only<Ellipse>(ui.sketch);
    REQUIRE_THAT(e.majorRadius, WithinAbs(std::hypot(10.0, 3.0), kTol));
    REQUIRE_THAT(e.rotation, WithinAbs(std::atan2(3.0, 10.0), kTol));
    REQUIRE(solve(ui.sketch).degreesOfFreedom == 3);

    // Si el «menor» sale más grande, se intercambian los ejes.
    Session ui2;
    auto t2 = makeEllipseTool();
    ui2.click(*t2, 0, 0);
    ui2.click(*t2, 5, 0);
    ui2.click(*t2, 3, 9);
    const Ellipse& f = only<Ellipse>(ui2.sketch);
    REQUIRE_THAT(f.majorRadius, WithinAbs(9.0, kTol));
    REQUIRE_THAT(f.minorRadius, WithinAbs(5.0, kTol));
}

TEST_CASE("Elipse con semiejes escritos", "[sketch][curvetools]")
{
    Session ui;
    auto tool = makeEllipseTool();
    ui.click(*tool, 0, 0);
    tool->move(ui.at(8, 0.6));
    REQUIRE_FALSE(tool->applyValues(ui.sketch, {20.0}));
    tool->move(ui.at(5, 6));
    REQUIRE(tool->applyValues(ui.sketch, {7.0}));
    solve(ui.sketch);
    const Ellipse& e = only<Ellipse>(ui.sketch);
    REQUIRE_THAT(e.majorRadius, WithinAbs(20.0, kTol));
    REQUIRE_THAT(e.minorRadius, WithinAbs(7.0, kTol));
}

TEST_CASE("Arco de elipse", "[sketch][curvetools]")
{
    Session ui;
    auto tool = makeEllipseArcTool();
    ui.click(*tool, 0, 0);
    ui.click(*tool, 10, 0);
    ui.click(*tool, 2, 5);
    ui.click(*tool, 10, 0.2);   // inicio ≈ ángulo 0
    REQUIRE(ui.click(*tool, -10, 0.2));  // final ≈ ángulo π
    const EllipseArc& a = only<EllipseArc>(ui.sketch);
    REQUIRE(a.endParam - a.startParam > 2.5);
    REQUIRE(a.endParam - a.startParam < 3.5);
    solve(ui.sketch);
}

TEST_CASE("Spline por polos y por puntos de paso", "[sketch][curvetools]")
{
    SECTION("Por polos")
    {
        Session ui;
        auto tool = makeSplineTool(SplineMode::ControlPoints);
        for (auto [x, y] : {std::pair{0.0, 0.0}, {5.0, 9.0}, {14.0, 9.0}, {20.0, 2.0}}) {
            ui.click(*tool, x, y);
        }
        REQUIRE(tool->finish(ui.sketch));
        const BSpline& b = only<BSpline>(ui.sketch);
        REQUIRE(b.poles.size() == 4);
        REQUIRE(solve(ui.sketch).degreesOfFreedom == 6);  // 8 − 2 (empieza en el origen)
    }
    SECTION("Por puntos de paso")
    {
        Session ui;
        auto tool = makeSplineTool(SplineMode::FitPoints);
        const std::vector<Vec2> fit{{2, 1}, {8, 10}, {16, 3}, {24, 11}};
        for (const Vec2& p : fit) {
            ui.click(*tool, p.x, p.y);
        }
        REQUIRE(tool->finish(ui.sketch));
        solve(ui.sketch);
        const BSpline& b = only<BSpline>(ui.sketch);
        for (const Vec2& p : fit) {
            REQUIRE(distanceToCurve(b, p) < 1e-3);
        }
        // Los puntos intermedios quedan como puntos de construcción sobre la curva.
        REQUIRE(std::ranges::count(ui.sketch.constraints(), ConstraintType::PointOnObject, &Constraint::type) == 2);
    }
    SECTION("Un solo punto no basta")
    {
        Session ui;
        auto tool = makeSplineTool(SplineMode::FitPoints);
        ui.click(*tool, 3, 3);
        REQUIRE_FALSE(tool->finish(ui.sketch));
    }
}

TEST_CASE("Cónica con rho de circunferencia", "[sketch][curvetools]")
{
    Session ui;
    auto tool = makeConicTool();
    ui.click(*tool, 10, 1);
    ui.click(*tool, 1, 10);
    tool->move(ui.at(10, 10));
    // rho = w / (1 + w) con w = √2/2: cuarto de circunferencia de radio 9.
    const double w = std::numbers::sqrt2 / 2.0;
    REQUIRE(tool->applyValues(ui.sketch, {w / (1.0 + w)}));
    const BSpline& b = only<BSpline>(ui.sketch);
    for (double u : {0.2, 0.5, 0.9}) {
        const Vec2 p = b.pointAt(u);
        REQUIRE_THAT(std::hypot(p.x - 1, p.y - 1), WithinAbs(9.0, 1e-9));
    }
    solve(ui.sketch);
}

TEST_CASE("Ranura en arco", "[sketch][curvetools]")
{
    Session ui;
    auto tool = makeArcSlotTool();
    ui.click(*tool, 20, 3);
    ui.click(*tool, -20, 3);
    ui.click(*tool, 0, 23);
    tool->move(ui.at(0, 26));
    REQUIRE(tool->applyValues(ui.sketch, {6.0}));
    auto result = solve(ui.sketch);
    REQUIRE(result.degreesOfFreedom == 5);  // centro, radio y dos ángulos (el ancho está escrito)
    auto profiles = findProfiles(ui.sketch);
    REQUIRE(profiles.size() == 1);
}

TEST_CASE("Línea: arrastrar desde el final dibuja un arco tangente", "[sketch][curvetools]")
{
    Session ui;
    auto tool = makeLineTool();
    ui.click(*tool, 0, 5);
    ui.click(*tool, 20, 5);  // línea horizontal
    const Snap from = ui.at(20, 5);
    REQUIRE(tool->dragMove(from, ui.at(30, 15)));
    REQUIRE(tool->preview().geometry.size() == 1);
    REQUIRE(tool->dragRelease(ui.sketch, from, ui.at(30, 15)));
    const Arc& a = only<Arc>(ui.sketch);
    REQUIRE_THAT(a.radius, WithinAbs(10.0, kTol));
    REQUIRE_THAT(a.center.y, WithinAbs(15.0, kTol));
    solve(ui.sketch);
    // La cadena sigue: la siguiente línea sale del final del arco.
    REQUIRE(ui.click(*tool, 30, 30));
    solve(ui.sketch);

    // Arrastrar lejos del punto actual no hace nada.
    REQUIRE_FALSE(tool->dragMove(ui.at(0, 0), ui.at(5, 5)));
}
