#include <nuf/sketch/Plane.hpp>

#include <cmath>
#include <stdexcept>

namespace nuf::sketch {

namespace {

Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 operator*(double k, Vec3 v) { return {k * v.x, k * v.y, k * v.z}; }
double dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 cross(Vec3 a, Vec3 b)
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
double length(Vec3 v) { return std::sqrt(dot(v, v)); }

} // namespace

Plane Plane::xy() { return {{}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}}; }
Plane Plane::xz() { return {{}, {1, 0, 0}, {0, 0, 1}, {0, -1, 0}}; }
Plane Plane::yz() { return {{}, {0, 1, 0}, {0, 0, 1}, {1, 0, 0}}; }

Plane Plane::fromPointAndNormal(Vec3 point, Vec3 normal)
{
    const double len = length(normal);
    if (!(len > 1e-12)) {
        throw std::invalid_argument("La normal del plano no puede ser nula");
    }
    Plane plane;
    plane.normal = (1.0 / len) * normal;
    const Vec3 n = plane.normal;

    // Origen del modelo proyectado sobre el plano.
    plane.origin = dot(point, n) * n;

    // «Arriba» = Z del modelo sin su componente normal; si el plano es casi
    // horizontal, se usa Y.
    Vec3 up = Vec3{0, 0, 1} - n.z * n;
    if (length(up) < 1e-6) {
        up = Vec3{0, 1, 0} - n.y * n;
    }
    plane.yAxis = (1.0 / length(up)) * up;
    plane.xAxis = cross(plane.yAxis, n);
    return plane;
}

Vec3 Plane::toWorld(Vec2 p) const
{
    return origin + p.x * xAxis + p.y * yAxis;
}

Vec2 Plane::toLocal(Vec3 p) const
{
    const Vec3 d = p - origin;
    return {dot(d, xAxis), dot(d, yAxis)};
}

} // namespace nuf::sketch
