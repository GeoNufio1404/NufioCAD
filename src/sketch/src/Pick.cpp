#include <nuf/sketch/Pick.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace nuf::sketch {

namespace {

double distance(Vec2 a, Vec2 b) { return std::hypot(b.x - a.x, b.y - a.y); }

double distanceToSegment(Vec2 p, Vec2 a, Vec2 b)
{
    const double dx = b.x - a.x;
    const double dy = b.y - a.y;
    const double len2 = dx * dx + dy * dy;
    if (len2 <= 0.0) {
        return distance(p, a);
    }
    const double t = std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / len2, 0.0, 1.0);
    return distance(p, {a.x + t * dx, a.y + t * dy});
}

double distanceToInfiniteLine(Vec2 p, Vec2 a, Vec2 b)
{
    const double dx = b.x - a.x;
    const double dy = b.y - a.y;
    return std::abs(dx * (p.y - a.y) - dy * (p.x - a.x)) / std::hypot(dx, dy);
}

double distanceToArc(Vec2 p, const Arc& arc)
{
    double angle = std::atan2(p.y - arc.center.y, p.x - arc.center.x);
    double sweep = arc.endAngle - arc.startAngle;
    while (sweep <= 0.0) {
        sweep += 2.0 * std::numbers::pi;
    }
    double offset = std::fmod(angle - arc.startAngle, 2.0 * std::numbers::pi);
    if (offset < 0.0) {
        offset += 2.0 * std::numbers::pi;
    }
    if (offset <= sweep) {
        return std::abs(distance(p, arc.center) - arc.radius);
    }
    return std::min(distance(p, arc.startPoint()), distance(p, arc.endPoint()));
}

} // namespace

double distanceToPolyline(Vec2 p, const std::vector<Vec2>& points)
{
    double best = std::numeric_limits<double>::infinity();
    for (std::size_t i = 1; i < points.size(); ++i) {
        best = std::min(best, distanceToSegment(p, points[i - 1], points[i]));
    }
    return best;
}

double distanceToCurve(const Entity& e, Vec2 p)
{
    if (const auto* l = std::get_if<Line>(&e.geometry)) {
        return e.reference ? distanceToInfiniteLine(p, l->start, l->end) : distanceToSegment(p, l->start, l->end);
    }
    if (const auto* c = std::get_if<Circle>(&e.geometry)) {
        return std::abs(distance(p, c->center) - c->radius);
    }
    if (const auto* a = std::get_if<Arc>(&e.geometry)) {
        return distanceToArc(p, *a);
    }
    if (std::holds_alternative<Point>(e.geometry)) {
        return std::numeric_limits<double>::infinity();
    }
    // Elipses y splines: sobre su polilínea.
    return distanceToPolyline(p, polyline(e.geometry));
}

Ref pick(const Sketch& sketch, Vec2 cursor, double tolerance)
{
    Ref best;
    double bestDistance = tolerance;
    const auto consider = [&](double d, Ref ref) {
        if (d < bestDistance || (best.empty() && d <= bestDistance)) {
            bestDistance = d;
            best = ref;
        }
    };

    // Primero los puntos: un extremo gana a la curva que pasa por él.
    for (const Entity& e : sketch.entities()) {
        if (e.reference && e.id != Sketch::kOrigin) {
            continue;
        }
        for (Part part : {Part::Whole, Part::Start, Part::End, Part::Center}) {
            if (const auto p = sketch.point({e.id, part})) {
                consider(distance(cursor, *p), {e.id, part});
            }
        }
        if (const auto* b = std::get_if<BSpline>(&e.geometry)) {
            // Polos intermedios (el primero y el último son los extremos).
            for (int i = 1; i + 1 < int(b->poles.size()); ++i) {
                consider(distance(cursor, b->poles[std::size_t(i)]), Ref::pole(e.id, i));
            }
        }
    }
    if (!best.empty()) {
        return best;
    }

    // Después las curvas propias y, solo si no hay ninguna cerca, los ejes.
    for (const Entity& e : sketch.entities()) {
        if (!e.reference) {
            consider(distanceToCurve(e, cursor), Ref::of(e.id));
        }
    }
    if (!best.empty()) {
        return best;
    }
    for (const Entity& e : sketch.entities()) {
        if (e.reference && std::holds_alternative<Line>(e.geometry)) {
            consider(distanceToCurve(e, cursor), Ref::of(e.id));
        }
    }
    return best;
}

} // namespace nuf::sketch
