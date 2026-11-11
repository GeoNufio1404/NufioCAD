#include "ToolSupport.hpp"

#include <nuf/sketch/Tools.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace nuf::sketch::detail {

double distance(Vec2 a, Vec2 b) { return std::hypot(b.x - a.x, b.y - a.y); }

void attach(Sketch& sketch, Ref created, const Snap& snapped)
{
    switch (snapped.kind) {
    case SnapKind::Point:
    case SnapKind::Center:
        sketch.addConstraint(Constraint::coincident(created, snapped.ref));
        break;
    case SnapKind::Midpoint:
        sketch.addConstraint(Constraint::midpoint(created, snapped.ref.entity));
        break;
    case SnapKind::OnCurve:
        sketch.addConstraint(Constraint::pointOnObject(created, snapped.ref.entity));
        break;
    case SnapKind::Intersection:
        sketch.addConstraint(Constraint::pointOnObject(created, snapped.ref.entity));
        sketch.addConstraint(Constraint::pointOnObject(created, snapped.ref2.entity));
        break;
    case SnapKind::None:
    case SnapKind::Grid:
        break;
    }
    // Alineación inferida con otro punto: horizontal o vertical entre ambos.
    if (!snapped.alignY.empty() && !(snapped.alignY == created)) {
        const Constraint c = Constraint::horizontal(created, snapped.alignY);
        if (sketch.canAdd(c)) {
            sketch.addConstraint(c);
        }
    }
    if (!snapped.alignX.empty() && !(snapped.alignX == created)) {
        const Constraint c = Constraint::vertical(created, snapped.alignX);
        if (sketch.canAdd(c)) {
            sketch.addConstraint(c);
        }
    }
}

Straight straighten(Vec2 start, Snap& end)
{
    if (end.free || (end.kind != SnapKind::None && end.kind != SnapKind::Grid)) {
        return Straight::None;
    }
    // Pegado a la recta de otro punto: no se mueve (la guía lo muestra). Si esa
    // recta es justo la del inicio, basta con la restricción de la línea.
    if (!end.alignY.empty() || !end.alignX.empty()) {
        if (!end.alignY.empty() && std::abs(end.point.y - start.y) < kMinLength) {
            end.alignY = {};
            return Straight::Horizontal;
        }
        if (!end.alignX.empty() && std::abs(end.point.x - start.x) < kMinLength) {
            end.alignX = {};
            return Straight::Vertical;
        }
        return Straight::None;
    }
    const double dx = end.point.x - start.x;
    const double dy = end.point.y - start.y;
    const double tolerance = std::tan(kStraightenAngle);
    if (std::abs(dx) > kMinLength && std::abs(dy) <= tolerance * std::abs(dx)) {
        end.point.y = start.y;
        return Straight::Horizontal;
    }
    if (std::abs(dy) > kMinLength && std::abs(dx) <= tolerance * std::abs(dy)) {
        end.point.x = start.x;
        return Straight::Vertical;
    }
    return Straight::None;
}

std::vector<EntityId> entitiesSince(const Sketch& sketch, std::size_t mark)
{
    std::vector<EntityId> ids;
    const auto& entities = sketch.entities();
    for (std::size_t i = mark; i < entities.size(); ++i) {
        ids.push_back(entities[i].id);
    }
    return ids;
}

bool discardEntities(Sketch& sketch, const std::vector<EntityId>& ids)
{
    for (const EntityId id : ids) {
        if (sketch.entity(id) == nullptr) {
            return false;
        }
    }
    for (const EntityId id : ids) {
        sketch.removeEntity(id);
    }
    return true;
}

Relation relate(Vec2 start, const Line& reference, Snap& end)
{
    if (end.free || (end.kind != SnapKind::None && end.kind != SnapKind::Grid) || !end.alignX.empty()
        || !end.alignY.empty()) {
        return Relation::None;
    }
    const double rx = reference.end.x - reference.start.x;
    const double ry = reference.end.y - reference.start.y;
    const double rlen = std::hypot(rx, ry);
    const double vx = end.point.x - start.x;
    const double vy = end.point.y - start.y;
    const double vlen = std::hypot(vx, vy);
    if (rlen < kMinLength || vlen < kMinLength) {
        return Relation::None;
    }
    const Vec2 d{rx / rlen, ry / rlen};
    const double along = vx * d.x + vy * d.y;     // componente paralela
    const double across = -vx * d.y + vy * d.x;   // componente perpendicular
    const double tolerance = std::tan(kStraightenAngle);
    if (std::abs(across) <= tolerance * std::abs(along)) {
        end.point = {start.x + d.x * along, start.y + d.y * along};
        return Relation::Parallel;
    }
    if (std::abs(along) <= tolerance * std::abs(across)) {
        end.point = {start.x - d.y * across, start.y + d.x * across};
        return Relation::Perpendicular;
    }
    return Relation::None;
}

std::optional<ArcFit> fitArc(Vec2 a, Vec2 b, Vec2 through)
{
    const auto c = circumcenter(a, b, through);
    if (!c) {
        return std::nullopt;
    }
    const Vec2 center = *c;
    const double radius = distance(center, a);

    const auto angle = [&](Vec2 p) { return std::atan2(p.y - center.y, p.x - center.x); };
    const auto ccw = [](double from, double to) {  // recorrido antihorario de from a to, en [0, 2π)
        double delta = std::fmod(to - from, 2.0 * std::numbers::pi);
        return delta < 0.0 ? delta + 2.0 * std::numbers::pi : delta;
    };
    const double angleA = angle(a);
    const double angleB = angle(b);
    const bool throughOnWay = ccw(angleA, angle(through)) < ccw(angleA, angleB);
    if (throughOnWay) {
        return ArcFit{Arc{center, radius, angleA, angleA + ccw(angleA, angleB)}, false};
    }
    return ArcFit{Arc{center, radius, angleB, angleB + ccw(angleB, angleA)}, true};
}

void attachOnCurve(Sketch& sketch, EntityId curve, const Snap& snapped)
{
    if (snapped.kind == SnapKind::Point || snapped.kind == SnapKind::Center) {
        sketch.addConstraint(Constraint::pointOnObject(snapped.ref, curve));
    }
}

EntityId curveAt(const Sketch& sketch, const Snap& snapped)
{
    if (snapped.kind == SnapKind::None || snapped.kind == SnapKind::Grid) {
        return 0;
    }
    const Entity* e = sketch.entity(snapped.ref.entity);
    if (!e || std::holds_alternative<Point>(e->geometry)) {
        return 0;
    }
    return e->id;
}

std::optional<Vec2> circumcenter(Vec2 a, Vec2 b, Vec2 c)
{
    const double d = 2.0 * (a.x * (b.y - c.y) + b.x * (c.y - a.y) + c.x * (a.y - b.y));
    const double scale = std::max({distance(a, b), distance(a, c), distance(b, c)});
    if (std::abs(d) < 1e-9 * scale * scale) {
        return std::nullopt;
    }
    const double a2 = a.x * a.x + a.y * a.y;
    const double b2 = b.x * b.x + b.y * b.y;
    const double c2 = c.x * c.x + c.y * c.y;
    return Vec2{(a2 * (b.y - c.y) + b2 * (c.y - a.y) + c2 * (a.y - b.y)) / d,
                (a2 * (c.x - b.x) + b2 * (a.x - c.x) + c2 * (b.x - a.x)) / d};
}

std::optional<ArcFit> tangentArc(Vec2 s, Vec2 direction, Vec2 end)
{
    const Vec2 n{-direction.y, direction.x};  // a la izquierda de la dirección
    const Vec2 v{end.x - s.x, end.y - s.y};
    const double k = v.x * n.x + v.y * n.y;
    const double len2 = v.x * v.x + v.y * v.y;
    if (len2 < kMinLength * kMinLength || std::abs(k) < 1e-9 * std::sqrt(len2)) {
        return std::nullopt;
    }
    const double r = len2 / (2.0 * k);  // con signo: positivo = centro a la izquierda
    const Vec2 center{s.x + r * n.x, s.y + r * n.y};
    const double radius = std::abs(r);
    const double angleS = std::atan2(s.y - center.y, s.x - center.x);
    const double angleE = std::atan2(end.y - center.y, end.x - center.x);
    const auto ccw = [](double from, double to) {
        const double delta = std::fmod(to - from, 2.0 * std::numbers::pi);
        return delta < 0.0 ? delta + 2.0 * std::numbers::pi : delta;
    };
    if (r > 0.0) {
        return ArcFit{Arc{center, radius, angleS, angleS + ccw(angleS, angleE)}, false};
    }
    return ArcFit{Arc{center, radius, angleE, angleE + ccw(angleE, angleS)}, true};
}

std::optional<Vec2> exitDirection(const Sketch& sketch, Ref endpoint)
{
    const Entity* e = sketch.entity(endpoint.entity);
    if (!e || (endpoint.part != Part::Start && endpoint.part != Part::End)) {
        return std::nullopt;
    }
    Vec2 dir;
    if (const auto* l = std::get_if<Line>(&e->geometry)) {
        dir = endpoint.part == Part::End ? Vec2{l->end.x - l->start.x, l->end.y - l->start.y}
                                         : Vec2{l->start.x - l->end.x, l->start.y - l->end.y};
    } else if (const auto* a = std::get_if<Arc>(&e->geometry)) {
        // Al final, se sigue en sentido antihorario; al inicio, en horario.
        dir = endpoint.part == Part::End ? Vec2{-std::sin(a->endAngle), std::cos(a->endAngle)}
                                         : Vec2{std::sin(a->startAngle), -std::cos(a->startAngle)};
    } else {
        return std::nullopt;
    }
    const double len = std::hypot(dir.x, dir.y);
    if (len < kMinLength) {
        return std::nullopt;
    }
    return Vec2{dir.x / len, dir.y / len};
}

} // namespace nuf::sketch::detail
