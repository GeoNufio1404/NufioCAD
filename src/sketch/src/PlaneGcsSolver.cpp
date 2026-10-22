// ISolver sobre PlaneGCS (el solver del sketcher de FreeCAD). Es el único
// archivo que incluye PlaneGCS.

#include <nuf/sketch/Solver.hpp>

#include <GCS.h>

#include <algorithm>
#include <cmath>
#include <deque>
#include <map>
#include <numbers>
#include <set>
#include <stdexcept>

namespace nuf::sketch {

namespace {

// Traduce un Sketch a un GCS::System. PlaneGCS trabaja con punteros a los
// parámetros (coordenadas, radios, ángulos), así que se guardan en un deque,
// que no los mueve al crecer.
class GcsProblem {
public:
    GcsProblem(Sketch& sketch, const std::vector<DragTarget>& drag) : sketch_(sketch)
    {
        for (const Entity& e : sketch.entities()) {
            addEntity(e);
        }
        for (const Constraint& c : sketch.constraints()) {
            if (c.type == ConstraintType::Fix) {
                for (double* p : paramsOf(c.first)) {
                    fixed_.insert(p);
                }
            }
        }
        for (const Constraint& c : sketch.constraints()) {
            if (c.driving && c.type != ConstraintType::Fix) {
                addConstraint(c);
            }
        }
        // Arrastre: restricciones temporales (etiqueta negativa) que PlaneGCS
        // resuelve con menos prioridad que las del sketch, como en FreeCAD.
        for (const DragTarget& d : drag) {
            GCS::Point target = point(d.target, 0);
            const bool curve = d.what.part == Part::Whole && geo(d.what.entity).kind != Geo::Kind::Point;
            if (curve && (geo(d.what.entity).kind == Geo::Kind::Circle || isArc(d.what))) {
                system_.addConstraintPointOnCircle(target, roundOf(d.what), GCS::DefaultTemporaryConstraint);
            } else if (!curve) {
                system_.addConstraintP2PCoincident(pointOf(d.what), target, GCS::DefaultTemporaryConstraint);
            }
        }
        for (const auto& [param, owner] : owner_) {
            if (!fixed_.contains(param)) {
                unknowns_.push_back(param);
            }
        }
    }

    SolveResult solve()
    {
        SolveResult result;
        system_.declareUnknowns(unknowns_);
        system_.initSolution(GCS::DogLeg);

        GCS::VEC_I tags;
        system_.getConflicting(tags);
        result.conflicting = toConstraintIds(tags);
        system_.getRedundant(tags);
        result.redundant = toConstraintIds(tags);
        result.degreesOfFreedom = system_.dofsNumber();

        if (!result.conflicting.empty()) {
            result.status = SolveStatus::Conflicting;
            return result;
        }

        // Con restricciones redundantes, PlaneGCS comprueba su error antes de
        // aplicar la solución y devuelve Converged aunque haya resuelto bien;
        // si fueran incompatibles se habrían detectado como conflicto.
        const auto solved = [&](GCS::SolveStatus status) {
            return status == GCS::SolveStatus::Success
                   || (status == GCS::SolveStatus::Converged && !result.redundant.empty());
        };
        // Mismo orden de intentos que FreeCAD cuando DogLeg no converge.
        GCS::SolveStatus status = system_.solve(GCS::DogLeg);
        if (!solved(status)) {
            status = system_.solve(GCS::LevenbergMarquardt);
        }
        if (!solved(status)) {
            status = system_.solve(GCS::BFGS);
        }
        if (!solved(status)) {
            result.status = SolveStatus::Failed;
            return result;
        }
        system_.applySolution();
        writeBack();

        // El diagnóstico de initSolution dice qué parámetros siguen libres:
        // son los de las entidades no restringidas del todo.
        GCS::VEC_pD free;
        system_.getDependentParams(free);
        if (free.empty() && result.degreesOfFreedom > 0) {
            // Sin ninguna restricción el diagnóstico no llega a calcularse:
            // todo está libre.
            free = unknowns_;
        }
        std::set<EntityId> loose;
        for (double* p : free) {
            if (auto it = owner_.find(p); it != owner_.end()) {
                loose.insert(it->second);
            }
        }
        result.underconstrained.assign(loose.begin(), loose.end());
        result.status = SolveStatus::Solved;
        return result;
    }

private:
    struct Geo {
        enum class Kind { Point, Line, Circle, Arc } kind;
        std::size_t index;
    };

    double* param(double value, EntityId owner)
    {
        params_.push_back(value);
        double* p = &params_.back();
        if (owner != 0) {
            owner_[p] = owner;
        }
        return p;
    }

    GCS::Point point(Vec2 v, EntityId owner)
    {
        GCS::Point p;
        p.x = param(v.x, owner);
        p.y = param(v.y, owner);
        return p;
    }

    void addEntity(const Entity& e)
    {
        const EntityId id = e.id;
        std::size_t first = params_.size();
        if (const auto* pt = std::get_if<Point>(&e.geometry)) {
            points_.push_back(point(pt->position, id));
            geos_[id] = {Geo::Kind::Point, points_.size() - 1};
        }
        else if (const auto* l = std::get_if<Line>(&e.geometry)) {
            GCS::Line line;
            line.p1 = point(l->start, id);
            line.p2 = point(l->end, id);
            lines_.push_back(line);
            geos_[id] = {Geo::Kind::Line, lines_.size() - 1};
        }
        else if (const auto* c = std::get_if<Circle>(&e.geometry)) {
            GCS::Circle circle;
            circle.center = point(c->center, id);
            circle.rad = param(c->radius, id);
            circles_.push_back(circle);
            geos_[id] = {Geo::Kind::Circle, circles_.size() - 1};
        }
        else if (const auto* a = std::get_if<Arc>(&e.geometry)) {
            // PlaneGCS espera endAngle > startAngle.
            double start = a->startAngle;
            double end = a->endAngle;
            while (end <= start) {
                end += 2.0 * std::numbers::pi;
            }
            GCS::Arc arc;
            arc.center = point(a->center, id);
            arc.rad = param(a->radius, id);
            arc.startAngle = param(start, id);
            arc.endAngle = param(end, id);
            arc.start = point(a->startPoint(), id);
            arc.end = point(a->endPoint(), id);
            arcs_.push_back(arc);
            geos_[id] = {Geo::Kind::Arc, arcs_.size() - 1};
            // Une los extremos al centro, radio y ángulos.
            system_.addConstraintArcRules(arcs_.back());
        }
        if (e.reference) {
            for (std::size_t i = first; i < params_.size(); ++i) {
                fixed_.insert(&params_[i]);
            }
        }
    }

    const Geo& geo(EntityId id) const
    {
        auto it = geos_.find(id);
        if (it == geos_.end()) {
            throw std::logic_error("Restricción con una entidad que no existe");
        }
        return it->second;
    }

    GCS::Point& pointOf(Ref r)
    {
        const Geo& g = geo(r.entity);
        switch (g.kind) {
        case Geo::Kind::Point:
            return points_[g.index];
        case Geo::Kind::Line:
            return r.part == Part::Start ? lines_[g.index].p1 : lines_[g.index].p2;
        case Geo::Kind::Circle:
            return circles_[g.index].center;
        case Geo::Kind::Arc:
            if (r.part == Part::Start) return arcs_[g.index].start;
            if (r.part == Part::End) return arcs_[g.index].end;
            return arcs_[g.index].center;
        }
        throw std::logic_error("Tipo de entidad desconocido");
    }

    GCS::Line& lineOf(Ref r) { return lines_[geo(r.entity).index]; }

    bool isLine(Ref r) const { return geo(r.entity).kind == Geo::Kind::Line; }
    bool isArc(Ref r) const { return geo(r.entity).kind == Geo::Kind::Arc; }

    // Círculo y arco comparten centro y radio (GCS::Arc deriva de GCS::Circle).
    GCS::Circle& roundOf(Ref r)
    {
        const Geo& g = geo(r.entity);
        if (g.kind == Geo::Kind::Arc) {
            return arcs_[g.index];
        }
        return circles_[g.index];
    }

    // Parámetros que fija una restricción Fix.
    std::vector<double*> paramsOf(Ref r)
    {
        if (r.part != Part::Whole || geo(r.entity).kind == Geo::Kind::Point) {
            GCS::Point& p = pointOf(r);
            return {p.x, p.y};
        }
        std::vector<double*> result;
        for (const auto& [p, owner] : owner_) {
            if (owner == r.entity) {
                result.push_back(p);
            }
        }
        return result;
    }

    // Lado de la recta en que está el punto: decide hacia dónde queda la
    // distancia o la tangencia (igual que FreeCAD, según la geometría actual).
    static bool counterClockwise(const GCS::Line& l, const GCS::Point& p)
    {
        double dx = *l.p2.x - *l.p1.x;
        double dy = *l.p2.y - *l.p1.y;
        return dx * (*p.y - *l.p1.y) - dy * (*p.x - *l.p1.x) > 0.0;
    }

    void addConstraint(const Constraint& c)
    {
        const int tag = static_cast<int>(c.id);
        auto& s = system_;
        switch (c.type) {
        case ConstraintType::Coincident:
            s.addConstraintP2PCoincident(pointOf(c.first), pointOf(c.second), tag);
            break;
        case ConstraintType::PointOnObject: {
            GCS::Point& p = pointOf(c.first);
            if (isLine(c.second)) {
                s.addConstraintPointOnLine(p, lineOf(c.second), tag);
            }
            else if (isArc(c.second)) {
                s.addConstraintPointOnArc(p, arcs_[geo(c.second.entity).index], tag);
            }
            else {
                s.addConstraintPointOnCircle(p, roundOf(c.second), tag);
            }
            break;
        }
        case ConstraintType::Horizontal:
            if (c.second.empty()) {
                s.addConstraintHorizontal(lineOf(c.first), tag);
            }
            else {
                s.addConstraintHorizontal(pointOf(c.first), pointOf(c.second), tag);
            }
            break;
        case ConstraintType::Vertical:
            if (c.second.empty()) {
                s.addConstraintVertical(lineOf(c.first), tag);
            }
            else {
                s.addConstraintVertical(pointOf(c.first), pointOf(c.second), tag);
            }
            break;
        case ConstraintType::Parallel:
            s.addConstraintParallel(lineOf(c.first), lineOf(c.second), tag);
            break;
        case ConstraintType::Perpendicular:
            s.addConstraintPerpendicular(lineOf(c.first), lineOf(c.second), tag);
            break;
        case ConstraintType::Tangent: {
            Ref line = isLine(c.first) ? c.first : (isLine(c.second) ? c.second : Ref{});
            if (!line.empty()) {
                Ref other = line.entity == c.first.entity ? c.second : c.first;
                GCS::Line& l = lineOf(line);
                GCS::Circle& round = roundOf(other);
                s.addConstraintTangent(l, round, counterClockwise(l, round.center), tag);
            }
            else {
                s.addConstraintTangent(roundOf(c.first), roundOf(c.second), tag);
            }
            break;
        }
        case ConstraintType::Equal:
            if (isLine(c.first)) {
                s.addConstraintEqualLength(lineOf(c.first), lineOf(c.second), tag);
            }
            else {
                s.addConstraintEqualRadius(roundOf(c.first), roundOf(c.second), tag);
            }
            break;
        case ConstraintType::Collinear: {
            GCS::Line& base = lineOf(c.first);
            GCS::Line& other = lineOf(c.second);
            s.addConstraintPointOnLine(other.p1, base, tag);
            s.addConstraintPointOnLine(other.p2, base, tag);
            break;
        }
        case ConstraintType::Concentric:
            s.addConstraintP2PCoincident(roundOf(c.first).center, roundOf(c.second).center, tag);
            break;
        case ConstraintType::Midpoint: {
            GCS::Line& l = lineOf(c.second);
            s.addConstraintP2PSymmetric(l.p1, l.p2, pointOf(c.first), tag);
            break;
        }
        case ConstraintType::Symmetric:
            if (c.third.part == Part::Whole && isLine(c.third)) {
                s.addConstraintP2PSymmetric(pointOf(c.first), pointOf(c.second), lineOf(c.third), tag);
            }
            else {
                s.addConstraintP2PSymmetric(pointOf(c.first), pointOf(c.second), pointOf(c.third), tag);
            }
            break;
        case ConstraintType::Fix:
            break;  // se resuelve dejando sus parámetros fuera de las incógnitas
        case ConstraintType::Distance: {
            double* value = param(c.value, 0);
            if (c.second.empty()) {
                GCS::Line& l = lineOf(c.first);
                s.addConstraintP2PDistance(l.p1, l.p2, value, tag);
            }
            else if (c.second.part == Part::Whole && isLine(c.second)) {
                GCS::Point& p = pointOf(c.first);
                GCS::Line& l = lineOf(c.second);
                if (c.value == 0.0) {
                    s.addConstraintPointOnLine(p, l, tag);
                }
                else {
                    s.addConstraintP2LDistance(p, l, value, counterClockwise(l, p), tag);
                }
            }
            else {
                s.addConstraintP2PDistance(pointOf(c.first), pointOf(c.second), value, tag);
            }
            break;
        }
        case ConstraintType::DistanceX:
        case ConstraintType::DistanceY: {
            GCS::Point a = c.second.empty() ? lineOf(c.first).p1 : pointOf(c.first);
            GCS::Point b = c.second.empty() ? lineOf(c.first).p2 : pointOf(c.second);
            const bool x = c.type == ConstraintType::DistanceX;
            s.addConstraintDifference(x ? a.x : a.y, x ? b.x : b.y, param(c.value, 0), tag);
            break;
        }
        case ConstraintType::Angle:
            s.addConstraintL2LAngle(lineOf(c.first), lineOf(c.second), param(c.value, 0), tag);
            break;
        case ConstraintType::Radius:
            if (isArc(c.first)) {
                s.addConstraintArcRadius(arcs_[geo(c.first.entity).index], param(c.value, 0), tag);
            }
            else {
                s.addConstraintCircleRadius(roundOf(c.first), param(c.value, 0), tag);
            }
            break;
        case ConstraintType::Diameter:
            if (isArc(c.first)) {
                s.addConstraintArcDiameter(arcs_[geo(c.first.entity).index], param(c.value, 0), tag);
            }
            else {
                s.addConstraintCircleDiameter(roundOf(c.first), param(c.value, 0), tag);
            }
            break;
        }
    }

    void writeBack()
    {
        const auto vec = [](const GCS::Point& p) { return Vec2{*p.x, *p.y}; };
        for (const auto& [id, g] : geos_) {
            Entity* e = sketch_.entity(id);
            if (!e || e->reference) {
                continue;
            }
            switch (g.kind) {
            case Geo::Kind::Point:
                e->geometry = Point{vec(points_[g.index])};
                break;
            case Geo::Kind::Line:
                e->geometry = Line{vec(lines_[g.index].p1), vec(lines_[g.index].p2)};
                break;
            case Geo::Kind::Circle: {
                const GCS::Circle& c = circles_[g.index];
                e->geometry = Circle{vec(c.center), std::abs(*c.rad)};
                break;
            }
            case Geo::Kind::Arc: {
                const GCS::Arc& a = arcs_[g.index];
                e->geometry = Arc{vec(a.center), std::abs(*a.rad), *a.startAngle, *a.endAngle};
                break;
            }
            }
        }
        // Las cotas de referencia muestran la medida de la geometría nueva.
        for (const Constraint& c : sketch_.constraints()) {
            if (!c.driving && isDimensional(c.type)) {
                sketch_.constraint(c.id)->value = sketch_.measure(c);
            }
        }
    }

    static std::vector<ConstraintId> toConstraintIds(const GCS::VEC_I& tags)
    {
        std::set<ConstraintId> ids;
        for (int tag : tags) {
            if (tag > 0) {
                ids.insert(static_cast<ConstraintId>(tag));
            }
        }
        return {ids.begin(), ids.end()};
    }

    Sketch& sketch_;
    std::deque<double> params_;
    std::map<double*, EntityId> owner_;
    std::set<double*> fixed_;
    GCS::VEC_pD unknowns_;
    std::deque<GCS::Point> points_;
    std::deque<GCS::Line> lines_;
    std::deque<GCS::Circle> circles_;
    std::deque<GCS::Arc> arcs_;
    std::map<EntityId, Geo> geos_;
    // Último: sus restricciones apuntan a todo lo anterior y se destruyen antes.
    GCS::System system_;
};

class PlaneGcsSolver final : public ISolver {
public:
    SolveResult solve(Sketch& sketch) override { return GcsProblem(sketch, {}).solve(); }
    SolveResult drag(Sketch& sketch, const std::vector<DragTarget>& targets) override
    {
        return GcsProblem(sketch, targets).solve();
    }
};

} // namespace

std::unique_ptr<ISolver> makePlaneGcsSolver()
{
    return std::make_unique<PlaneGcsSolver>();
}

} // namespace nuf::sketch
