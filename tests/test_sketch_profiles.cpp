#include <nuf/sketch/Profiles.hpp>
#include <nuf/sketch/Tools.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>

using namespace nuf::sketch;
using Catch::Matchers::WithinAbs;

namespace {

void rectangle(Sketch& s, double x0, double y0, double x1, double y1)
{
    auto tool = makeRectangleTool();
    tool->click(s, {{x0, y0}, SnapKind::None, {}});
    tool->click(s, {{x1, y1}, SnapKind::None, {}});
}

std::vector<double> areas(const std::vector<Profile>& profiles)
{
    std::vector<double> result;
    for (const Profile& p : profiles) {
        result.push_back(p.area);
    }
    std::ranges::sort(result);
    return result;
}

} // namespace

TEST_CASE("Perfiles: rectángulo", "[sketch][profiles]")
{
    Sketch s;
    rectangle(s, 0, 0, 40, 25);
    auto profiles = findProfiles(s);
    REQUIRE(profiles.size() == 1);
    REQUIRE_THAT(profiles[0].area, WithinAbs(1000.0, 1e-9));
    REQUIRE(profiles[0].outer.edges.size() == 4);
    REQUIRE(profiles[0].holes.empty());
}

TEST_CASE("Perfiles: rectángulo con un agujero circular", "[sketch][profiles]")
{
    Sketch s;
    rectangle(s, 0, 0, 40, 25);
    s.addCircle({20, 12.5}, 5);
    auto profiles = findProfiles(s);
    REQUIRE(profiles.size() == 2);
    const double disk = std::numbers::pi * 25.0;
    REQUIRE_THAT(areas(profiles)[0], WithinAbs(disk, 1e-9));
    REQUIRE_THAT(areas(profiles)[1], WithinAbs(1000.0 - disk, 1e-9));
    const auto& outer = *std::ranges::find_if(profiles, [](const Profile& p) { return !p.holes.empty(); });
    REQUIRE(outer.holes.size() == 1);
}

TEST_CASE("Perfiles: rectángulo partido por una línea", "[sketch][profiles]")
{
    Sketch s;
    // Contorno con un vértice en (15, 0) y (15, 25) para apoyar la división.
    EntityId a = s.addLine({0, 0}, {15, 0});
    EntityId b = s.addLine({15, 0}, {40, 0});
    s.addLine({40, 0}, {40, 25});
    s.addLine({40, 25}, {15, 25});
    s.addLine({15, 25}, {0, 25});
    s.addLine({0, 25}, {0, 0});
    s.addLine({15, 0}, {15, 25});
    (void)a;
    (void)b;
    auto profiles = findProfiles(s);
    REQUIRE(profiles.size() == 2);
    REQUIRE_THAT(areas(profiles)[0], WithinAbs(375.0, 1e-9));
    REQUIRE_THAT(areas(profiles)[1], WithinAbs(625.0, 1e-9));
}

TEST_CASE("Perfiles: forma de D con un arco", "[sketch][profiles]")
{
    Sketch s;
    s.addLine({0, -10}, {0, 10});
    s.addArc({0, 0}, 10, -std::numbers::pi / 2, std::numbers::pi / 2);
    auto profiles = findProfiles(s);
    REQUIRE(profiles.size() == 1);
    REQUIRE_THAT(profiles[0].area, WithinAbs(std::numbers::pi * 50.0, 0.5));  // polígono aproximado
}

TEST_CASE("Perfiles: abiertos, sueltos y construcción no cuentan", "[sketch][profiles]")
{
    Sketch s;
    s.addLine({0, 0}, {10, 0});
    s.addLine({10, 0}, {10, 10});
    REQUIRE(findProfiles(s).empty());

    // Un triángulo con una «cola» suelta: la cola se ignora.
    Sketch t;
    t.addLine({0, 0}, {10, 0});
    t.addLine({10, 0}, {0, 10});
    t.addLine({0, 10}, {0, 0});
    t.addLine({10, 0}, {20, 5});
    auto profiles = findProfiles(t);
    REQUIRE(profiles.size() == 1);
    REQUIRE(profiles[0].outer.edges.size() == 3);
    REQUIRE_THAT(profiles[0].area, WithinAbs(50.0, 1e-9));

    Sketch u;
    u.addCircle({0, 0}, 3, true);
    REQUIRE(findProfiles(u).empty());
}

TEST_CASE("Perfiles: rectángulos anidados", "[sketch][profiles]")
{
    Sketch s;
    rectangle(s, 0, 0, 100, 100);
    rectangle(s, 10, 10, 30, 30);
    auto profiles = findProfiles(s);
    REQUIRE(profiles.size() == 2);
    REQUIRE_THAT(areas(profiles)[0], WithinAbs(400.0, 1e-9));
    REQUIRE_THAT(areas(profiles)[1], WithinAbs(9600.0, 1e-9));
}

TEST_CASE("Perfiles: una línea que cruza un rectángulo lo parte en dos", "[sketch][profiles]")
{
    Sketch s;
    rectangle(s, 0, 0, 40, 20);
    s.addLine({10, -5}, {10, 25});  // cruza sin tocar extremos y sobresale
    auto profiles = findProfiles(s);
    REQUIRE(profiles.size() == 2);
    REQUIRE(areas(profiles) == std::vector<double>{200.0, 600.0});
    // Los tramos partidos lo indican.
    const bool trimmed = std::ranges::any_of(profiles.front().outer.edges, [](const ProfileEdge& e) { return e.trim.has_value(); });
    REQUIRE(trimmed);
}

TEST_CASE("Perfiles: polígono partido por una diagonal y círculo partido por una recta", "[sketch][profiles]")
{
    Sketch s;
    // Hexágono regular de radio 10 y una recta horizontal por el centro.
    std::vector<Vec2> v;
    for (int i = 0; i < 6; ++i) {
        v.push_back({10 * std::cos(i * std::numbers::pi / 3), 10 * std::sin(i * std::numbers::pi / 3)});
    }
    for (int i = 0; i < 6; ++i) {
        s.addLine(v[std::size_t(i)], v[std::size_t((i + 1) % 6)]);
    }
    s.addLine({-15, 1}, {15, 1});
    auto profiles = findProfiles(s);
    REQUIRE(profiles.size() == 2);
    const double hexArea = 1.5 * std::sqrt(3.0) * 100.0;
    double total = 0.0;
    for (const Profile& p : profiles) {
        total += p.area;
    }
    REQUIRE_THAT(total, WithinAbs(hexArea, 1e-6));

    Sketch c;
    c.addCircle({0, 0}, 10);
    c.addLine({-20, 0}, {20, 0});
    auto halves = findProfiles(c);
    REQUIRE(halves.size() == 2);
    for (const Profile& p : halves) {
        REQUIRE_THAT(p.area, WithinAbs(std::numbers::pi * 50.0, 0.5));  // polilínea del círculo
    }
}

TEST_CASE("Perfiles: círculo tangente a los cuatro lados de un cuadrado da 5 regiones", "[sketch][profiles]")
{
    Sketch s;
    rectangle(s, 0, 0, 20, 20);
    s.addCircle({10, 10}, 10);
    auto profiles = findProfiles(s);
    REQUIRE(profiles.size() == 5);
    double total = 0.0;
    for (const Profile& p : profiles) {
        total += p.area;
    }
    REQUIRE_THAT(total, WithinAbs(400.0, 0.5));
}
