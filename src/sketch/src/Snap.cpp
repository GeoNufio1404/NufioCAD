#include <nuf/sketch/Snap.hpp>

#include <nuf/sketch/Pick.hpp>

#include <cmath>
#include <algorithm>
#include <numbers>
#include <optional>
#include <vector>

namespace nuf::sketch {

namespace {

double distance(Vec2 a, Vec2 b) { return std::hypot(b.x - a.x, b.y - a.y); }

// Curva como recta (con o sin límites) o circunferencia (con o sin arco).
struct Curve {
    bool isLine = false;
    Vec2 a, b;           // línea
    bool infinite = false;
    Vec2 center;         // círculo / arco
    double radius = 0.0;
    bool isArc = false;
    double startAngle = 0.0;
    double sweep = 0.0;
};

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

// ¿Está p (que ya está sobre la recta o circunferencia) dentro de la curva?
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

// Punto de la curva más cercano a p (nullopt si cae fuera de un arco).
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

} // namespace

Snap snap(const Sketch& sketch, Vec2 cursor, const SnapOptions& options)
{
    if (!options.enabled) {
        Snap free{cursor, SnapKind::None, {}};
        free.free = true;
        return free;
    }
    Snap best{cursor, SnapKind::None, {}};
    double bestDistance = options.tolerance;

    // En un empate (puntos en el mismo sitio) gana el primero: el origen
    // antes que una línea que empieza en él.
    const auto consider = [&](Vec2 p, SnapKind kind, Ref ref) {
        const double d = distance(cursor, p);
        if (d < bestDistance || (best.kind == SnapKind::None && d <= bestDistance)) {
            bestDistance = d;
            best = {p, kind, ref};
        }
    };

    // Puntos y centros. De las referencias solo cuenta el origen: los
    // extremos de los ejes no significan nada.
    for (const Entity& e : sketch.entities()) {
        if (e.reference && e.id != Sketch::kOrigin) {
            continue;
        }
        if (const auto* p = std::get_if<Point>(&e.geometry)) {
            consider(p->position, SnapKind::Point, Ref::of(e.id));
        } else if (const auto* l = std::get_if<Line>(&e.geometry)) {
            consider(l->start, SnapKind::Point, Ref::start(e.id));
            consider(l->end, SnapKind::Point, Ref::end(e.id));
        } else if (const auto* c = std::get_if<Circle>(&e.geometry)) {
            consider(c->center, SnapKind::Center, Ref::center(e.id));
        } else if (const auto* a = std::get_if<Arc>(&e.geometry)) {
            consider(a->startPoint(), SnapKind::Point, Ref::start(e.id));
            consider(a->endPoint(), SnapKind::Point, Ref::end(e.id));
            consider(a->center, SnapKind::Center, Ref::center(e.id));
        }
    }
    if (best.kind != SnapKind::None) {
        return best;
    }

    // Curvas cerca del cursor (también los ejes).
    std::vector<const Entity*> near;
    for (const Entity& e : sketch.entities()) {
        if (distanceToCurve(e, cursor) <= options.tolerance) {
            near.push_back(&e);
        }
    }

    for (std::size_t i = 0; i < near.size(); ++i) {
        for (std::size_t j = i + 1; j < near.size(); ++j) {
            for (const Vec2& p : intersections(curveOf(*near[i]), curveOf(*near[j]))) {
                const Snap before = best;
                consider(p, SnapKind::Intersection, Ref::of(near[i]->id));
                if (best.kind == SnapKind::Intersection && (before.kind != best.kind || before.point.x != best.point.x
                                                            || before.point.y != best.point.y)) {
                    best.ref2 = Ref::of(near[j]->id);
                }
            }
        }
    }
    if (best.kind != SnapKind::None) {
        return best;
    }

    for (const Entity& e : sketch.entities()) {
        if (const auto* l = std::get_if<Line>(&e.geometry); l && !e.reference) {
            consider({(l->start.x + l->end.x) / 2, (l->start.y + l->end.y) / 2}, SnapKind::Midpoint,
                     Ref::of(e.id));
        }
    }
    if (best.kind != SnapKind::None) {
        return best;
    }

    // Sobre la curva más cercana (las propias antes que los ejes).
    const Entity* closest = nullptr;
    for (const Entity* e : near) {
        if (!closest || (closest->reference && !e->reference)
            || (closest->reference == e->reference && distanceToCurve(*e, cursor) < distanceToCurve(*closest, cursor))) {
            closest = e;
        }
    }
    if (closest) {
        if (const auto p = nearestOn(curveOf(*closest), cursor)) {
            return {*p, SnapKind::OnCurve, Ref::of(closest->id)};
        }
    }

    if (options.gridStep > 0.0) {
        const double step = options.gridStep;
        return {{std::round(cursor.x / step) * step, std::round(cursor.y / step) * step}, SnapKind::Grid, {}};
    }
    return best;
}

} // namespace nuf::sketch
