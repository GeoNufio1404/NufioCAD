#include <nuf/sketch/Edit.hpp>

#include <nuf/sketch/Pick.hpp>
#include <nuf/sketch/Tools.hpp>

#include "Curves2d.hpp"

#include <algorithm>
#include <cmath>
#include <deque>
#include <functional>
#include <map>
#include <numbers>
#include <set>

namespace nuf::sketch {

namespace {

using namespace detail;

constexpr double kPi = std::numbers::pi;
constexpr double kTwoPi = 2.0 * kPi;
constexpr double kEps = 1e-9;      // parámetros
constexpr double kOnCurve = 1e-7;  // distancias (mm)

Vec2 add(Vec2 a, Vec2 b) { return {a.x + b.x, a.y + b.y}; }
Vec2 sub(Vec2 a, Vec2 b) { return {a.x - b.x, a.y - b.y}; }
Vec2 mul(double k, Vec2 v) { return {k * v.x, k * v.y}; }
double dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
double cross(Vec2 a, Vec2 b) { return a.x * b.y - a.y * b.x; }
double dist(Vec2 a, Vec2 b) { return std::hypot(b.x - a.x, b.y - a.y); }
Vec2 unit(Vec2 v)
{
    const double len = std::hypot(v.x, v.y);
    return len > 0.0 ? mul(1.0 / len, v) : Vec2{1.0, 0.0};
}
Vec2 lerp(Vec2 a, Vec2 b, double t) { return add(a, mul(t, sub(b, a))); }

double ccw(double from, double to)
{
    const double d = std::fmod(to - from, kTwoPi);
    return d < 0.0 ? d + kTwoPi : d;
}

double sweepOf(const Arc& a)
{
    double s = a.endAngle - a.startAngle;
    while (s <= 0.0) {
        s += kTwoPi;
    }
    return s;
}

bool isSplittable(const Entity& e)
{
    return !e.reference
           && (std::holds_alternative<Line>(e.geometry) || std::holds_alternative<Arc>(e.geometry)
               || std::holds_alternative<Circle>(e.geometry));
}

// Parámetro de p sobre la curva: t ∈ [0, 1] en una línea; desplazamiento
// angular desde el inicio en un arco; ángulo absoluto en [0, 2π) en un círculo.
double paramOf(const Entity& e, Vec2 p)
{
    if (const auto* l = std::get_if<Line>(&e.geometry)) {
        const Vec2 d = sub(l->end, l->start);
        return dot(sub(p, l->start), d) / dot(d, d);
    }
    if (const auto* a = std::get_if<Arc>(&e.geometry)) {
        return ccw(a->startAngle, std::atan2(p.y - a->center.y, p.x - a->center.x));
    }
    const auto& c = std::get<Circle>(e.geometry);
    return ccw(0.0, std::atan2(p.y - c.center.y, p.x - c.center.x));
}

// Extremo final del rango de parámetros (1 en líneas, barrido en arcos).
double paramEnd(const Entity& e)
{
    if (std::holds_alternative<Line>(e.geometry)) {
        return 1.0;
    }
    if (const auto* a = std::get_if<Arc>(&e.geometry)) {
        return sweepOf(*a);
    }
    return kTwoPi;
}

// Un corte: dónde y qué lo produce (un punto apoyado o el cruce con otra curva).
struct Cut {
    double param = 0.0;
    EntityId cutter = 0;
    Ref point;
};

std::vector<Cut> findCuts(const Sketch& sketch, const Entity& e)
{
    const bool closed = std::holds_alternative<Circle>(e.geometry);
    const double end = paramEnd(e);
    std::vector<Cut> cuts;
    const auto add = [&](double t, EntityId cutter, Ref point) {
        if (!closed && (t <= kEps || t >= end - kEps)) {
            return;  // en los extremos no se corta
        }
        cuts.push_back({t, cutter, point});
    };
    for (const Entity& o : sketch.entities()) {
        if (o.id == e.id || o.reference) {
            continue;
        }
        // Puntos de otras entidades apoyados en la curva.
        for (Part part : {Part::Whole, Part::Start, Part::End, Part::Center}) {
            const auto p = sketch.point({o.id, part});
            if (p && distanceToCurve(e, *p) < kOnCurve) {
                add(paramOf(e, *p), 0, {o.id, part});
            }
        }
        if (isSimpleCurve(o)) {
            for (const Vec2& p : intersections(curveOf(e), curveOf(o))) {
                add(paramOf(e, p), o.id, {});
            }
        }
    }
    // Ordenados y sin repetir (un punto apoyado gana al cruce en el mismo sitio).
    std::ranges::sort(cuts, [](const Cut& a, const Cut& b) {
        return a.param < b.param || (std::abs(a.param - b.param) <= kEps && !a.point.empty() && b.point.empty());
    });
    std::vector<Cut> unique;
    for (const Cut& c : cuts) {
        if (unique.empty() || c.param - unique.back().param > kEps) {
            unique.push_back(c);
        }
    }
    return unique;
}

// Une el extremo nuevo de un tramo con lo que produjo el corte.
void attachCut(Sketch& sketch, Ref endpoint, const Cut& cut)
{
    const Constraint c = !cut.point.empty() ? Constraint::coincident(endpoint, cut.point)
                                            : Constraint::pointOnObject(endpoint, cut.cutter);
    if (sketch.canAdd(c)) {
        sketch.addConstraint(c);
    }
}

// Restricciones que dejan de tener sentido si cambia la longitud de la curva
// (cotas de longitud, igualdad de longitudes, punto medio).
bool dependsOnLength(const Sketch& sketch, const Constraint& c, EntityId id)
{
    const Entity* e = sketch.entity(id);
    const bool line = e && std::holds_alternative<Line>(e->geometry);
    if (!line) {
        return false;
    }
    const bool onWhole = c.first == Ref::of(id) || c.second == Ref::of(id);
    switch (c.type) {
    case ConstraintType::Distance:
    case ConstraintType::DistanceX:
    case ConstraintType::DistanceY:
        return c.first == Ref::of(id) && c.second.empty();
    case ConstraintType::Equal:
    case ConstraintType::Midpoint:
        return onWhole;
    default:
        return false;
    }
}

void removeWhere(Sketch& sketch, auto&& predicate)
{
    std::vector<ConstraintId> doomed;
    for (const Constraint& c : sketch.constraints()) {
        if (predicate(c)) {
            doomed.push_back(c.id);
        }
    }
    for (ConstraintId id : doomed) {
        sketch.removeConstraint(id);
    }
}

bool refersTo(const Constraint& c, Ref r)
{
    return c.first == r || c.second == r || c.third == r;
}

// Divide la curva en los parámetros dados (interiores, ordenados). La
// entidad original pasa a ser el primer tramo; devuelve todos los tramos.
// joined: los tramos siguen unidos (partir); si no, se dejan sueltos para que
// quien llama quite alguno (recortar).
std::vector<EntityId> split(Sketch& sketch, EntityId id, const std::vector<double>& params, bool joined)
{
    const Entity original = *sketch.entity(id);
    std::vector<Geometry> pieces;
    std::vector<std::pair<double, double>> ranges;  // rango de parámetros de cada tramo

    if (const auto* l = std::get_if<Line>(&original.geometry)) {
        std::vector<double> t{0.0};
        t.insert(t.end(), params.begin(), params.end());
        t.push_back(1.0);
        for (std::size_t i = 0; i + 1 < t.size(); ++i) {
            pieces.push_back(Line{lerp(l->start, l->end, t[i]), lerp(l->start, l->end, t[i + 1])});
            ranges.push_back({t[i], t[i + 1]});
        }
    } else if (const auto* a = std::get_if<Arc>(&original.geometry)) {
        std::vector<double> t{0.0};
        t.insert(t.end(), params.begin(), params.end());
        t.push_back(sweepOf(*a));
        for (std::size_t i = 0; i + 1 < t.size(); ++i) {
            pieces.push_back(Arc{a->center, a->radius, a->startAngle + t[i], a->startAngle + t[i + 1]});
            ranges.push_back({t[i], t[i + 1]});
        }
    } else {
        const auto& c = std::get<Circle>(original.geometry);
        for (std::size_t i = 0; i < params.size(); ++i) {
            const double from = params[i];
            const double to = i + 1 < params.size() ? params[i + 1] : params.front() + kTwoPi;
            pieces.push_back(Arc{c.center, c.radius, from, to});
            ranges.push_back({from, to});
        }
    }

    std::vector<EntityId> ids{id};
    sketch.entity(id)->geometry = pieces.front();
    for (std::size_t i = 1; i < pieces.size(); ++i) {
        ids.push_back(sketch.addGeometry(pieces[i], original.construction));
        sketch.entity(ids.back())->locked = original.locked;
    }

    // Tramo que contiene un parámetro (para los puntos apoyados en la curva).
    const auto pieceAt = [&](double t) {
        for (std::size_t i = 0; i < ranges.size(); ++i) {
            const auto [from, to] = ranges[i];
            const double u = (t < from - kEps) ? t + kTwoPi : t;  // círculo: vuelta
            if (u >= from - kEps && u <= to + kEps) {
                return ids[i];
            }
        }
        return ids.front();
    };

    // Reparto de las restricciones de la curva original.
    removeWhere(sketch, [&](const Constraint& c) { return dependsOnLength(sketch, c, id); });
    for (const Constraint& c : std::vector<Constraint>(sketch.constraints())) {
        Constraint* k = sketch.constraint(c.id);
        for (Ref* r : {&k->first, &k->second, &k->third}) {
            if (r->entity == id && r->part == Part::End) {
                r->entity = ids.back();  // el final es el del último tramo
            }
        }
        if (k->type == ConstraintType::PointOnObject && k->second == Ref::of(id)) {
            if (const auto p = sketch.point(k->first)) {
                k->second.entity = pieceAt(paramOf(original, *p));
            }
        }
    }

    // Uniones entre tramos, sin restricciones redundantes.
    const bool isLine = std::holds_alternative<Line>(original.geometry);
    const bool isCircle = std::holds_alternative<Circle>(original.geometry);
    for (std::size_t i = 1; i < ids.size(); ++i) {
        if (joined) {
            sketch.addConstraint(Constraint::coincident(Ref::end(ids[i - 1]), Ref::start(ids[i])));
            if (isLine) {
                sketch.addConstraint(Constraint::parallel(ids.front(), ids[i]));  // unidos + paralelos = alineados
            } else if (isCircle) {
                sketch.addConstraint(Constraint::equal(ids.front(), ids[i]));
            } else {
                sketch.addConstraint(Constraint::concentric(ids.front(), ids[i]));
            }
        }
    }
    if (joined && isCircle) {
        sketch.addConstraint(Constraint::coincident(Ref::end(ids.back()), Ref::start(ids.front())));
    }
    return ids;
}

// Une dos tramos sueltos de la misma curva para que sigan alineados.
void keepAligned(Sketch& sketch, EntityId a, EntityId b)
{
    if (std::holds_alternative<Line>(sketch.entity(a)->geometry)) {
        sketch.addConstraint(Constraint::collinear(a, b));
    } else {
        sketch.addConstraint(Constraint::concentric(a, b));
        sketch.addConstraint(Constraint::equal(a, b));
    }
}

// Acorta una línea o un arco por un extremo hasta el parámetro t.
void cutEnd(Sketch& sketch, EntityId id, Part which, double t)
{
    Entity& e = *sketch.entity(id);
    const Entity before = e;
    if (auto* l = std::get_if<Line>(&e.geometry)) {
        const Vec2 p = lerp(l->start, l->end, t);
        (which == Part::Start ? l->start : l->end) = p;
    } else if (auto* a = std::get_if<Arc>(&e.geometry)) {
        if (which == Part::Start) {
            a->startAngle += t;
        } else {
            a->endAngle = a->startAngle + t;
        }
    }
    // Lo que se apoyaba en el extremo que se va o en el tramo eliminado.
    removeWhere(sketch, [&](const Constraint& c) {
        if (refersTo(c, {id, which}) || dependsOnLength(sketch, c, id)) {
            return true;
        }
        if (c.type == ConstraintType::PointOnObject && c.second == Ref::of(id)) {
            const auto p = sketch.point(c.first);
            if (!p) {
                return false;
            }
            const double u = paramOf(before, *p);
            return which == Part::Start ? u < t - kEps : u > t + kEps;
        }
        return false;
    });
}

} // namespace

// --- Recortar, partir y extender ---------------------------------------------------

namespace {

// --- Recorte de elipses y arcos de elipse (U9) -------------------------------------------------
// Las intersecciones exactas solo existen entre rectas y circunferencias; con una elipse se cortan las polilíneas de las
// dos curvas (muy finas) y el parámetro del cruce se afina buscando el punto de la elipse más cercano a la otra curva. El
// extremo nuevo queda sobre la curva que lo cortó (una restricción «punto sobre objeto», que el solver acaba de ajustar).

const Ellipse* ellipseOf(const Entity& e)
{
    if (const auto* el = std::get_if<Ellipse>(&e.geometry)) {
        return el;
    }
    if (const auto* ea = std::get_if<EllipseArc>(&e.geometry)) {
        return &ea->ellipse;
    }
    return nullptr;
}

// Ángulo paramétrico de un punto cercano a la elipse, en [0, 2π).
double ellipseParamOf(const Ellipse& m, Vec2 p)
{
    const double dx = p.x - m.center.x;
    const double dy = p.y - m.center.y;
    const double cr = std::cos(m.rotation);
    const double sr = std::sin(m.rotation);
    const double x = cr * dx + sr * dy;
    const double y = -sr * dx + cr * dy;
    return ccw(0.0, std::atan2(y / m.minorRadius, x / m.majorRadius));
}

// Distancia de un punto a una polilínea.
double distanceToPolyline(const std::vector<Vec2>& line, Vec2 p)
{
    double best = 1e300;
    for (std::size_t i = 0; i + 1 < line.size(); ++i) {
        const Vec2 d = sub(line[i + 1], line[i]);
        const double len2 = dot(d, d);
        const double u = len2 > 0.0 ? std::clamp(dot(sub(p, line[i]), d) / len2, 0.0, 1.0) : 0.0;
        best = std::min(best, dist(p, add(line[i], mul(u, d))));
    }
    return best;
}

// Cruce de dos segmentos: el parámetro (0..1) sobre el primero, o nullopt.
std::optional<double> segmentCross(Vec2 a, Vec2 b, Vec2 c, Vec2 d)
{
    const Vec2 r = sub(b, a);
    const Vec2 q = sub(d, c);
    const double denominator = cross(r, q);
    if (std::abs(denominator) < 1e-14) {
        return std::nullopt;
    }
    const double u = cross(sub(c, a), q) / denominator;
    const double v = cross(sub(c, a), r) / denominator;
    if (u < 0.0 || u > 1.0 || v < 0.0 || v > 1.0) {
        return std::nullopt;
    }
    return u;
}

// Cortes de una elipse (o arco): parámetros en el rango [0, 2π) de la elipse entera.
std::vector<Cut> ellipseCuts(const Sketch& sketch, const Entity& e)
{
    const Ellipse& m = *ellipseOf(e);
    constexpr int kSamples = 1440;
    std::vector<Vec2> ring;
    for (int i = 0; i <= kSamples; ++i) {
        ring.push_back(m.pointAt(kTwoPi * double(i) / kSamples));
    }
    std::vector<Cut> cuts;
    for (const Entity& o : sketch.entities()) {
        if (o.id == e.id || o.reference) {
            continue;
        }
        // Puntos de otras entidades apoyados en la curva.
        for (Part part : {Part::Whole, Part::Start, Part::End, Part::Center}) {
            const auto p = sketch.point({o.id, part});
            if (p && distanceToCurve(e, *p) < kOnCurve) {
                cuts.push_back({ellipseParamOf(m, *p), 0, {o.id, part}});
            }
        }
        if (std::holds_alternative<Point>(o.geometry)) {
            continue;
        }
        const std::vector<Vec2> other = polyline(o.geometry);
        if (other.size() < 2) {
            continue;
        }
        for (int i = 0; i < kSamples; ++i) {
            for (std::size_t j = 0; j + 1 < other.size(); ++j) {
                const auto u = segmentCross(ring[std::size_t(i)], ring[std::size_t(i) + 1], other[j], other[j + 1]);
                if (!u) {
                    continue;
                }
                // Afinar: el punto de la elipse, entre los dos de la polilínea, más cercano a la otra curva.
                double lo = kTwoPi * (double(i) + std::max(0.0, *u - 0.5)) / kSamples;
                double hi = kTwoPi * (double(i) + std::min(1.0, *u + 0.5)) / kSamples;
                for (int k = 0; k < 40; ++k) {
                    const double m1 = lo + (hi - lo) / 3.0;
                    const double m2 = hi - (hi - lo) / 3.0;
                    if (distanceToPolyline(other, m.pointAt(m1)) < distanceToPolyline(other, m.pointAt(m2))) {
                        hi = m2;
                    } else {
                        lo = m1;
                    }
                }
                cuts.push_back({ccw(0.0, 0.5 * (lo + hi)), o.id, {}});
            }
        }
    }
    std::ranges::sort(cuts, [](const Cut& a, const Cut& b) {
        return a.param < b.param || (std::abs(a.param - b.param) <= 1e-6 && !a.point.empty() && b.point.empty());
    });
    std::vector<Cut> unique;
    for (const Cut& c : cuts) {
        if (unique.empty() || c.param - unique.back().param > 1e-6) {
            unique.push_back(c);
        }
    }
    return unique;
}

bool trimEllipse(Sketch& sketch, EntityId id, Vec2 click)
{
    const Entity* e = sketch.entity(id);
    if (e == nullptr || e->reference || ellipseOf(*e) == nullptr) {
        return false;
    }
    const Ellipse m = *ellipseOf(*e);
    const std::vector<Cut> all = ellipseCuts(sketch, *e);
    const double tc = ellipseParamOf(m, click);
    const auto* arc = std::get_if<EllipseArc>(&e->geometry);
    const double begin = arc != nullptr ? ccw(0.0, arc->startParam) : 0.0;
    const double sweep = arc != nullptr ? ccw(arc->startParam, arc->endParam) : kTwoPi;
    // Solo cuentan los cortes dentro del arco (y no en sus extremos).
    std::vector<Cut> cuts;
    for (const Cut& c : all) {
        const double along = ccw(begin, c.param);
        if (arc == nullptr || (along > 1e-6 && along < sweep - 1e-6)) {
            cuts.push_back(c);
        }
    }
    const auto newArc = [&](double from, double to) { return EllipseArc{m, from, to}; };

    if (arc == nullptr) {
        // Elipse entera: se queda el tramo que no contiene el clic, de un corte al siguiente.
        if (cuts.size() < 2) {
            sketch.removeEntity(id);
            return true;
        }
        const auto after = std::ranges::find_if(cuts, [&](const Cut& c) { return c.param > tc; });
        const Cut b = after == cuts.end() ? cuts.front() : *after;
        const Cut a = after == cuts.begin() || after == cuts.end() ? cuts.back() : *(after - 1);
        sketch.entity(id)->geometry = newArc(b.param, b.param + ccw(b.param, a.param));
        attachCut(sketch, Ref::start(id), b);
        attachCut(sketch, Ref::end(id), a);
        return true;
    }

    // Arco de elipse: los cortes a cada lado del clic (medidos desde el inicio del arco).
    const double clickAlong = ccw(begin, tc);
    const Cut* before = nullptr;
    const Cut* next = nullptr;
    for (const Cut& c : cuts) {
        if (ccw(begin, c.param) < clickAlong) {
            before = &c;
        } else if (next == nullptr) {
            next = &c;
        }
    }
    if (before == nullptr && next == nullptr) {
        sketch.removeEntity(id);
        return true;
    }
    const double startParam = arc->startParam;
    const double endParam = arc->endParam;
    if (before != nullptr && next != nullptr) {
        const Cut a = *before;
        const Cut b = *next;
        const bool construction = e->construction;
        // Se quita el tramo del medio: quedan los dos de los lados.
        sketch.removeEntity(id);
        const EntityId first = sketch.addEllipseArc(newArc(startParam, a.param + (ccw(startParam, a.param) < 1e-9 ? kTwoPi : 0.0)), construction);
        const EntityId second = sketch.addEllipseArc(newArc(b.param, endParam), construction);
        attachCut(sketch, Ref::end(first), a);
        attachCut(sketch, Ref::start(second), b);
        return true;
    }
    if (next != nullptr) {
        // Se quita el tramo del inicio.
        sketch.entity(id)->geometry = newArc(next->param, endParam);
        attachCut(sketch, Ref::start(id), *next);
        return true;
    }
    sketch.entity(id)->geometry = newArc(startParam, before->param);
    attachCut(sketch, Ref::end(id), *before);
    return true;
}

} // namespace

bool trim(Sketch& sketch, EntityId id, Vec2 click)
{
    const Entity* e = sketch.entity(id);
    if (e != nullptr && ellipseOf(*e) != nullptr) {
        return trimEllipse(sketch, id, click);
    }
    if (!e || !isSplittable(*e)) {
        return false;
    }
    const std::vector<Cut> cuts = findCuts(sketch, *e);
    const double tc = paramOf(*e, click);

    if (std::holds_alternative<Circle>(e->geometry)) {
        if (cuts.size() < 2) {
            sketch.removeEntity(id);
            return true;
        }
        // Cortes a ambos lados del clic, dando la vuelta si hace falta.
        const auto after = std::ranges::find_if(cuts, [&](const Cut& c) { return c.param > tc; });
        const Cut b = after == cuts.end() ? cuts.front() : *after;
        const Cut a = after == cuts.begin() || after == cuts.end() ? cuts.back() : *(after - 1);
        // Queda el arco de b a a: el círculo se convierte en ese arco.
        const auto& c = std::get<Circle>(e->geometry);
        const double from = b.param;
        const double to = from + ccw(b.param, a.param);
        const Entity before = *e;
        sketch.entity(id)->geometry = Arc{c.center, c.radius, from, to};
        removeWhere(sketch, [&](const Constraint& k) {
            if (k.type != ConstraintType::PointOnObject || k.second != Ref::of(id)) {
                return false;
            }
            const auto p = sketch.point(k.first);
            return p && ccw(from, paramOf(before, *p)) > to - from + kEps;
        });
        attachCut(sketch, Ref::start(id), b);
        attachCut(sketch, Ref::end(id), a);
        return true;
    }

    const Cut* a = nullptr;
    const Cut* b = nullptr;
    for (const Cut& c : cuts) {
        if (c.param < tc) {
            a = &c;
        } else if (!b) {
            b = &c;
        }
    }
    if (!a && !b) {
        sketch.removeEntity(id);
        return true;
    }
    if (a && b) {
        const Cut ca = *a;
        const Cut cb = *b;
        const auto pieces = split(sketch, id, {ca.param, cb.param}, false);
        sketch.removeEntity(pieces[1]);
        keepAligned(sketch, pieces[0], pieces[2]);
        attachCut(sketch, Ref::end(pieces[0]), ca);
        attachCut(sketch, Ref::start(pieces[2]), cb);
        return true;
    }
    if (b) {
        // Se quita el tramo del inicio: el inicio pasa al corte.
        const Cut cb = *b;
        cutEnd(sketch, id, Part::Start, cb.param);
        attachCut(sketch, Ref::start(id), cb);
    } else {
        const Cut ca = *a;
        cutEnd(sketch, id, Part::End, ca.param);
        attachCut(sketch, Ref::end(id), ca);
    }
    return true;
}

bool breakCurve(Sketch& sketch, EntityId id, Vec2 click)
{
    const Entity* e = sketch.entity(id);
    if (!e || !isSplittable(*e)) {
        return false;
    }
    const std::vector<Cut> cuts = findCuts(sketch, *e);
    const double tc = paramOf(*e, click);
    std::vector<Cut> around;
    if (std::holds_alternative<Circle>(e->geometry)) {
        if (cuts.size() < 2) {
            return false;
        }
        const auto after = std::ranges::find_if(cuts, [&](const Cut& c) { return c.param > tc; });
        const Cut b = after == cuts.end() ? cuts.front() : *after;
        const Cut a = after == cuts.begin() || after == cuts.end() ? cuts.back() : *(after - 1);
        around = {a, b};
    } else {
        for (const Cut& c : cuts) {
            if (c.param < tc) {
                around = {c};
            }
        }
        for (const Cut& c : cuts) {
            if (c.param > tc) {
                around.push_back(c);
                break;
            }
        }
        if (around.empty()) {
            return false;
        }
    }
    std::vector<double> params;
    for (const Cut& c : around) {
        params.push_back(c.param);
    }
    std::ranges::sort(params);  // en un círculo, ángulos absolutos
    split(sketch, id, params, true);
    return true;
}

bool extend(Sketch& sketch, EntityId id, Vec2 click)
{
    const Entity* e = sketch.entity(id);
    if (!e || e->reference) {
        return false;
    }
    // Curvas contra las que se puede chocar (también los ejes).
    const auto targets = [&](const Curve& probe) {
        std::vector<std::pair<Vec2, EntityId>> hits;
        for (const Entity& o : sketch.entities()) {
            if (o.id != id && isSimpleCurve(o)) {
                for (const Vec2& p : intersections(probe, curveOf(o))) {
                    hits.push_back({p, o.id});
                }
            }
        }
        return hits;
    };

    if (const auto* l = std::get_if<Line>(&e->geometry)) {
        const Part which = dist(click, l->start) < dist(click, l->end) ? Part::Start : Part::End;
        const Vec2 from = which == Part::Start ? l->start : l->end;
        const Vec2 u = unit(which == Part::Start ? sub(l->start, l->end) : sub(l->end, l->start));
        Curve ray = curveOf(*e);
        ray.infinite = true;
        std::optional<std::pair<double, EntityId>> best;
        for (const auto& [p, hit] : targets(ray)) {
            const double s = dot(sub(p, from), u);
            if (s > kOnCurve && (!best || s < best->first)) {
                best = {s, hit};
            }
        }
        if (!best) {
            return false;
        }
        const Vec2 to = add(from, mul(best->first, u));
        removeWhere(sketch, [&](const Constraint& c) { return refersTo(c, {id, which}) || dependsOnLength(sketch, c, id); });
        auto& line = std::get<Line>(sketch.entity(id)->geometry);
        (which == Part::Start ? line.start : line.end) = to;
        sketch.addConstraint(Constraint::pointOnObject({id, which}, best->second));
        return true;
    }
    if (const auto* a = std::get_if<Arc>(&e->geometry)) {
        const Part which = dist(click, a->startPoint()) < dist(click, a->endPoint()) ? Part::Start : Part::End;
        Curve full = curveOf(*e);
        full.isArc = false;
        const double room = kTwoPi - sweepOf(*a);
        std::optional<std::pair<double, EntityId>> best;
        for (const auto& [p, hit] : targets(full)) {
            const double angle = std::atan2(p.y - a->center.y, p.x - a->center.x);
            const double delta = which == Part::End ? ccw(a->endAngle, angle) : ccw(angle, a->startAngle);
            if (delta > kEps && delta < room - kEps && (!best || delta < best->first)) {
                best = {delta, hit};
            }
        }
        if (!best) {
            return false;
        }
        removeWhere(sketch, [&](const Constraint& c) { return refersTo(c, {id, which}); });
        auto& arc = std::get<Arc>(sketch.entity(id)->geometry);
        if (which == Part::End) {
            arc.endAngle += best->first;
        } else {
            arc.startAngle -= best->first;
        }
        sketch.addConstraint(Constraint::pointOnObject({id, which}, best->second));
        return true;
    }
    return false;
}

// --- Empalme y chaflán ---------------------------------------------------------------

namespace {

// Las dos líneas que se unen en «corner»: su extremo allí y la dirección
// en que se alejan de la esquina.
struct Corner {
    Ref ends[2];
    Vec2 directions[2];
    double lengths[2];
    Vec2 at;
};

std::optional<Corner> cornerAt(const Sketch& sketch, Vec2 corner, double tolerance)
{
    Corner c;
    int found = 0;
    for (const Entity& e : sketch.entities()) {
        const auto* l = std::get_if<Line>(&e.geometry);
        if (!l || e.reference) {
            continue;
        }
        for (Part which : {Part::Start, Part::End}) {
            const Vec2 p = which == Part::Start ? l->start : l->end;
            const Vec2 other = which == Part::Start ? l->end : l->start;
            if (dist(p, corner) <= tolerance) {
                if (found == 2) {
                    return std::nullopt;  // más de dos líneas: ambiguo
                }
                c.ends[found] = {e.id, which};
                c.directions[found] = unit(sub(other, p));
                c.lengths[found] = dist(p, other);
                c.at = p;
                ++found;
            }
        }
    }
    if (found != 2 || c.ends[0].entity == c.ends[1].entity) {
        return std::nullopt;
    }
    return c;
}

// Mueve el extremo de una línea y quita lo que dependía de él.
void moveCornerEnd(Sketch& sketch, Ref end, Vec2 to)
{
    removeWhere(sketch, [&](const Constraint& c) { return refersTo(c, end) || dependsOnLength(sketch, c, end.entity); });
    auto& line = std::get<Line>(sketch.entity(end.entity)->geometry);
    (end.part == Part::Start ? line.start : line.end) = to;
}

} // namespace

std::optional<EntityId> fillet(Sketch& sketch, Vec2 corner, double radius, double tolerance)
{
    const auto c = cornerAt(sketch, corner, tolerance);
    if (!c || !(radius > 0.0)) {
        return std::nullopt;
    }
    const double cosPhi = std::clamp(dot(c->directions[0], c->directions[1]), -1.0, 1.0);
    const double phi = std::acos(cosPhi);
    if (phi < 1e-6 || phi > kPi - 1e-6) {
        return std::nullopt;  // líneas alineadas
    }
    const double d = radius / std::tan(phi / 2.0);
    if (d >= c->lengths[0] || d >= c->lengths[1]) {
        return std::nullopt;  // el empalme no cabe
    }
    const Vec2 p1 = add(c->at, mul(d, c->directions[0]));
    const Vec2 p2 = add(c->at, mul(d, c->directions[1]));
    const Vec2 bisector = unit(add(c->directions[0], c->directions[1]));
    const Vec2 center = add(c->at, mul(radius / std::sin(phi / 2.0), bisector));
    const double a1 = std::atan2(p1.y - center.y, p1.x - center.x);
    const double a2 = std::atan2(p2.y - center.y, p2.x - center.x);
    // El arco corto entre los dos puntos de tangencia (barrido π − φ).
    const bool fromFirst = ccw(a1, a2) <= kPi;
    const double start = fromFirst ? a1 : a2;
    const double sweep = fromFirst ? ccw(a1, a2) : ccw(a2, a1);

    moveCornerEnd(sketch, c->ends[0], p1);
    moveCornerEnd(sketch, c->ends[1], p2);
    const EntityId arc = sketch.addArc(center, radius, start, start + sweep);
    sketch.addConstraint(Constraint::tangentAt(c->ends[0], fromFirst ? Ref::start(arc) : Ref::end(arc)));
    sketch.addConstraint(Constraint::tangentAt(c->ends[1], fromFirst ? Ref::end(arc) : Ref::start(arc)));
    sketch.addConstraint(Constraint::radius(arc, radius));
    return arc;
}

std::optional<EntityId> chamfer(Sketch& sketch, Vec2 corner, double distance, double tolerance)
{
    const auto c = cornerAt(sketch, corner, tolerance);
    if (!c || !(distance > 0.0) || distance >= c->lengths[0] || distance >= c->lengths[1]) {
        return std::nullopt;
    }
    if (std::abs(cross(c->directions[0], c->directions[1])) < 1e-9) {
        return std::nullopt;
    }
    const Vec2 p1 = add(c->at, mul(distance, c->directions[0]));
    const Vec2 p2 = add(c->at, mul(distance, c->directions[1]));
    moveCornerEnd(sketch, c->ends[0], p1);
    moveCornerEnd(sketch, c->ends[1], p2);
    const EntityId line = sketch.addLine(p1, p2);
    sketch.addConstraint(Constraint::coincident(c->ends[0], Ref::start(line)));
    sketch.addConstraint(Constraint::coincident(c->ends[1], Ref::end(line)));
    return line;
}

// --- Desfase -------------------------------------------------------------------------

std::vector<EntityId> chainOf(const Sketch& sketch, EntityId id)
{
    const auto endpoints = [&](EntityId e) -> std::optional<std::pair<Vec2, Vec2>> {
        const auto s = sketch.point(Ref::start(e));
        const auto t = sketch.point(Ref::end(e));
        if (!s || !t) {
            return std::nullopt;
        }
        return std::pair{*s, *t};
    };
    const Entity* first = sketch.entity(id);
    if (!first || first->reference) {
        return {};
    }
    if (std::holds_alternative<Circle>(first->geometry)) {
        return {id};
    }
    std::deque<EntityId> chain{id};
    std::set<EntityId> used{id};
    const auto ends = endpoints(id);
    if (!ends) {
        return {id};
    }
    // Hacia delante desde el final y hacia atrás desde el inicio.
    for (Vec2 tip : {ends->second, ends->first}) {
        const bool forward = tip.x == ends->second.x && tip.y == ends->second.y;
        for (bool grew = true; grew;) {
            grew = false;
            for (const Entity& e : sketch.entities()) {
                if (used.contains(e.id) || e.reference || !(std::holds_alternative<Line>(e.geometry) || std::holds_alternative<Arc>(e.geometry))) {
                    continue;
                }
                const auto pe = endpoints(e.id);
                if (!pe) {
                    continue;
                }
                const bool touchesStart = dist(pe->first, tip) < kOnCurve;
                const bool touchesEnd = dist(pe->second, tip) < kOnCurve;
                if (!touchesStart && !touchesEnd) {
                    continue;
                }
                used.insert(e.id);
                if (forward) {
                    chain.push_back(e.id);
                } else {
                    chain.push_front(e.id);
                }
                tip = touchesStart ? pe->second : pe->first;
                grew = true;
                break;
            }
        }
    }
    return {chain.begin(), chain.end()};
}

std::vector<EntityId> offset(Sketch& sketch, const std::vector<EntityId>& chain, double distance)
{
    if (chain.empty() || std::abs(distance) < kOnCurve) {
        return {};
    }
    // Una elipse, un arco de elipse o una spline sola: el desfase de estas curvas no es de su misma clase, así que se
    // muestrea y se aproxima con una spline que pasa por los puntos (a la izquierda del sentido de recorrido; en la
    // elipse completa, hacia dentro con distancia positiva).
    if (chain.size() == 1) {
        const Entity* e = sketch.entity(chain.front());
        if (e && (std::holds_alternative<Ellipse>(e->geometry) || std::holds_alternative<EllipseArc>(e->geometry) ||
                  std::holds_alternative<BSpline>(e->geometry))) {
            const auto shifted = [&](const std::function<Vec2(double)>& at, int count, bool closedCurve) -> std::vector<Vec2> {
                std::vector<Vec2> out;
                const double h = 1e-4;
                for (int i = 0; i <= count; ++i) {
                    const double u = double(i) / count;
                    const Vec2 p = at(u);
                    Vec2 tangent = sub(at(std::min(1.0, u + h)), at(std::max(0.0, u - h)));
                    if (closedCurve && (i == 0 || i == count)) {
                        tangent = sub(at(h), at(1.0 - h));
                    }
                    const double length = std::hypot(tangent.x, tangent.y);
                    if (length < 1e-12) {
                        return {};
                    }
                    out.push_back(add(p, mul(distance / length, Vec2{-tangent.y, tangent.x})));
                }
                if (closedCurve) {
                    out.back() = out.front();
                }
                return out;
            };
            std::vector<Vec2> points;
            if (const auto* el = std::get_if<Ellipse>(&e->geometry)) {
                if (distance > 0.0 && distance >= el->minorRadius * el->minorRadius / el->majorRadius) {
                    return {};  // hacia dentro más allá de la curvatura mínima: se formarían picos
                }
                points = shifted([el](double u) { return el->pointAt(u * 2.0 * std::numbers::pi); }, 96, true);
            } else if (const auto* arc = std::get_if<EllipseArc>(&e->geometry)) {
                double end = arc->endParam;
                while (end <= arc->startParam) {
                    end += 2.0 * std::numbers::pi;
                }
                points = shifted([arc, end](double u) { return arc->ellipse.pointAt(arc->startParam + u * (end - arc->startParam)); }, 64, false);
            } else if (const auto* spline = std::get_if<BSpline>(&e->geometry)) {
                points = shifted([spline](double u) { return spline->pointAt(u); }, 64, false);
            }
            if (points.size() < 4) {
                return {};
            }
            BSpline fitted = interpolatingSpline(points);
            return {sketch.addBSpline(fitted, e->construction)};
        }
    }
    // Sentido de recorrido de cada curva para que la cadena sea continua.
    struct Item {
        EntityId id;
        bool reversed;
        Geometry geometry;  // desfasada (con la orientación de la original)
    };
    std::vector<Item> items;
    Vec2 tip;
    for (std::size_t i = 0; i < chain.size(); ++i) {
        const Entity* e = sketch.entity(chain[i]);
        if (!e) {
            return {};
        }
        bool reversed = false;
        const auto s = sketch.point(Ref::start(e->id));
        const auto t = sketch.point(Ref::end(e->id));
        if (i == 0 && chain.size() > 1 && s && t) {
            // La primera se orienta hacia la segunda.
            const auto s2 = sketch.point(Ref::start(chain[1]));
            const auto t2 = sketch.point(Ref::end(chain[1]));
            const bool startTouches = (s2 && dist(*s, *s2) < kOnCurve) || (t2 && dist(*s, *t2) < kOnCurve);
            reversed = startTouches;
        } else if (i > 0 && s && t) {
            reversed = dist(*t, tip) < kOnCurve;
        }
        if (s && t) {
            tip = reversed ? *s : *t;
        }
        items.push_back({e->id, reversed, e->geometry});
    }

    // Cada curva, desplazada a la izquierda de su sentido de recorrido.
    for (Item& it : items) {
        if (auto* l = std::get_if<Line>(&it.geometry)) {
            const Vec2 dir = it.reversed ? sub(l->start, l->end) : sub(l->end, l->start);
            const Vec2 n = mul(distance, unit(Vec2{-dir.y, dir.x}));
            *l = {add(l->start, n), add(l->end, n)};
        } else if (auto* a = std::get_if<Arc>(&it.geometry)) {
            a->radius += it.reversed ? distance : -distance;
            if (!(a->radius > kOnCurve)) {
                return {};
            }
        } else if (auto* c = std::get_if<Circle>(&it.geometry)) {
            c->radius -= distance;
            if (!(c->radius > kOnCurve)) {
                return {};
            }
        } else {
            return {};  // elipses y splines: no soportado
        }
    }
    // Extremo de recorrido (final o inicio) de un tramo desfasado.
    const auto travelEnd = [](Item& it, bool end) -> Vec2* {
        auto* l = std::get_if<Line>(&it.geometry);
        if (!l) {
            return nullptr;
        }
        return (end != it.reversed) ? &l->end : &l->start;
    };
    const auto pointAt = [](const Item& it, bool end) -> std::optional<Vec2> {
        const bool atEnd = end != it.reversed;
        if (const auto* l = std::get_if<Line>(&it.geometry)) {
            return atEnd ? l->end : l->start;
        }
        if (const auto* a = std::get_if<Arc>(&it.geometry)) {
            return atEnd ? a->endPoint() : a->startPoint();
        }
        return std::nullopt;
    };
    const auto first = sketch.point(items.front().reversed ? Ref::end(items.front().id) : Ref::start(items.front().id));
    const bool closed = items.size() > 1 && first && dist(*first, tip) < kOnCurve;
    const std::size_t joints = closed ? items.size() : items.size() - 1;
    // Esquinas entre dos líneas: se prolongan hasta cortarse.
    for (std::size_t i = 0; i < joints; ++i) {
        Item& a = items[i];
        Item& b = items[(i + 1) % items.size()];
        Vec2* ea = travelEnd(a, true);
        Vec2* sb = travelEnd(b, false);
        if (!ea || !sb) {
            continue;
        }
        const auto& la = std::get<Line>(a.geometry);
        const auto& lb = std::get<Line>(b.geometry);
        const Vec2 da = sub(la.end, la.start);
        const Vec2 db = sub(lb.end, lb.start);
        const double denom = cross(da, db);
        if (std::abs(denom) < 1e-12) {
            continue;
        }
        const double t = cross(sub(lb.start, la.start), db) / denom;
        const Vec2 x = add(la.start, mul(t, da));
        *ea = x;
        *sb = x;
    }

    std::vector<EntityId> created;
    for (const Item& it : items) {
        const Entity& src = *sketch.entity(it.id);
        EntityId dst = 0;
        if (const auto* l = std::get_if<Line>(&it.geometry)) {
            dst = sketch.addLine(l->start, l->end, src.construction);
            sketch.addConstraint(Constraint::parallel(it.id, dst));
            sketch.addConstraint(Constraint::distance(Ref::start(dst), Ref::of(it.id), std::abs(distance)));
        } else if (const auto* a = std::get_if<Arc>(&it.geometry)) {
            dst = sketch.addArc(a->center, a->radius, a->startAngle, a->endAngle, src.construction);
            sketch.addConstraint(Constraint::concentric(it.id, dst));
        } else if (const auto* c = std::get_if<Circle>(&it.geometry)) {
            dst = sketch.addCircle(c->center, c->radius, src.construction);
            sketch.addConstraint(Constraint::concentric(it.id, dst));
        }
        created.push_back(dst);
    }
    // Uniones: donde los tramos desfasados se tocan.
    for (std::size_t i = 0; i < joints; ++i) {
        const std::size_t j = (i + 1) % items.size();
        const auto pa = pointAt(items[i], true);
        const auto pb = pointAt(items[j], false);
        if (pa && pb && dist(*pa, *pb) < 1e-6) {
            const Ref ra = items[i].reversed ? Ref::start(created[i]) : Ref::end(created[i]);
            const Ref rb = items[j].reversed ? Ref::end(created[j]) : Ref::start(created[j]);
            sketch.addConstraint(Constraint::coincident(ra, rb));
        }
    }
    return created;
}

// --- Simetría ------------------------------------------------------------------------

std::vector<EntityId> mirror(Sketch& sketch, const std::vector<EntityId>& entities, EntityId axisId)
{
    const Entity* axisEntity = sketch.entity(axisId);
    const auto* axis = axisEntity ? std::get_if<Line>(&axisEntity->geometry) : nullptr;
    if (!axis) {
        return {};
    }
    const Vec2 o = axis->start;
    const Vec2 u = unit(sub(axis->end, axis->start));
    const double alpha = std::atan2(u.y, u.x);
    const auto reflect = [&](Vec2 p) {
        const Vec2 d = sub(p, o);
        return sub(add(o, mul(2.0 * dot(d, u), u)), d);
    };
    const auto reflectAngle = [&](double a) { return 2.0 * alpha - a; };
    const Ref axisRef = Ref::of(axisId);
    bool constrain = true;  // lo bloqueado se copia bloqueado, sin restricciones de simetría
    const auto symmetric = [&](Ref a, Ref b) {
        if (constrain) {
            sketch.addConstraint(Constraint::symmetric(a, b, axisRef));
        }
    };

    std::vector<EntityId> created;
    for (EntityId id : entities) {
        const Entity* e = sketch.entity(id);
        if (!e || e->reference || id == axisId) {
            continue;
        }
        const Entity src = *e;
        constrain = !src.locked;
        EntityId dst = 0;
        if (const auto* p = std::get_if<Point>(&src.geometry)) {
            dst = sketch.addPoint(reflect(p->position), src.construction);
            symmetric(Ref::of(id), Ref::of(dst));
        } else if (const auto* l = std::get_if<Line>(&src.geometry)) {
            dst = sketch.addLine(reflect(l->start), reflect(l->end), src.construction);
            symmetric(Ref::start(id), Ref::start(dst));
            symmetric(Ref::end(id), Ref::end(dst));
        } else if (const auto* c = std::get_if<Circle>(&src.geometry)) {
            dst = sketch.addCircle(reflect(c->center), c->radius, src.construction);
            symmetric(Ref::center(id), Ref::center(dst));
            if (constrain) {
                sketch.addConstraint(Constraint::equal(id, dst));
            }
        } else if (const auto* a = std::get_if<Arc>(&src.geometry)) {
            // Al reflejar se invierte el sentido: el inicio nuevo es el final reflejado.
            dst = sketch.addArc(reflect(a->center), a->radius, reflectAngle(a->endAngle), reflectAngle(a->startAngle),
                                src.construction);
            symmetric(Ref::start(id), Ref::end(dst));
            symmetric(Ref::end(id), Ref::start(dst));
            if (constrain) {
                sketch.addConstraint(Constraint::equal(id, dst));
            }
        } else if (const auto* el = std::get_if<Ellipse>(&src.geometry)) {
            dst = sketch.addEllipse({reflect(el->center), el->majorRadius, el->minorRadius, reflectAngle(el->rotation)},
                                    src.construction);
            symmetric(Ref::center(id), Ref::center(dst));
        } else if (const auto* ea = std::get_if<EllipseArc>(&src.geometry)) {
            const Ellipse& m = ea->ellipse;
            dst = sketch.addEllipseArc({{reflect(m.center), m.majorRadius, m.minorRadius, reflectAngle(m.rotation)},
                                        -ea->endParam, -ea->startParam},
                                       src.construction);
            symmetric(Ref::start(id), Ref::end(dst));
            symmetric(Ref::end(id), Ref::start(dst));
        } else if (const auto* b = std::get_if<BSpline>(&src.geometry)) {
            BSpline copy = *b;
            for (Vec2& pole : copy.poles) {
                pole = reflect(pole);
            }
            dst = sketch.addBSpline(copy, src.construction);
            const int n = int(copy.poles.size());
            for (int i = 0; i < n; ++i) {
                const Ref ra = i == 0 ? Ref::start(id) : (i == n - 1 ? Ref::end(id) : Ref::pole(id, i));
                const Ref rb = i == 0 ? Ref::start(dst) : (i == n - 1 ? Ref::end(dst) : Ref::pole(dst, i));
                symmetric(ra, rb);
            }
        }
        if (dst != 0) {
            sketch.entity(dst)->locked = src.locked;
            created.push_back(dst);
        }
    }
    return created;
}

// --- Transformaciones y patrones --------------------------------------------------------

Vec2 Similarity::apply(Vec2 p) const
{
    const Vec2 d = sub(p, pivot);
    const double c = std::cos(angle);
    const double s = std::sin(angle);
    return add(add(pivot, mul(scale, Vec2{d.x * c - d.y * s, d.x * s + d.y * c})), translation);
}

Geometry transformed(const Geometry& g, const Similarity& t)
{
    Geometry out = g;
    if (auto* p = std::get_if<Point>(&out)) {
        p->position = t.apply(p->position);
    } else if (auto* l = std::get_if<Line>(&out)) {
        *l = {t.apply(l->start), t.apply(l->end)};
    } else if (auto* c = std::get_if<Circle>(&out)) {
        *c = {t.apply(c->center), c->radius * t.scale};
    } else if (auto* a = std::get_if<Arc>(&out)) {
        *a = {t.apply(a->center), a->radius * t.scale, a->startAngle + t.angle, a->endAngle + t.angle};
    } else if (auto* e = std::get_if<Ellipse>(&out)) {
        *e = {t.apply(e->center), e->majorRadius * t.scale, e->minorRadius * t.scale, e->rotation + t.angle};
    } else if (auto* ea = std::get_if<EllipseArc>(&out)) {
        Ellipse& m = ea->ellipse;
        m = {t.apply(m.center), m.majorRadius * t.scale, m.minorRadius * t.scale, m.rotation + t.angle};
    } else if (auto* b = std::get_if<BSpline>(&out)) {
        for (Vec2& pole : b->poles) {
            pole = t.apply(pole);
        }
    }
    return out;
}

namespace {

bool isLengthDimension(ConstraintType type)
{
    return type == ConstraintType::Distance || type == ConstraintType::DistanceX || type == ConstraintType::DistanceY
           || type == ConstraintType::Radius || type == ConstraintType::Diameter
           || type == ConstraintType::MajorRadius || type == ConstraintType::MinorRadius;
}

// ¿Gira lo suficiente como para romper horizontales y verticales?
bool breaksAxes(const Similarity& t)
{
    return std::abs(std::remainder(t.angle, kPi / 2.0)) > 1e-9;
}

// Una restricción solo entre entidades del conjunto (o con referencias vacías).
bool isInternal(const Constraint& c, const std::set<EntityId>& set)
{
    for (const Ref& r : {c.first, c.second, c.third}) {
        if (!r.empty() && !set.contains(r.entity)) {
            return false;
        }
    }
    return true;
}

} // namespace

std::vector<EntityId> transform(Sketch& sketch, const std::vector<EntityId>& entities, const Similarity& t, bool copy)
{
    std::set<EntityId> set;
    for (EntityId id : entities) {
        const Entity* e = sketch.entity(id);
        if (e && !e->reference) {
            set.insert(id);
        }
    }
    if (set.empty()) {
        return {};
    }
    const bool rotates = breaksAxes(t);
    const auto adjust = [&](Constraint& c) -> bool {  // false = no conservar
        if (rotates && (c.type == ConstraintType::Horizontal || c.type == ConstraintType::Vertical
                        || c.type == ConstraintType::DistanceX || c.type == ConstraintType::DistanceY)) {
            return false;
        }
        if (isLengthDimension(c.type)) {
            c.value *= t.scale;
        }
        return true;
    };

    if (!copy) {
        for (EntityId id : set) {
            Entity* e = sketch.entity(id);
            e->geometry = transformed(e->geometry, t);
        }
        std::vector<ConstraintId> doomed;
        for (const Constraint& c : std::vector<Constraint>(sketch.constraints())) {
            if (isInternal(c, set) && !adjust(*sketch.constraint(c.id))) {
                doomed.push_back(c.id);
            }
        }
        for (ConstraintId id : doomed) {
            sketch.removeConstraint(id);
        }
        return {set.begin(), set.end()};
    }

    std::map<EntityId, EntityId> mapping;
    for (EntityId id : set) {
        const Entity e = *sketch.entity(id);
        const Geometry g = transformed(e.geometry, t);
        mapping[id] = sketch.addGeometry(g, e.construction);
        sketch.entity(mapping[id])->locked = e.locked;
    }
    for (const Constraint& c : std::vector<Constraint>(sketch.constraints())) {
        if (!isInternal(c, set)) {
            continue;
        }
        Constraint k = c;
        if (!adjust(k)) {
            continue;
        }
        for (Ref* r : {&k.first, &k.second, &k.third}) {
            if (!r->empty()) {
                r->entity = mapping.at(r->entity);
            }
        }
        if (sketch.canAdd(k)) {
            sketch.addConstraint(k);
        }
    }
    std::vector<EntityId> created;
    for (const auto& [src, dst] : mapping) {
        created.push_back(dst);
    }
    return created;
}

std::vector<EntityId> rectangularPattern(Sketch& sketch, const std::vector<EntityId>& entities, int columns, int rows,
                                         Vec2 step)
{
    std::vector<EntityId> created;
    for (int j = 0; j < std::max(1, rows); ++j) {
        for (int i = 0; i < std::max(1, columns); ++i) {
            if (i == 0 && j == 0) {
                continue;
            }
            Similarity t;
            t.translation = {i * step.x, j * step.y};
            const auto copies = transform(sketch, entities, t, true);
            created.insert(created.end(), copies.begin(), copies.end());
        }
    }
    return created;
}

std::vector<EntityId> circularPattern(Sketch& sketch, const std::vector<EntityId>& entities, Vec2 center, int count,
                                      double totalAngle)
{
    std::vector<EntityId> created;
    if (count < 2) {
        return created;
    }
    // Vuelta completa: las copias se reparten sin repetir la original.
    const bool full = std::abs(std::abs(totalAngle) - kTwoPi) < 1e-9;
    const double step = full ? totalAngle / count : totalAngle / (count - 1);
    for (int k = 1; k < count; ++k) {
        Similarity t;
        t.pivot = center;
        t.angle = step * k;
        const auto copies = transform(sketch, entities, t, true);
        created.insert(created.end(), copies.begin(), copies.end());
    }
    return created;
}

} // namespace nuf::sketch
