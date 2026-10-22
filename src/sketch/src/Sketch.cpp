#include <nuf/sketch/Sketch.hpp>

#include <algorithm>
#include <cmath>
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

bool isDimensional(ConstraintType type) noexcept
{
    switch (type) {
    case ConstraintType::Distance:
    case ConstraintType::DistanceX:
    case ConstraintType::DistanceY:
    case ConstraintType::Angle:
    case ConstraintType::Radius:
    case ConstraintType::Diameter:
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
    const auto isCurve = [&](Ref r) { return isLine(r) || isRound(r); };
    const auto distinct = [](Ref a, Ref b) { return a.entity != b.entity || a.part != b.part; };
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
        ok = isCurve(c.first) && isCurve(c.second) && c.first.entity != c.second.entity
             && !(isLine(c.first) && isLine(c.second));
        break;
    case ConstraintType::Equal:
        ok = twoLines()
             || (isRound(c.first) && isRound(c.second) && c.first.entity != c.second.entity);
        break;
    case ConstraintType::Concentric:
        ok = isRound(c.first) && isRound(c.second) && c.first.entity != c.second.entity;
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
    default:
        invalid("La restricción no es una cota");
    }
}

} // namespace nuf::sketch
