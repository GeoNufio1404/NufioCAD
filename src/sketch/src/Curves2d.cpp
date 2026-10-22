#include "Curves2d.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace nuf::sketch::detail {

namespace {

double distance(Vec2 a, Vec2 b) { return std::hypot(b.x - a.x, b.y - a.y); }

} // namespace

bool isSimpleCurve(const Entity& e)
{
    return std::holds_alternative<Line>(e.geometry) || std::holds_alternative<Circle>(e.geometry)
           || std::holds_alternative<Arc>(e.geometry);
}

Curve curveOf(const Entity& e)
{
    Curve c;
    if (const auto* l = std::get_if<Line>(&e.geometry)) {
        c.isLine = true;
        c.a = l->start;
        c.b = l->end;
        c.infinite = e.reference;
    } else if (const auto* circle = std::get_if<Circle>(&e.geometry)) {
        c.center = circle->center;
        c.radius = circle->radius;
    } else if (const auto* arc = std::get_if<Arc>(&e.geometry)) {
        c.center = arc->center;
        c.radius = arc->radius;
        c.isArc = true;
        c.startAngle = arc->startAngle;
        c.sweep = arc->endAngle - arc->startAngle;
        while (c.sweep <= 0.0) {
            c.sweep += 2.0 * std::numbers::pi;
        }
    }
    return c;
}

bool contains(const Curve& c, Vec2 p)
{
    constexpr double kEps = 1e-9;
    if (c.isLine) {
        if (c.infinite) {
            return true;
        }
        const double dx = c.b.x - c.a.x;
        const double dy = c.b.y - c.a.y;
        const double t = ((p.x - c.a.x) * dx + (p.y - c.a.y) * dy) / (dx * dx + dy * dy);
        return t >= -kEps && t <= 1.0 + kEps;
    }
    if (!c.isArc) {
        return true;
    }
    double offset = std::fmod(std::atan2(p.y - c.center.y, p.x - c.center.x) - c.startAngle, 2.0 * std::numbers::pi);
    if (offset < 0.0) {
        offset += 2.0 * std::numbers::pi;
    }
    return offset <= c.sweep + kEps;
}

std::vector<Vec2> intersections(const Curve& p, const Curve& q)
{
    std::vector<Vec2> raw;
    if (p.isLine && q.isLine) {
        const Vec2 d1{p.b.x - p.a.x, p.b.y - p.a.y};
        const Vec2 d2{q.b.x - q.a.x, q.b.y - q.a.y};
        const double denom = d1.x * d2.y - d1.y * d2.x;
        if (std::abs(denom) > 1e-12) {
            const double t = ((q.a.x - p.a.x) * d2.y - (q.a.y - p.a.y) * d2.x) / denom;
            raw.push_back({p.a.x + t * d1.x, p.a.y + t * d1.y});
        }
    } else if (p.isLine != q.isLine) {
        const Curve& line = p.isLine ? p : q;
        const Curve& round = p.isLine ? q : p;
        const Vec2 d{line.b.x - line.a.x, line.b.y - line.a.y};
        const Vec2 f{line.a.x - round.center.x, line.a.y - round.center.y};
        const double a = d.x * d.x + d.y * d.y;
        const double b = 2.0 * (f.x * d.x + f.y * d.y);
        const double c = f.x * f.x + f.y * f.y - round.radius * round.radius;
        const double disc = b * b - 4.0 * a * c;
        if (disc >= 0.0) {
            for (double sign : {-1.0, 1.0}) {
                const double t = (-b + sign * std::sqrt(disc)) / (2.0 * a);
                raw.push_back({line.a.x + t * d.x, line.a.y + t * d.y});
            }
        }
    } else {
        const double d = distance(p.center, q.center);
        if (d > 1e-12 && d <= p.radius + q.radius && d >= std::abs(p.radius - q.radius)) {
            const double a = (p.radius * p.radius - q.radius * q.radius + d * d) / (2.0 * d);
            const double h = std::sqrt(std::max(0.0, p.radius * p.radius - a * a));
            const Vec2 u{(q.center.x - p.center.x) / d, (q.center.y - p.center.y) / d};
            const Vec2 m{p.center.x + a * u.x, p.center.y + a * u.y};
            raw.push_back({m.x - h * u.y, m.y + h * u.x});
            raw.push_back({m.x + h * u.y, m.y - h * u.x});
        }
    }
    std::vector<Vec2> result;
    for (const Vec2& point : raw) {
        if (contains(p, point) && contains(q, point)) {
            result.push_back(point);
        }
    }
    return result;
}

std::optional<Vec2> nearestOn(const Curve& c, Vec2 p)
{
    if (c.isLine) {
        const double dx = c.b.x - c.a.x;
        const double dy = c.b.y - c.a.y;
        double t = ((p.x - c.a.x) * dx + (p.y - c.a.y) * dy) / (dx * dx + dy * dy);
        if (!c.infinite) {
            t = std::clamp(t, 0.0, 1.0);
        }
        return Vec2{c.a.x + t * dx, c.a.y + t * dy};
    }
    const double d = distance(p, c.center);
    if (d < 1e-12) {
        return std::nullopt;
    }
    const Vec2 q{c.center.x + c.radius * (p.x - c.center.x) / d, c.center.y + c.radius * (p.y - c.center.y) / d};
    return contains(c, q) ? std::optional<Vec2>(q) : std::nullopt;
}

} // namespace nuf::sketch::detail
