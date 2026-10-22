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
using Catch::Matchers::WithinAbs;

namespace {

constexpr double kTol = 1e-9;

// Simula el uso desde la UI: cada clic pasa por el snap, como en la app.
struct Session {
    Sketch sketch;
    SnapOptions options{0.5, 1.0};

    Snap at(double x, double y) const { return snap(sketch, {x, y}, options); }

    bool click(SketchTool& tool, double x, double y)
    {
        const Snap s = at(x, y);
        tool.move(s);
        return tool.click(sketch, s);
    }
};

std::size_t count(const Sketch& s, ConstraintType type)
{
    return std::ranges::count(s.constraints(), type, &Constraint::type);
}

std::vector<EntityId> created(const Sketch& s)
{
    std::vector<EntityId> ids;
    for (const Entity& e : s.entities()) {
        if (!e.reference) {
            ids.push_back(e.id);
        }
    }
    return ids;
}

} // namespace

TEST_CASE("Snap: puntos antes que la rejilla", "[sketch][snap]")
{
    Sketch s;
    EntityId line = s.addLine({2.3, 4.1}, {10.0, 4.1});
    SnapOptions options{0.5, 1.0};

    Snap end = snap(s, {2.5, 4.0}, options);
    REQUIRE(end.kind == SnapKind::Point);
    REQUIRE(end.ref.entity == line);
    REQUIRE(end.ref.part == Part::Start);
    REQUIRE_THAT(end.point.x, WithinAbs(2.3, kTol));

    Snap origin = snap(s, {0.2, -0.3}, options);
    REQUIRE(origin.kind == SnapKind::Point);
    REQUIRE(origin.ref.entity == Sketch::kOrigin);

    Snap mid = snap(s, {6.3, 4.3}, options);
    REQUIRE(mid.kind == SnapKind::Midpoint);
    REQUIRE(mid.ref.entity == line);
    REQUIRE_THAT(mid.point.x, WithinAbs(6.15, kTol));

    // El extremo (1, 0) del eje X no es un punto real: se ajusta a la rejilla.
    Snap grid = snap(s, {1.1, 0.2}, options);
    REQUIRE(grid.kind == SnapKind::Grid);
    REQUIRE_THAT(grid.point.x, WithinAbs(1.0, kTol));
    REQUIRE_THAT(grid.point.y, WithinAbs(0.0, kTol));

    Snap free = snap(s, {7.3, -2.6}, SnapOptions{0.5, 0.0});
    REQUIRE(free.kind == SnapKind::None);
    REQUIRE_THAT(free.point.x, WithinAbs(7.3, kTol));
}

TEST_CASE("Línea: polilínea unida enderezada y cerrada", "[sketch][tools]")
{
    Session ui;
    auto tool = makeLineTool();

    REQUIRE_FALSE(ui.click(*tool, 0.1, 0.1));   // origen
    REQUIRE(ui.click(*tool, 20.2, 0.6));        // casi horizontal → (20, 0)
    REQUIRE(ui.click(*tool, 20.4, 15.0));       // casi vertical → (20, 15)
    REQUIRE(ui.click(*tool, 0.2, -0.1));        // cierra en el origen

    auto lines = created(ui.sketch);
    REQUIRE(lines.size() == 3);
    REQUIRE(count(ui.sketch, ConstraintType::Horizontal) == 1);
    REQUIRE(count(ui.sketch, ConstraintType::Vertical) == 1);
    // origen-L1, L1-L2, L2-L3, L3-origen
    REQUIRE(count(ui.sketch, ConstraintType::Coincident) == 4);
    REQUIRE_THAT(ui.sketch.point(Ref::end(lines[0]))->y, WithinAbs(0.0, kTol));
    REQUIRE_THAT(ui.sketch.point(Ref::end(lines[1]))->x, WithinAbs(20.0, kTol));

    // Al cerrar la cadena no queda nada a medias.
    REQUIRE_FALSE(tool->cancel());

    auto result = makePlaneGcsSolver()->solve(ui.sketch);
    REQUIRE(result.status == SolveStatus::Solved);
    REQUIRE(result.conflicting.empty());
    REQUIRE(result.degreesOfFreedom == 2);  // ancho y alto del triángulo
}

TEST_CASE("Línea: Esc termina la cadena", "[sketch][tools]")
{
    Session ui;
    auto tool = makeLineTool();
    ui.click(*tool, 5.0, 5.0);
    ui.click(*tool, 12.0, 9.0);
    REQUIRE(tool->cancel());
    REQUIRE_FALSE(tool->cancel());
    REQUIRE(tool->preview().geometry.empty());

    // Un clic en el mismo punto no crea una línea de longitud cero.
    ui.click(*tool, 5.0, 5.0);
    REQUIRE_FALSE(ui.click(*tool, 5.0, 5.0));
    REQUIRE(created(ui.sketch).size() == 1);
}

TEST_CASE("Rectángulo por dos esquinas", "[sketch][tools]")
{
    Session ui;
    auto tool = makeRectangleTool();
    ui.click(*tool, 0.0, 0.0);  // origen
    REQUIRE(tool->preview().geometry.empty() == false);
    REQUIRE(ui.click(*tool, 30.0, 20.0));

    REQUIRE(created(ui.sketch).size() == 4);
    auto result = makePlaneGcsSolver()->solve(ui.sketch);
    REQUIRE(result.status == SolveStatus::Solved);
    REQUIRE(result.redundant.empty());
    REQUIRE(result.degreesOfFreedom == 2);  // ancho y alto
}

TEST_CASE("Círculo por centro y radio", "[sketch][tools]")
{
    Session ui;
    auto tool = makeCircleTool();
    ui.click(*tool, 0.2, 0.1);  // centro en el origen
    REQUIRE(ui.click(*tool, 8.0, 6.0));

    const auto& c = std::get<Circle>(ui.sketch.entity(created(ui.sketch)[0])->geometry);
    REQUIRE_THAT(c.radius, WithinAbs(10.0, kTol));
    auto result = makePlaneGcsSolver()->solve(ui.sketch);
    REQUIRE(result.degreesOfFreedom == 1);  // solo el radio
}

TEST_CASE("Arco por 3 puntos en ambos sentidos", "[sketch][tools]")
{
    SECTION("El tercer punto arriba: arco antihorario de inicio a fin")
    {
        Session ui;
        auto tool = makeArcTool();
        ui.click(*tool, 10.0, 0.0);
        ui.click(*tool, -10.0, 0.0);
        REQUIRE(ui.click(*tool, 0.0, 10.0));
        const EntityId id = created(ui.sketch)[0];
        const auto& a = std::get<Arc>(ui.sketch.entity(id)->geometry);
        REQUIRE_THAT(a.radius, WithinAbs(10.0, 1e-9));
        REQUIRE_THAT(ui.sketch.point(Ref::start(id))->x, WithinAbs(10.0, 1e-9));
        REQUIRE_THAT(ui.sketch.point(Ref::end(id))->x, WithinAbs(-10.0, 1e-9));
    }
    SECTION("El tercer punto abajo: el arco se invierte")
    {
        Session ui;
        auto tool = makeArcTool();
        ui.click(*tool, 10.0, 0.0);
        ui.click(*tool, -10.0, 0.0);
        REQUIRE(ui.click(*tool, 0.0, -10.0));
        const EntityId id = created(ui.sketch)[0];
        const auto& a = std::get<Arc>(ui.sketch.entity(id)->geometry);
        REQUIRE_THAT(ui.sketch.point(Ref::start(id))->x, WithinAbs(-10.0, 1e-9));
        REQUIRE_THAT(ui.sketch.point(Ref::end(id))->x, WithinAbs(10.0, 1e-9));
        REQUIRE_THAT(a.endAngle - a.startAngle, WithinAbs(std::numbers::pi, 1e-9));
    }
    SECTION("Unido a una línea existente")
    {
        Session ui;
        EntityId line = ui.sketch.addLine({-10, 0}, {10, 0});
        auto tool = makeArcTool();
        ui.click(*tool, 10.1, 0.1);
        ui.click(*tool, -9.9, -0.1);
        REQUIRE(ui.click(*tool, 0.0, 10.0));
        REQUIRE(count(ui.sketch, ConstraintType::Coincident) == 2);
        (void)line;
        REQUIRE(makePlaneGcsSolver()->solve(ui.sketch).status == SolveStatus::Solved);
    }
    SECTION("Tres puntos alineados no crean arco")
    {
        Session ui;
        auto tool = makeArcTool();
        ui.click(*tool, 0.0, 0.0);
        ui.click(*tool, 10.0, 0.0);
        REQUIRE_FALSE(ui.click(*tool, 5.0, 0.0));
    }
}

TEST_CASE("Un rectángulo sin cotas queda entero como no restringido", "[sketch][tools]")
{
    Session ui;
    auto tool = makeRectangleTool();
    ui.click(*tool, 0.0, 0.0);
    ui.click(*tool, 23.0, -14.0);

    auto result = makePlaneGcsSolver()->solve(ui.sketch);
    REQUIRE(result.underconstrained == created(ui.sketch));
}
