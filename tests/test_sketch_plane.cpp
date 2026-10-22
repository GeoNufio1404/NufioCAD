#include <nuf/sketch/Plane.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <stdexcept>

using namespace nuf::sketch;
using Catch::Matchers::WithinAbs;

namespace {

constexpr double kTol = 1e-9;

void requireVec(Vec3 v, double x, double y, double z)
{
    REQUIRE_THAT(v.x, WithinAbs(x, kTol));
    REQUIRE_THAT(v.y, WithinAbs(y, kTol));
    REQUIRE_THAT(v.z, WithinAbs(z, kTol));
}

Vec3 cross(Vec3 a, Vec3 b)
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

// Ejes ortonormales y a derechas.
void requireFrame(const Plane& p)
{
    Vec3 n = cross(p.xAxis, p.yAxis);
    requireVec(n, p.normal.x, p.normal.y, p.normal.z);
    REQUIRE_THAT(std::hypot(p.xAxis.x, p.xAxis.y, p.xAxis.z), WithinAbs(1.0, kTol));
    REQUIRE_THAT(std::hypot(p.yAxis.x, p.yAxis.y, p.yAxis.z), WithinAbs(1.0, kTol));
}

} // namespace

TEST_CASE("Los planos de origen son sistemas a derechas", "[sketch]")
{
    for (const Plane& p : {Plane::xy(), Plane::xz(), Plane::yz()}) {
        requireFrame(p);
    }
    // Vista de frente (XZ): X a la derecha, Z arriba.
    requireVec(Plane::xz().toWorld({3, 4}), 3, 0, 4);
}

TEST_CASE("Plano desde una cara: orientación como en Fusion", "[sketch]")
{
    SECTION("Cara superior a 8 mm de altura")
    {
        Plane p = Plane::fromPointAndNormal({12, -5, 8}, {0, 0, 2});
        requireFrame(p);
        requireVec(p.origin, 0, 0, 8);
        requireVec(p.xAxis, 1, 0, 0);
        requireVec(p.yAxis, 0, 1, 0);
    }
    SECTION("Cara frontal (normal −Y): X a la derecha, Z arriba")
    {
        Plane p = Plane::fromPointAndNormal({5, -20, 3}, {0, -1, 0});
        requireFrame(p);
        requireVec(p.origin, 0, -20, 0);
        requireVec(p.xAxis, 1, 0, 0);
        requireVec(p.yAxis, 0, 0, 1);
    }
    SECTION("Cara lateral izquierda (normal −X)")
    {
        Plane p = Plane::fromPointAndNormal({-30, 4, 4}, {-1, 0, 0});
        requireFrame(p);
        requireVec(p.xAxis, 0, -1, 0);
        requireVec(p.yAxis, 0, 0, 1);
    }
    SECTION("Cara inclinada")
    {
        requireFrame(Plane::fromPointAndNormal({1, 2, 3}, {1, 1, 1}));
    }
    REQUIRE_THROWS_AS(Plane::fromPointAndNormal({}, {0, 0, 0}), std::invalid_argument);
}

TEST_CASE("Ida y vuelta entre coordenadas del sketch y del modelo", "[sketch]")
{
    Plane p = Plane::fromPointAndNormal({0, 0, 10}, {0, -1, 1});
    Vec3 w = p.toWorld({7.5, -2.25});
    Vec2 back = p.toLocal(w);
    REQUIRE_THAT(back.x, WithinAbs(7.5, kTol));
    REQUIRE_THAT(back.y, WithinAbs(-2.25, kTol));

    // Un punto fuera del plano se proyecta.
    Vec2 projected = p.toLocal({w.x + p.normal.x * 5, w.y + p.normal.y * 5, w.z + p.normal.z * 5});
    REQUIRE_THAT(projected.x, WithinAbs(7.5, kTol));
    REQUIRE_THAT(projected.y, WithinAbs(-2.25, kTol));
}
