#include <nuf/sketch/Snap.hpp>

#include <cmath>

namespace nuf::sketch {

namespace {

double distance(Vec2 a, Vec2 b) { return std::hypot(b.x - a.x, b.y - a.y); }

} // namespace

Snap snap(const Sketch& sketch, Vec2 cursor, const SnapOptions& options)
{
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

    for (const Entity& e : sketch.entities()) {
        if (const auto* l = std::get_if<Line>(&e.geometry); l && !e.reference) {
            consider({(l->start.x + l->end.x) / 2, (l->start.y + l->end.y) / 2}, SnapKind::Midpoint,
                     Ref::of(e.id));
        }
    }
    if (best.kind != SnapKind::None) {
        return best;
    }

    if (options.gridStep > 0.0) {
        const double step = options.gridStep;
        return {{std::round(cursor.x / step) * step, std::round(cursor.y / step) * step}, SnapKind::Grid, {}};
    }
    return best;
}

} // namespace nuf::sketch
