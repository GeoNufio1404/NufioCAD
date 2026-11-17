// Variantes de creación (Fase 2b): geometría, restricciones y grados de libertad.

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

using namespace nuf::sketch;

// Opciones del snap con nombre de campo: un inicializador agregado parcial ({0.5, 1.0}) hace que GCC avise de los
// campos que faltan (-Wmissing-field-initializers) y, con -Werror, el CI de Linux no compila.
static SnapOptions snapOptions(double tolerance, double gridStep, bool enabled = true)
{
    SnapOptions result;
    result.tolerance = tolerance;
    result.gridStep = gridStep;
    result.enabled = enabled;
    return result;
}
using Catch::Matchers::WithinAbs;

namespace {

constexpr double kTol = 1e-6;

struct Session {
    Sketch sketch;
    SnapOptions options = snapOptions(0.5, 0.0);

    Snap at(double x, double y) const { return snap(sketch, {x, y}, options); }
    bool click(SketchTool& tool, double x, double y)
    {
        const Snap s = at(x, y);
        tool.move(s);
        return tool.click(sketch, s);
    }
    void move(SketchTool& tool, double x, double y) { tool.move(at(x, y)); }
};

std::vector<EntityId> created(const Sketch& s, bool includeConstruction = false)
{
    std::vector<EntityId> ids;
    for (const Entity& e : s.entities()) {
        if (!e.reference && (includeConstruction || !e.construction)) {
            ids.push_back(e.id);
        }
    }
    return ids;
}

SolveResult solve(Sketch& s)
{
    auto result = makePlaneGcsSolver()->solve(s);
    REQUIRE(result.status == SolveStatus::Solved);
    REQUIRE(result.conflicting.empty());
    REQUIRE(result.redundant.empty());
    return result;
}

double lineLength(const Sketch& s, EntityId id)
{
    const auto& l = std::get<Line>(s.entity(id)->geometry);
    return std::hypot(l.end.x - l.start.x, l.end.y - l.start.y);
}

} // namespace

TEST_CASE("Rectángulo por 3 puntos (girado)", "[sketch][tools2b]")
{
    Session ui;
    auto tool = makeThreePointRectangleTool();
    ui.click(*tool, 0, 0);
    ui.click(*tool, 30, 40);       // primer lado: 50 de largo, girado
    REQUIRE(ui.click(*tool, 26, 43));  // altura hacia la izquierda: 5
    auto lines = created(ui.sketch);
    REQUIRE(lines.size() == 4);
    REQUIRE_THAT(lineLength(ui.sketch, lines[0]), WithinAbs(50.0, kTol));
    REQUIRE_THAT(lineLength(ui.sketch, lines[1]), WithinAbs(5.0, kTol));
    REQUIRE(solve(ui.sketch).degreesOfFreedom == 3);  // 5 − 2 (empieza en el origen)
    REQUIRE(findProfiles(ui.sketch).size() == 1);
}

TEST_CASE("Rectángulo por 3 puntos con medidas escritas", "[sketch][tools2b]")
{
    Session ui;
    auto tool = makeThreePointRectangleTool();
    ui.click(*tool, 0, 0);
    ui.move(*tool, 30, 1.0);  // casi horizontal (y fuera del alcance del eje X)
    REQUIRE_FALSE(tool->applyValues(ui.sketch, {40.0}));
    ui.move(*tool, 20, 7);
    REQUIRE(tool->applyValues(ui.sketch, {25.0}));
    // Empieza en el origen, horizontal, 40 × 25: totalmente definido.
    REQUIRE(solve(ui.sketch).degreesOfFreedom == 0);
}

TEST_CASE("Rectángulo por centro", "[sketch][tools2b]")
{
    Session ui;
    auto tool = makeCenterRectangleTool();
    ui.click(*tool, 0.1, -0.1);  // centro en el origen
    ui.move(*tool, 10, 5);
    REQUIRE(tool->applyValues(ui.sketch, {40.0, 20.0}));
    auto lines = created(ui.sketch);
    REQUIRE(lines.size() == 4);
    REQUIRE_THAT(lineLength(ui.sketch, lines[0]), WithinAbs(40.0, kTol));
    REQUIRE(solve(ui.sketch).degreesOfFreedom == 0);
    REQUIRE_THAT(ui.sketch.point(Ref::start(lines[0]))->x, WithinAbs(-20.0, kTol));
}

TEST_CASE("Círculo por 2 y por 3 puntos", "[sketch][tools2b]")
{
    Session ui;
    auto two = makeTwoPointCircleTool();
    ui.click(*two, -10, 0);
    REQUIRE(ui.click(*two, 10, 0));
    const auto& c = std::get<Circle>(ui.sketch.entity(created(ui.sketch)[0])->geometry);
    REQUIRE_THAT(c.radius, WithinAbs(10.0, kTol));
    REQUIRE_THAT(c.center.x, WithinAbs(0.0, kTol));

    Session ui3;
    auto three = makeThreePointCircleTool();
    ui3.click(*three, 5, 0);
    ui3.click(*three, 0, 5);
    REQUIRE(ui3.click(*three, -5, 0));
    const auto& c3 = std::get<Circle>(ui3.sketch.entity(created(ui3.sketch)[0])->geometry);
    REQUIRE_THAT(c3.radius, WithinAbs(5.0, kTol));

    // Apoyado en puntos existentes: quedan sobre el círculo.
    Session ui4;
    EntityId p = ui4.sketch.addPoint({20, 0});
    auto through = makeThreePointCircleTool();
    ui4.click(*through, 20.1, 0.1);
    ui4.click(*through, 0, 20);
    ui4.click(*through, -20, 0);
    REQUIRE(std::ranges::count(ui4.sketch.constraints(), ConstraintType::PointOnObject, &Constraint::type) == 1);
    (void)p;
}

TEST_CASE("Círculo tangente a 2 y a 3 líneas", "[sketch][tools2b]")
{
    Session ui;
    EntityId floor = ui.sketch.addLine({-50, 0}, {50, 0});
    EntityId wall = ui.sketch.addLine({30, -10}, {30, 50});
    EntityId roof = ui.sketch.addLine({-50, 20}, {50, 20});
    for (EntityId id : {floor, wall, roof}) {
        ui.sketch.addConstraint(Constraint::fix(Ref::of(id)));
    }

    auto two = makeTangentCircleTool(2);
    ui.click(*two, -20, 0.1);   // suelo
    ui.click(*two, 30.1, 30);   // pared
    REQUIRE(ui.click(*two, 20, 8));
    solve(ui.sketch);
    const EntityId first = created(ui.sketch).back();
    const auto& c = std::get<Circle>(ui.sketch.entity(first)->geometry);
    REQUIRE_THAT(c.center.y, WithinAbs(c.radius, 1e-6));            // toca el suelo
    REQUIRE_THAT(30.0 - c.center.x, WithinAbs(c.radius, 1e-6));     // y la pared

    auto three = makeTangentCircleTool(3);
    ui.click(*three, -20, 0.1);
    ui.click(*three, 30.1, 10);
    ui.click(*three, -20, 19.9);
    REQUIRE(ui.click(*three, 10, 10));
    REQUIRE(solve(ui.sketch).degreesOfFreedom == 1);  // el primero aún puede cambiar de radio
    const auto& d = std::get<Circle>(ui.sketch.entity(created(ui.sketch).back())->geometry);
    REQUIRE_THAT(d.radius, WithinAbs(10.0, 1e-6));
    REQUIRE_THAT(d.center.x, WithinAbs(20.0, 1e-6));
}

TEST_CASE("Arco por centro en ambos sentidos", "[sketch][tools2b]")
{
    for (const double turn : {1.0, -1.0}) {
        Session ui;
        auto tool = makeCenterArcTool();
        ui.click(*tool, 0, 0);
        ui.click(*tool, 10, 0);
        // El cursor gira un cuarto de vuelta en un sentido u otro.
        for (int i = 1; i <= 9; ++i) {
            const double a = turn * i * std::numbers::pi / 18.0;
            ui.move(*tool, 10 * std::cos(a), 10 * std::sin(a));
        }
        REQUIRE(ui.click(*tool, 0, turn * 10));
        const auto& arc = std::get<Arc>(ui.sketch.entity(created(ui.sketch)[0])->geometry);
        REQUIRE_THAT(arc.endAngle - arc.startAngle, WithinAbs(std::numbers::pi / 2, 1e-9));
        REQUIRE_THAT(arc.startPoint().y + arc.endPoint().y, WithinAbs(turn * 10, 1e-9));
        // Centro en el origen e inicio sobre el eje X (fija el ángulo inicial):
        // quedan el radio y el ángulo final.
        REQUIRE(solve(ui.sketch).degreesOfFreedom == 2);
    }
}

TEST_CASE("Arco tangente que continúa una línea", "[sketch][tools2b]")
{
    Session ui;
    EntityId line = ui.sketch.addLine({0, 0}, {20, 0});
    auto tool = makeTangentArcTool();
    REQUIRE_FALSE(ui.click(*tool, 5, 3));     // no es un extremo: no empieza
    REQUIRE(tool->preview().geometry.empty());
    ui.click(*tool, 20.1, 0.1);               // final de la línea
    REQUIRE(ui.click(*tool, 20, 20));         // media vuelta hacia arriba
    const EntityId arc = created(ui.sketch).back();
    const auto& a = std::get<Arc>(ui.sketch.entity(arc)->geometry);
    REQUIRE_THAT(a.radius, WithinAbs(10.0, kTol));
    REQUIRE_THAT(a.center.y, WithinAbs(10.0, kTol));
    solve(ui.sketch);
    // Sigue encadenado: otro arco tangente desde el final del primero.
    REQUIRE(ui.click(*tool, 10, 30));
    solve(ui.sketch);
    (void)line;
}

TEST_CASE("Polígonos: inscrito, circunscrito y por lado", "[sketch][tools2b]")
{
    SECTION("Hexágono inscrito")
    {
        Session ui;
        auto tool = makePolygonTool(PolygonMode::Inscribed);
        ui.click(*tool, 0, 0);
        REQUIRE(ui.click(*tool, 10, 0));
        REQUIRE(created(ui.sketch).size() == 6);
        REQUIRE_THAT(lineLength(ui.sketch, created(ui.sketch)[0]), WithinAbs(10.0, kTol));
        // Centro en el origen y un vértice en (10, 0) apoyado en el eje X.
        REQUIRE(solve(ui.sketch).degreesOfFreedom == 1);
        REQUIRE(findProfiles(ui.sketch).size() == 1);
    }
    SECTION("Cuadrado circunscrito con lados escritos")
    {
        Session ui;
        auto tool = makePolygonTool(PolygonMode::Circumscribed);
        REQUIRE_FALSE(tool->applyValues(ui.sketch, {4.0}));
        ui.click(*tool, 0, 0);
        ui.move(*tool, 8, 0);
        REQUIRE(tool->applyValues(ui.sketch, {std::nullopt, 20.0}));
        REQUIRE(created(ui.sketch).size() == 4);
        REQUIRE_THAT(lineLength(ui.sketch, created(ui.sketch)[0]), WithinAbs(20.0, kTol));
        REQUIRE(solve(ui.sketch).degreesOfFreedom == 1);  // solo el giro
    }
    SECTION("Triángulo por lado")
    {
        Session ui;
        auto tool = makePolygonTool(PolygonMode::Edge);
        tool->applyValues(ui.sketch, {3.0});
        ui.click(*tool, 0, 0);
        REQUIRE(ui.click(*tool, 12, 0));
        auto lines = created(ui.sketch);
        REQUIRE(lines.size() == 3);
        for (EntityId id : lines) {
            REQUIRE_THAT(lineLength(ui.sketch, id), WithinAbs(12.0, kTol));
        }
        // Primer vértice en el origen y el segundo sobre el eje X: queda el tamaño.
        REQUIRE(solve(ui.sketch).degreesOfFreedom == 1);
    }
}

TEST_CASE("Ranuras", "[sketch][tools2b]")
{
    SECTION("Centro a centro con ancho escrito")
    {
        Session ui;
        auto tool = makeSlotTool(SlotMode::CenterToCenter);
        ui.click(*tool, 0, 0);
        ui.click(*tool, 30, 0.3);  // se endereza
        ui.move(*tool, 15, 4);
        REQUIRE(tool->applyValues(ui.sketch, {10.0}));
        REQUIRE(created(ui.sketch).size() == 4);
        // Origen, horizontal y ancho: queda la distancia entre centros.
        REQUIRE(solve(ui.sketch).degreesOfFreedom == 1);
        auto profiles = findProfiles(ui.sketch);
        REQUIRE(profiles.size() == 1);
        REQUIRE_THAT(profiles[0].area, WithinAbs(30.0 * 10.0 + std::numbers::pi * 25.0, 0.5));
    }
    SECTION("Por longitud total")
    {
        Session ui;
        auto tool = makeSlotTool(SlotMode::Overall);
        ui.click(*tool, -20, 7);
        ui.click(*tool, 20, 7);
        REQUIRE(ui.click(*tool, 0, 12));
        const auto& top = std::get<Line>(ui.sketch.entity(created(ui.sketch)[0])->geometry);
        REQUIRE_THAT(std::abs(top.end.x - top.start.x), WithinAbs(30.0, kTol));  // 40 − 2 × 5
        solve(ui.sketch);
    }
    SECTION("Desde el punto central")
    {
        Session ui;
        auto tool = makeSlotTool(SlotMode::CenterPoint);
        ui.click(*tool, 0, 0);
        ui.click(*tool, 0.2, 15);  // se endereza en vertical
        REQUIRE(ui.click(*tool, 4, 0));
        auto result = solve(ui.sketch);
        REQUIRE(result.degreesOfFreedom == 2);  // largo y ancho
    }
}
