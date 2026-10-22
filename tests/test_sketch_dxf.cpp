#include <nuf/sketch/Dxf.hpp>
#include <nuf/sketch/Profiles.hpp>
#include <nuf/sketch/Solver.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <sstream>

using namespace nuf::sketch;
using Catch::Matchers::WithinAbs;

namespace {

// DXF mínimo (solo la sección ENTITIES, como escriben muchos programas):
// polilínea cerrada con un arco por bulge, círculo, arco, elipse, spline
// Bézier y un texto (no soportado).
const char* kDrawing = "0\nSECTION\n2\nENTITIES\n0\nLWPOLYLINE\n90\n4\n70\n1\n10\n0\n20\n0\n10\n40\n20\n0\n42\n1.0\n10\n40\n20\n20\n10\n0\n20\n20\n0\nCIRCLE\n10\n15\n20\n10\n40\n4\n0\nARC\n10\n100\n20\n0\n40\n5\n50\n0\n51\n90\n0\nELLIPSE\n10\n200\n20\n0\n11\n10\n21\n0\n40\n0.5\n41\n0\n42\n6.283185307179586\n0\nSPLINE\n71\n3\n72\n8\n73\n4\n40\n0\n40\n0\n40\n0\n40\n0\n40\n1\n40\n1\n40\n1\n40\n1\n10\n300\n20\n0\n10\n305\n20\n10\n10\n315\n20\n10\n10\n320\n20\n0\n0\nTEXT\n1\nhola\n0\nENDSEC\n0\nEOF";

} // namespace

TEST_CASE("DXF: entidades habituales", "[sketch][dxf]")
{
    std::istringstream in(kDrawing);
    const DxfDrawing d = readDxf(in);
    // 4 tramos de la polilínea (uno es un arco por bulge) + círculo + arco + elipse + spline.
    REQUIRE(d.geometry.size() == 8);
    REQUIRE(d.skipped == std::vector<std::string>{"TEXT"});
    REQUIRE(std::holds_alternative<Line>(d.geometry[0]));
    // bulge 1 = semicírculo de (40,0) a (40,20), antihorario: hacia +X.
    const auto& bulge = std::get<Arc>(d.geometry[1]);
    REQUIRE_THAT(bulge.radius, WithinAbs(10.0, 1e-9));
    REQUIRE_THAT(bulge.center.x, WithinAbs(40.0, 1e-9));
    REQUIRE_THAT(bulge.center.y, WithinAbs(10.0, 1e-9));
    const auto& arc = std::get<Arc>(d.geometry[5]);
    REQUIRE_THAT(arc.endAngle - arc.startAngle, WithinAbs(std::numbers::pi / 2, 1e-9));
    const auto& e = std::get<Ellipse>(d.geometry[6]);
    REQUIRE_THAT(e.majorRadius, WithinAbs(10.0, 1e-9));
    REQUIRE_THAT(e.minorRadius, WithinAbs(5.0, 1e-9));
    // La spline Bézier (nudos 0000 1111) se reinterpola pasando por sus extremos.
    const auto& s = std::get<BSpline>(d.geometry[7]);
    REQUIRE_THAT(s.poles.front().x, WithinAbs(300.0, 1e-9));
    REQUIRE_THAT(s.poles.back().x, WithinAbs(320.0, 1e-9));
    REQUIRE_THAT(s.pointAt(0.5).y, WithinAbs(7.5, 1e-2));  // (0 + 30 + 30 + 0) / 8
}

TEST_CASE("DXF: importar une los extremos y forma perfiles", "[sketch][dxf]")
{
    std::istringstream in(kDrawing);
    Sketch sketch;
    const auto ids = importDrawing(sketch, readDxf(in), {5, 5});
    REQUIRE(ids.size() == 8);
    // La polilínea cerrada: 4 uniones.
    REQUIRE(std::ranges::count(sketch.constraints(), ConstraintType::Coincident, &Constraint::type) == 4);
    const auto result = makePlaneGcsSolver()->solve(sketch);
    REQUIRE(result.status == SolveStatus::Solved);
    REQUIRE(result.redundant.empty());
    // Polilínea (con el círculo dentro como agujero), círculo y elipse.
    REQUIRE(findProfiles(sketch).size() == 3);
    REQUIRE_THAT(sketch.point(Ref::start(ids[0]))->x, WithinAbs(5.0, 1e-9));
}
