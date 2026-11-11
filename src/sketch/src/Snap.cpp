#include <nuf/sketch/Snap.hpp>

#include <nuf/sketch/Pick.hpp>

#include "Curves2d.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <optional>
#include <vector>

namespace nuf::sketch {

namespace {

using namespace detail;

double distance(Vec2 a, Vec2 b) { return std::hypot(b.x - a.x, b.y - a.y); }

std::optional<Vec2> nearestOnPolyline(const std::vector<Vec2>& points, Vec2 p)
{
    std::optional<Vec2> best;
    double bestDistance = std::numeric_limits<double>::infinity();
    for (std::size_t i = 1; i < points.size(); ++i) {
        const Vec2 a = points[i - 1];
        const Vec2 b = points[i];
        const double dx = b.x - a.x;
        const double dy = b.y - a.y;
        const double len2 = dx * dx + dy * dy;
        const double t = len2 > 0.0 ? std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / len2, 0.0, 1.0) : 0.0;
        const Vec2 q{a.x + t * dx, a.y + t * dy};
        const double d = distance(p, q);
        if (d < bestDistance) {
            bestDistance = d;
            best = q;
        }
    }
    return best;
}

// Busca, entre los puntos de referencia, el corte de la curva con su
// horizontal o su vertical más cercano al cursor (a menos de alignTolerance) y,
// si lo hay, lleva el ajuste hasta allí con su guía.
void alignOnCurve(const Curve& curve, Vec2 cursor, const SnapOptions& options, Snap& snap)
{
    double bestDistance = options.alignTolerance;
    for (const AlignPoint& ref : options.alignPoints) {
        for (const bool horizontal : {true, false}) {
            Curve axis;
            axis.isLine = true;
            axis.infinite = true;
            axis.a = horizontal ? Vec2{0.0, ref.position.y} : Vec2{ref.position.x, 0.0};
            axis.b = horizontal ? Vec2{1.0, ref.position.y} : Vec2{ref.position.x, 1.0};
            for (const Vec2& p : intersections(curve, axis)) {
                const double d = distance(cursor, p);
                if (d < bestDistance && distance(p, ref.position) > 1e-6) {
                    bestDistance = d;
                    snap.point = p;
                    snap.alignX = {};
                    snap.alignY = {};
                    (horizontal ? snap.alignY : snap.alignX) = ref.ref;
                    snap.guides = {{ref.position, p}};
                }
            }
        }
    }
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
        } else if (const auto* el = std::get_if<Ellipse>(&e.geometry)) {
            consider(el->center, SnapKind::Center, Ref::center(e.id));
        } else if (const auto* ea = std::get_if<EllipseArc>(&e.geometry)) {
            consider(ea->startPoint(), SnapKind::Point, Ref::start(e.id));
            consider(ea->endPoint(), SnapKind::Point, Ref::end(e.id));
            consider(ea->ellipse.center, SnapKind::Center, Ref::center(e.id));
        } else if (const auto* b = std::get_if<BSpline>(&e.geometry)) {
            consider(b->poles.front(), SnapKind::Point, Ref::start(e.id));
            consider(b->poles.back(), SnapKind::Point, Ref::end(e.id));
            for (int i = 1; i + 1 < int(b->poles.size()); ++i) {
                consider(b->poles[std::size_t(i)], SnapKind::Point, Ref::pole(e.id, i));
            }
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
            if (!isSimpleCurve(*near[i]) || !isSimpleCurve(*near[j])) {
                continue;
            }
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
        std::optional<Vec2> p;
        if (isSimpleCurve(*closest)) {
            p = nearestOn(curveOf(*closest), cursor);
        } else {
            p = nearestOnPolyline(polyline(closest->geometry), cursor);
        }
        if (p) {
            Snap onCurve{*p, SnapKind::OnCurve, Ref::of(closest->id)};
            // Sobre una recta o circunferencia también se alinea: el punto se
            // desliza hasta donde la curva corta la horizontal o la vertical
            // de un punto de referencia.
            if (options.alignTolerance > 0.0 && isSimpleCurve(*closest)) {
                alignOnCurve(curveOf(*closest), cursor, options, onCurve);
            }
            return onCurve;
        }
    }

    // Alineación horizontal/vertical con los puntos de referencia (softblock):
    // el cursor se pega a la recta; con dos, al cruce de ambas.
    if (options.alignTolerance > 0.0) {
        const AlignPoint* sameY = nullptr;
        const AlignPoint* sameX = nullptr;
        double bestY = options.alignTolerance;
        double bestX = options.alignTolerance;
        const double release = std::max(options.alignRelease, options.alignTolerance);
        for (const AlignPoint& ref : options.alignPoints) {
            const double dy = std::abs(cursor.y - ref.position.y);
            const double dx = std::abs(cursor.x - ref.position.x);
            // La alineación que ya había se suelta más lejos (histéresis) y
            // gana a cualquier otra.
            const bool stickY = !options.stickyY.empty() && ref.ref == options.stickyY && dy <= release;
            const bool stickX = !options.stickyX.empty() && ref.ref == options.stickyX && dx <= release;
            if (stickY || (dy <= bestY && dx > options.alignTolerance)) {
                bestY = stickY ? -1.0 : dy;
                sameY = &ref;
            }
            if (stickX || (dx <= bestX && dy > options.alignTolerance)) {
                bestX = stickX ? -1.0 : dx;
                sameX = &ref;
            }
        }
        if (sameX || sameY) {
            Snap aligned{cursor, SnapKind::None, {}};
            if (options.gridStep > 0.0) {
                const double step = options.gridStep;
                aligned.point = {std::round(cursor.x / step) * step, std::round(cursor.y / step) * step};
            }
            if (sameY) {
                aligned.point.y = sameY->position.y;
                aligned.alignY = sameY->ref;
            }
            if (sameX) {
                aligned.point.x = sameX->position.x;
                aligned.alignX = sameX->ref;
            }
            if (sameY) {
                aligned.guides.push_back({sameY->position, aligned.point});
            }
            if (sameX) {
                aligned.guides.push_back({sameX->position, aligned.point});
            }
            return aligned;
        }
    }

    if (options.gridStep > 0.0) {
        const double step = options.gridStep;
        return {{std::round(cursor.x / step) * step, std::round(cursor.y / step) * step}, SnapKind::Grid, {}};
    }
    return best;
}

} // namespace nuf::sketch
