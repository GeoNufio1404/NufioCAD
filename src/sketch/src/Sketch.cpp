#include <nuf/sketch/Sketch.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>
#include <utility>

namespace nuf::sketch {

namespace {

Vec2 polar(Vec2 center, double radius, double angle)
{
    return {center.x + radius * std::cos(angle), center.y + radius * std::sin(angle)};
}

double cross(Vec2 a, Vec2 b) { return a.x * b.y - a.y * b.x; }
double dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
Vec2 operator-(Vec2 a, Vec2 b) { return {a.x - b.x, a.y - b.y}; }

[[noreturn]] void invalid(const char* message)
{
    throw std::invalid_argument(message);
}

} // namespace

Vec2 Arc::startPoint() const { return polar(center, radius, startAngle); }
Vec2 Arc::endPoint() const { return polar(center, radius, endAngle); }

Vec2 Ellipse::pointAt(double t) const
{
    const double x = majorRadius * std::cos(t);
    const double y = minorRadius * std::sin(t);
    const double c = std::cos(rotation);
    const double s = std::sin(rotation);
    return {center.x + x * c - y * s, center.y + x * s + y * c};
}

int BSpline::effectiveDegree() const
{
    return std::clamp(degree, 1, std::max(1, int(poles.size()) - 1));
}

std::vector<double> BSpline::knots() const
{
    const int spans = int(poles.size()) - effectiveDegree();
    std::vector<double> result;
    for (int i = 0; i <= spans; ++i) {
        result.push_back(double(i));
    }
    return result;
}

std::vector<int> BSpline::multiplicities() const
{
    const int p = effectiveDegree();
    std::vector<int> result(knots().size(), 1);
    result.front() = p + 1;
    result.back() = p + 1;
    return result;
}

Vec2 BSpline::pointAt(double u) const
{
    const int n = int(poles.size());
    const int p = effectiveDegree();
    // Vector de nudos completo (con repeticiones).
    std::vector<double> flat;
    const auto distinct = knots();
    const auto mult = multiplicities();
    for (std::size_t i = 0; i < distinct.size(); ++i) {
        flat.insert(flat.end(), std::size_t(mult[i]), distinct[i]);
    }
    const double t = std::clamp(u, 0.0, 1.0) * distinct.back();
    int k = p;
    while (k < n - 1 && t >= flat[std::size_t(k) + 1]) {
        ++k;
    }
    // De Boor en coordenadas homogéneas (x·w, y·w, w).
    struct H { double x, y, w; };
    std::vector<H> d(std::size_t(p) + 1);
    for (int j = 0; j <= p; ++j) {
        const Vec2 pole = poles[std::size_t(j + k - p)];
        const double w = weights.empty() ? 1.0 : weights[std::size_t(j + k - p)];
        d[std::size_t(j)] = {pole.x * w, pole.y * w, w};
    }
    for (int r = 1; r <= p; ++r) {
        for (int j = p; j >= r; --j) {
            const double left = flat[std::size_t(j + k - p)];
            const double right = flat[std::size_t(j + 1 + k - r)];
            const double alpha = right > left ? (t - left) / (right - left) : 0.0;
            H& a = d[std::size_t(j)];
            const H& b = d[std::size_t(j - 1)];
            a = {(1 - alpha) * b.x + alpha * a.x, (1 - alpha) * b.y + alpha * a.y, (1 - alpha) * b.w + alpha * a.w};
        }
    }
    const H& h = d[std::size_t(p)];
    return {h.x / h.w, h.y / h.w};
}

std::vector<Vec2> polyline(const Geometry& geometry)
{
    const auto sampled = [](auto&& at, double from, double to, int segments) {
        std::vector<Vec2> points;
        points.reserve(std::size_t(segments) + 1);
        for (int i = 0; i <= segments; ++i) {
            points.push_back(at(from + (to - from) * i / segments));
        }
        return points;
    };
    constexpr double kTwoPi = 2.0 * std::numbers::pi;
    // Un tramo cada 4° como máximo, al menos 8.
    const auto arcSegments = [](double sweep) { return std::max(8, int(std::ceil(sweep / (4.0 * std::numbers::pi / 180.0)))); };

    if (const auto* l = std::get_if<Line>(&geometry)) {
        return {l->start, l->end};
    }
    if (const auto* c = std::get_if<Circle>(&geometry)) {
        return sampled([&](double t) { return polar(c->center, c->radius, t); }, 0.0, kTwoPi, 90);
    }
    if (const auto* a = std::get_if<Arc>(&geometry)) {
        double end = a->endAngle;
        while (end <= a->startAngle) {
            end += kTwoPi;
        }
        return sampled([&](double t) { return polar(a->center, a->radius, t); }, a->startAngle, end,
                       arcSegments(end - a->startAngle));
    }
    if (const auto* e = std::get_if<Ellipse>(&geometry)) {
        return sampled([&](double t) { return e->pointAt(t); }, 0.0, kTwoPi, 120);
    }
    if (const auto* ea = std::get_if<EllipseArc>(&geometry)) {
        double end = ea->endParam;
        while (end <= ea->startParam) {
            end += kTwoPi;
        }
        return sampled([&](double t) { return ea->ellipse.pointAt(t); }, ea->startParam, end,
                       arcSegments(end - ea->startParam));
    }
    if (const auto* b = std::get_if<BSpline>(&geometry)) {
        const int segments = std::max(32, 16 * int(b->poles.size()));
        return sampled([&](double u) { return b->pointAt(u); }, 0.0, 1.0, segments);
    }
    return {};
}

bool isClosedCurve(const Geometry& geometry)
{
    return std::holds_alternative<Circle>(geometry) || std::holds_alternative<Ellipse>(geometry);
}

bool isDimensional(ConstraintType type) noexcept
{
    switch (type) {
    case ConstraintType::Distance:
    case ConstraintType::DistanceX:
    case ConstraintType::DistanceY:
    case ConstraintType::Angle:
    case ConstraintType::Radius:
    case ConstraintType::Diameter:
    case ConstraintType::MajorRadius:
    case ConstraintType::MinorRadius:
        return true;
    default:
        return false;
    }
}

// ---------------------------------------------------------------------------
// Fábricas de restricciones
// ---------------------------------------------------------------------------

namespace {

Constraint make(ConstraintType type, Ref a, Ref b = {}, Ref c = {}, double value = 0.0)
{
    Constraint constraint;
    constraint.type = type;
    constraint.first = a;
    constraint.second = b;
    constraint.third = c;
    constraint.value = value;
    return constraint;
}

} // namespace

Constraint Constraint::coincident(Ref a, Ref b) { return make(ConstraintType::Coincident, a, b); }
Constraint Constraint::pointOnObject(Ref point, EntityId curve)
{
    return make(ConstraintType::PointOnObject, point, Ref::of(curve));
}
Constraint Constraint::horizontal(EntityId line) { return make(ConstraintType::Horizontal, Ref::of(line)); }
Constraint Constraint::horizontal(Ref a, Ref b) { return make(ConstraintType::Horizontal, a, b); }
Constraint Constraint::vertical(EntityId line) { return make(ConstraintType::Vertical, Ref::of(line)); }
Constraint Constraint::vertical(Ref a, Ref b) { return make(ConstraintType::Vertical, a, b); }
Constraint Constraint::parallel(EntityId a, EntityId b)
{
    return make(ConstraintType::Parallel, Ref::of(a), Ref::of(b));
}
Constraint Constraint::perpendicular(EntityId a, EntityId b)
{
    return make(ConstraintType::Perpendicular, Ref::of(a), Ref::of(b));
}
Constraint Constraint::tangent(EntityId a, EntityId b)
{
    return make(ConstraintType::Tangent, Ref::of(a), Ref::of(b));
}
Constraint Constraint::tangentAt(Ref a, Ref b) { return make(ConstraintType::Tangent, a, b); }
Constraint Constraint::curvatureAt(Ref a, Ref b) { return make(ConstraintType::Curvature, a, b); }
Constraint Constraint::equal(EntityId a, EntityId b)
{
    return make(ConstraintType::Equal, Ref::of(a), Ref::of(b));
}
Constraint Constraint::collinear(EntityId a, EntityId b)
{
    return make(ConstraintType::Collinear, Ref::of(a), Ref::of(b));
}
Constraint Constraint::concentric(EntityId a, EntityId b)
{
    return make(ConstraintType::Concentric, Ref::of(a), Ref::of(b));
}
Constraint Constraint::midpoint(Ref point, EntityId line)
{
    return make(ConstraintType::Midpoint, point, Ref::of(line));
}
Constraint Constraint::symmetric(Ref a, Ref b, Ref axis)
{
    return make(ConstraintType::Symmetric, a, b, axis);
}
Constraint Constraint::fix(Ref what) { return make(ConstraintType::Fix, what); }

Constraint Constraint::distance(Ref a, Ref b, double value)
{
    return make(ConstraintType::Distance, a, b, {}, value);
}
Constraint Constraint::length(EntityId line, double value)
{
    return make(ConstraintType::Distance, Ref::of(line), {}, {}, value);
}
Constraint Constraint::distanceX(Ref a, Ref b, double value)
{
    return make(ConstraintType::DistanceX, a, b, {}, value);
}
Constraint Constraint::distanceY(Ref a, Ref b, double value)
{
    return make(ConstraintType::DistanceY, a, b, {}, value);
}
Constraint Constraint::angle(EntityId a, EntityId b, double radians)
{
    return make(ConstraintType::Angle, Ref::of(a), Ref::of(b), {}, radians);
}
Constraint Constraint::radius(EntityId curve, double value)
{
    return make(ConstraintType::Radius, Ref::of(curve), {}, {}, value);
}
Constraint Constraint::diameter(EntityId curve, double value)
{
    return make(ConstraintType::Diameter, Ref::of(curve), {}, {}, value);
}
Constraint Constraint::majorRadius(EntityId ellipse, double value)
{
    return make(ConstraintType::MajorRadius, Ref::of(ellipse), {}, {}, value);
}
Constraint Constraint::minorRadius(EntityId ellipse, double value)
{
    return make(ConstraintType::MinorRadius, Ref::of(ellipse), {}, {}, value);
}

// ---------------------------------------------------------------------------
// Sketch
// ---------------------------------------------------------------------------

Sketch::Sketch()
{
    // Los ejes son líneas por el origen; para las restricciones cuentan
    // como rectas infinitas, así que su longitud da igual.
    addEntity(Point{{0.0, 0.0}}, false, true);
    addEntity(Line{{0.0, 0.0}, {1.0, 0.0}}, true, true);
    addEntity(Line{{0.0, 0.0}, {0.0, 1.0}}, true, true);
}

EntityId Sketch::addEntity(Geometry geometry, bool construction, bool reference)
{
    Entity added;
    added.id = nextEntityId_++;
    added.geometry = geometry;
    added.construction = construction;
    added.reference = reference;
    entities_.push_back(added);
    return added.id;
}

EntityId Sketch::addPoint(Vec2 position, bool construction)
{
    return addEntity(Point{position}, construction, false);
}

EntityId Sketch::addLine(Vec2 start, Vec2 end, bool construction)
{
    return addEntity(Line{start, end}, construction, false);
}

EntityId Sketch::addCircle(Vec2 center, double radius, bool construction)
{
    if (!(radius > 0.0)) {
        invalid("El radio del círculo debe ser positivo");
    }
    return addEntity(Circle{center, radius}, construction, false);
}

EntityId Sketch::addArc(Vec2 center, double radius, double startAngle, double endAngle,
                        bool construction)
{
    if (!(radius > 0.0)) {
        invalid("El radio del arco debe ser positivo");
    }
    return addEntity(Arc{center, radius, startAngle, endAngle}, construction, false);
}

EntityId Sketch::addEllipse(const Ellipse& ellipse, bool construction)
{
    if (!(ellipse.minorRadius > 0.0) || ellipse.majorRadius < ellipse.minorRadius) {
        invalid("La elipse necesita semiejes positivos (mayor ≥ menor)");
    }
    return addEntity(ellipse, construction, false);
}

EntityId Sketch::addEllipseArc(const EllipseArc& arc, bool construction)
{
    if (!(arc.ellipse.minorRadius > 0.0) || arc.ellipse.majorRadius < arc.ellipse.minorRadius) {
        invalid("El arco de elipse necesita semiejes positivos (mayor ≥ menor)");
    }
    return addEntity(arc, construction, false);
}

EntityId Sketch::addBSpline(const BSpline& spline, bool construction)
{
    BSpline s = spline;
    if (s.poles.size() < 2) {
        invalid("La spline necesita al menos 2 polos");
    }
    if (s.weights.empty()) {
        s.weights.assign(s.poles.size(), 1.0);
    }
    if (s.weights.size() != s.poles.size()
        || std::ranges::any_of(s.weights, [](double w) { return !(w > 0.0); })) {
        invalid("La spline necesita un peso positivo por polo");
    }
    s.degree = s.effectiveDegree();
    return addEntity(s, construction, false);
}

EntityId Sketch::addGeometry(const Geometry& g, bool construction)
{
    if (const auto* p = std::get_if<Point>(&g)) return addPoint(p->position, construction);
    if (const auto* l = std::get_if<Line>(&g)) return addLine(l->start, l->end, construction);
    if (const auto* c = std::get_if<Circle>(&g)) return addCircle(c->center, c->radius, construction);
    if (const auto* a = std::get_if<Arc>(&g)) return addArc(a->center, a->radius, a->startAngle, a->endAngle, construction);
    if (const auto* e = std::get_if<Ellipse>(&g)) return addEllipse(*e, construction);
    if (const auto* ea = std::get_if<EllipseArc>(&g)) return addEllipseArc(*ea, construction);
    return addBSpline(std::get<BSpline>(g), construction);
}

void Sketch::removeEntity(EntityId id)
{
    const Entity* target = entity(id);
    if (!target) {
        return;
    }
    if (target->reference) {
        invalid("El origen y los ejes del sketch no se pueden borrar");
    }
    std::erase_if(constraints_, [id](const Constraint& c) {
        return c.first.entity == id || c.second.entity == id || c.third.entity == id;
    });
    std::erase_if(entities_, [id](const Entity& e) { return e.id == id; });
}

ConstraintId Sketch::addConstraint(Constraint constraint)
{
    validate(constraint);
    constraint.id = nextConstraintId_++;
    constraints_.push_back(constraint);
    return constraint.id;
}

bool Sketch::canAdd(const Constraint& constraint) const
{
    try {
        validate(constraint);
        return true;
    } catch (const std::invalid_argument&) {
        return false;
    }
}

void Sketch::removeConstraint(ConstraintId id)
{
    std::erase_if(constraints_, [id](const Constraint& c) { return c.id == id; });
}

const Entity* Sketch::entity(EntityId id) const
{
    auto it = std::ranges::find(entities_, id, &Entity::id);
    return it == entities_.end() ? nullptr : &*it;
}

Entity* Sketch::entity(EntityId id)
{
    return const_cast<Entity*>(std::as_const(*this).entity(id));
}

const Constraint* Sketch::constraint(ConstraintId id) const
{
    auto it = std::ranges::find(constraints_, id, &Constraint::id);
    return it == constraints_.end() ? nullptr : &*it;
}

Constraint* Sketch::constraint(ConstraintId id)
{
    return const_cast<Constraint*>(std::as_const(*this).constraint(id));
}

std::optional<Vec2> Sketch::point(Ref ref) const
{
    const Entity* e = entity(ref.entity);
    if (!e) {
        return std::nullopt;
    }
    const Geometry& g = e->geometry;
    if (const auto* p = std::get_if<Point>(&g); p && ref.part == Part::Whole) {
        return p->position;
    }
    if (const auto* l = std::get_if<Line>(&g)) {
        if (ref.part == Part::Start) return l->start;
        if (ref.part == Part::End) return l->end;
    }
    if (const auto* c = std::get_if<Circle>(&g); c && ref.part == Part::Center) {
        return c->center;
    }
    if (const auto* a = std::get_if<Arc>(&g)) {
        if (ref.part == Part::Start) return a->startPoint();
        if (ref.part == Part::End) return a->endPoint();
        if (ref.part == Part::Center) return a->center;
    }
    if (const auto* el = std::get_if<Ellipse>(&g); el && ref.part == Part::Center) {
        return el->center;
    }
    if (const auto* ea = std::get_if<EllipseArc>(&g)) {
        if (ref.part == Part::Start) return ea->startPoint();
        if (ref.part == Part::End) return ea->endPoint();
        if (ref.part == Part::Center) return ea->ellipse.center;
    }
    if (const auto* b = std::get_if<BSpline>(&g)) {
        if (ref.part == Part::Start) return b->poles.front();
        if (ref.part == Part::End) return b->poles.back();
        if (ref.part == Part::Pole && ref.index >= 0 && ref.index < int(b->poles.size())) {
            return b->poles[std::size_t(ref.index)];
        }
    }
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// Validación
// ---------------------------------------------------------------------------

void Sketch::validate(const Constraint& c) const
{
    const auto geometry = [this](Ref r) -> const Geometry* {
        if (r.part != Part::Whole) {
            return nullptr;
        }
        const Entity* e = entity(r.entity);
        return e ? &e->geometry : nullptr;
    };
    const auto isPoint = [this](Ref r) { return point(r).has_value(); };
    const auto isLine = [&](Ref r) {
        const Geometry* g = geometry(r);
        return g && std::holds_alternative<Line>(*g);
    };
    const auto isRound = [&](Ref r) {
        const Geometry* g = geometry(r);
        return g && (std::holds_alternative<Circle>(*g) || std::holds_alternative<Arc>(*g));
    };
    const auto isEllipse = [&](Ref r) {
        const Geometry* g = geometry(r);
        return g && (std::holds_alternative<Ellipse>(*g) || std::holds_alternative<EllipseArc>(*g));
    };
    const auto isCurve = [&](Ref r) {
        const Geometry* g = geometry(r);
        return g && !std::holds_alternative<Point>(*g);
    };
    const auto distinct = [](Ref a, Ref b) { return a != b; };
    const auto twoLines = [&] {
        return isLine(c.first) && isLine(c.second) && c.first.entity != c.second.entity;
    };

    bool ok = false;
    switch (c.type) {
    case ConstraintType::Coincident:
        ok = isPoint(c.first) && isPoint(c.second) && distinct(c.first, c.second);
        break;
    case ConstraintType::PointOnObject:
        ok = isPoint(c.first) && isCurve(c.second) && c.first.entity != c.second.entity;
        break;
    case ConstraintType::Horizontal:
    case ConstraintType::Vertical:
        ok = (isLine(c.first) && c.second.empty())
             || (isPoint(c.first) && isPoint(c.second) && distinct(c.first, c.second));
        break;
    case ConstraintType::Parallel:
    case ConstraintType::Perpendicular:
    case ConstraintType::Collinear:
    case ConstraintType::Angle:
        ok = twoLines();
        break;
    case ConstraintType::Tangent:
    case ConstraintType::Curvature: {
        // Extremo de una curva abierta.
        const auto isEndpoint = [&](Ref r) {
            const Entity* e = entity(r.entity);
            return e && (r.part == Part::Start || r.part == Part::End) && point(r).has_value()
                   && !std::holds_alternative<Point>(e->geometry);
        };
        const auto lineEntity = [&](Ref r) { return isLine(Ref::of(r.entity)); };
        const auto isFullEllipse = [&](Ref r) {
            const Geometry* g = geometry(r);
            return g && std::holds_alternative<Ellipse>(*g);
        };
        if (c.first.part == Part::Whole && c.type == ConstraintType::Tangent) {
            // Tangencia de curva completa: línea o arco con círculo/arco, o
            // línea con elipse (lo que resuelve PlaneGCS).
            ok = c.first.entity != c.second.entity
                 && ((isRound(c.first) && (isRound(c.second) || isLine(c.second)))
                     || (isLine(c.first) && isRound(c.second))
                     || (isLine(c.first) && isFullEllipse(c.second))
                     || (isFullEllipse(c.first) && isLine(c.second)));
        } else {
            ok = isEndpoint(c.first) && isEndpoint(c.second) && c.first.entity != c.second.entity
                 && !(lineEntity(c.first) && lineEntity(c.second));
        }
        break;
    }
    case ConstraintType::Equal:
        ok = twoLines()
             || (isRound(c.first) && isRound(c.second) && c.first.entity != c.second.entity);
        break;
    case ConstraintType::Concentric:
        ok = (isRound(c.first) || isEllipse(c.first)) && (isRound(c.second) || isEllipse(c.second))
             && c.first.entity != c.second.entity;
        break;
    case ConstraintType::Midpoint:
        ok = isPoint(c.first) && isLine(c.second) && c.first.entity != c.second.entity;
        break;
    case ConstraintType::Symmetric:
        ok = isPoint(c.first) && isPoint(c.second) && distinct(c.first, c.second)
             && (isLine(c.third) || isPoint(c.third));
        break;
    case ConstraintType::Fix:
        ok = isPoint(c.first) || geometry(c.first) != nullptr;
        break;
    case ConstraintType::Distance:
        ok = ((isPoint(c.first) && isPoint(c.second) && distinct(c.first, c.second))
              || (isPoint(c.first) && isLine(c.second) && c.first.entity != c.second.entity)
              || (isLine(c.first) && c.second.empty()))
             && c.value >= 0.0;
        break;
    case ConstraintType::DistanceX:
    case ConstraintType::DistanceY:
        ok = (isPoint(c.first) && isPoint(c.second) && distinct(c.first, c.second))
             || (isLine(c.first) && c.second.empty());
        break;
    case ConstraintType::Radius:
    case ConstraintType::Diameter:
        ok = isRound(c.first) && c.value > 0.0;
        break;
    case ConstraintType::MajorRadius:
    case ConstraintType::MinorRadius:
        ok = isEllipse(c.first) && c.value > 0.0;
        break;
    }
    if (!ok) {
        invalid("Argumentos no válidos para este tipo de restricción");
    }
}

// ---------------------------------------------------------------------------
// Medidas
// ---------------------------------------------------------------------------

double Sketch::measure(const Constraint& c) const
{
    const auto lineOf = [this](Ref r) -> Line {
        const Entity* e = entity(r.entity);
        const auto* l = e ? std::get_if<Line>(&e->geometry) : nullptr;
        if (!l) {
            invalid("Se esperaba una línea");
        }
        return *l;
    };
    const auto pointOf = [this](Ref r) {
        auto p = point(r);
        if (!p) {
            invalid("Se esperaba un punto");
        }
        return *p;
    };
    const auto radiusOf = [this](Ref r) {
        const Entity* e = entity(r.entity);
        if (e) {
            if (const auto* circle = std::get_if<Circle>(&e->geometry)) return circle->radius;
            if (const auto* arc = std::get_if<Arc>(&e->geometry)) return arc->radius;
        }
        invalid("Se esperaba un círculo o un arco");
    };
    // Extremos de una cota entre dos puntos, o de la línea si solo hay una.
    const auto endpoints = [&]() -> std::pair<Vec2, Vec2> {
        if (c.second.empty()) {
            Line l = lineOf(c.first);
            return {l.start, l.end};
        }
        return {pointOf(c.first), pointOf(c.second)};
    };

    switch (c.type) {
    case ConstraintType::Distance: {
        if (!c.second.empty() && c.second.part == Part::Whole
            && entity(c.second.entity)
            && std::holds_alternative<Line>(entity(c.second.entity)->geometry)) {
            Line l = lineOf(c.second);
            Vec2 d = l.end - l.start;
            return std::abs(cross(d, pointOf(c.first) - l.start)) / std::hypot(d.x, d.y);
        }
        auto [a, b] = endpoints();
        return std::hypot(b.x - a.x, b.y - a.y);
    }
    case ConstraintType::DistanceX: {
        auto [a, b] = endpoints();
        return b.x - a.x;
    }
    case ConstraintType::DistanceY: {
        auto [a, b] = endpoints();
        return b.y - a.y;
    }
    case ConstraintType::Angle: {
        Line l1 = lineOf(c.first);
        Line l2 = lineOf(c.second);
        Vec2 d1 = l1.end - l1.start;
        Vec2 d2 = l2.end - l2.start;
        return std::atan2(cross(d1, d2), dot(d1, d2));
    }
    case ConstraintType::Radius:
        return radiusOf(c.first);
    case ConstraintType::Diameter:
        return 2.0 * radiusOf(c.first);
    case ConstraintType::MajorRadius:
    case ConstraintType::MinorRadius: {
        const Entity* e = entity(c.first.entity);
        const Ellipse* ellipse = nullptr;
        if (e) {
            if (const auto* full = std::get_if<Ellipse>(&e->geometry)) ellipse = full;
            if (const auto* arc = std::get_if<EllipseArc>(&e->geometry)) ellipse = &arc->ellipse;
        }
        if (!ellipse) {
            invalid("Se esperaba una elipse");
        }
        return c.type == ConstraintType::MajorRadius ? ellipse->majorRadius : ellipse->minorRadius;
    }
    default:
        invalid("La restricción no es una cota");
    }
}

} // namespace nuf::sketch
