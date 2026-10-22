// ISolver sobre PlaneGCS (el solver del sketcher de FreeCAD). Es el único
// archivo que incluye PlaneGCS.

#include <nuf/sketch/Solver.hpp>

#include <GCS.h>

#include <algorithm>
#include <cmath>
#include <deque>
#include <limits>
#include <map>
#include <numbers>
#include <set>
#include <stdexcept>

namespace nuf::sketch {

namespace {

// --- Continuidad de curvatura (G2) -------------------------------------------
//
// PlaneGCS no la trae: es una restricción propia. Para cada curva se calcula
// su curvatura con signo al salir del punto de unión (positiva si gira a la
// izquierda alejándose de él); en una unión G2 las dos suman cero.

struct EndCurvature {
    enum class Kind { Line, Arc, EllipseArc, BSpline } kind = Kind::Line;
    bool fromStart = true;
    std::size_t offset = 0;  // primer parámetro propio dentro de pvec
    // Spline: factor constante de la fórmula (grado, pesos y nudos).
    double splineFactor = 0.0;
};

class CurvatureContinuity final : public GCS::Constraint {
public:
    CurvatureContinuity(std::vector<double*> params, EndCurvature a, EndCurvature b)
        : a_(a)
        , b_(b)
    {
        pvec = std::move(params);
        origpvec = pvec;
    }

    void rescale(double coef) override { scale = coef; }
    double error() override { return scale * (curvature(a_) + curvature(b_)); }

    // Derivada numérica (diferencia centrada): la fórmula exacta no aporta
    // nada a la convergencia para una sola restricción así.
    double grad(double* param) override
    {
        double derivative = 0.0;
        for (double* p : pvec) {
            if (p != param) {
                continue;
            }
            const double original = *p;
            const double h = 1e-7 * std::max(1.0, std::abs(original));
            *p = original + h;
            const double plus = curvature(a_) + curvature(b_);
            *p = original - h;
            const double minus = curvature(a_) + curvature(b_);
            *p = original;
            derivative = scale * (plus - minus) / (2.0 * h);
            break;  // el mismo puntero repetido ya cuenta en ambas evaluaciones
        }
        return derivative;
    }

private:
    double at(std::size_t i) const { return *pvec[i]; }

    double curvature(const EndCurvature& e) const
    {
        switch (e.kind) {
        case EndCurvature::Kind::Line:
            return 0.0;
        case EndCurvature::Kind::Arc: {
            const double r = std::abs(at(e.offset));
            return r > 0.0 ? (e.fromStart ? 1.0 : -1.0) / r : 0.0;
        }
        case EndCurvature::Kind::EllipseArc: {
            // [cx, cy, fx, fy, b, t]
            const double fx = at(e.offset + 2) - at(e.offset);
            const double fy = at(e.offset + 3) - at(e.offset + 1);
            const double b = std::abs(at(e.offset + 4));
            const double a = std::sqrt(fx * fx + fy * fy + b * b);
            const double t = at(e.offset + 5);
            const double s = std::sin(t);
            const double c = std::cos(t);
            const double k = a * b / std::pow(a * a * s * s + b * b * c * c, 1.5);
            return e.fromStart ? k : -k;
        }
        case EndCurvature::Kind::BSpline: {
            // [P0x, P0y, P1x, P1y, P2x, P2y] desde el extremo hacia dentro.
            const double d1x = at(e.offset + 2) - at(e.offset);
            const double d1y = at(e.offset + 3) - at(e.offset + 1);
            const double d2x = at(e.offset + 4) - at(e.offset + 2);
            const double d2y = at(e.offset + 5) - at(e.offset + 3);
            const double len = std::hypot(d1x, d1y);
            if (len < 1e-12) {
                return 0.0;
            }
            return e.splineFactor * (d1x * d2y - d1y * d2x) / (len * len * len);
        }
        }
        return 0.0;
    }

    EndCurvature a_;
    EndCurvature b_;
};

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
        unknowns_.insert(unknowns_.end(), auxiliary_.begin(), auxiliary_.end());
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
        enum class Kind { Point, Line, Circle, Arc, Ellipse, EllipseArc, BSpline } kind;
        std::size_t index;
    };

    // Parámetro auxiliar (incógnita sin entidad dueña): posición de un punto
    // sobre una spline, extremos del eje de una elipse acotada…
    double* auxiliary(double value)
    {
        double* p = param(value, 0);
        auxiliary_.push_back(p);
        return p;
    }

    // Foco de la elipse (PlaneGCS la describe por centro, foco y semieje menor).
    static Vec2 focusOf(const Ellipse& e)
    {
        const double c = std::sqrt(std::max(0.0, e.majorRadius * e.majorRadius - e.minorRadius * e.minorRadius));
        return {e.center.x + c * std::cos(e.rotation), e.center.y + c * std::sin(e.rotation)};
    }

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
        else if (const auto* el = std::get_if<Ellipse>(&e.geometry)) {
            GCS::Ellipse ellipse;
            ellipse.center = point(el->center, id);
            ellipse.focus1 = point(focusOf(*el), id);
            ellipse.radmin = param(el->minorRadius, id);
            ellipses_.push_back(ellipse);
            geos_[id] = {Geo::Kind::Ellipse, ellipses_.size() - 1};
        }
        else if (const auto* ea = std::get_if<EllipseArc>(&e.geometry)) {
            double start = ea->startParam;
            double end = ea->endParam;
            while (end <= start) {
                end += 2.0 * std::numbers::pi;
            }
            GCS::ArcOfEllipse arc;
            arc.center = point(ea->ellipse.center, id);
            arc.focus1 = point(focusOf(ea->ellipse), id);
            arc.radmin = param(ea->ellipse.minorRadius, id);
            arc.startAngle = param(start, id);
            arc.endAngle = param(end, id);
            arc.start = point(ea->startPoint(), id);
            arc.end = point(ea->endPoint(), id);
            ellipseArcs_.push_back(arc);
            geos_[id] = {Geo::Kind::EllipseArc, ellipseArcs_.size() - 1};
            system_.addConstraintArcOfEllipseRules(ellipseArcs_.back());
        }
        else if (const auto* b = std::get_if<BSpline>(&e.geometry)) {
            GCS::BSpline spline;
            for (const Vec2& pole : b->poles) {
                spline.poles.push_back(point(pole, id));
            }
            // Pesos y nudos no son incógnitas (sin dueño).
            for (std::size_t i = 0; i < b->poles.size(); ++i) {
                spline.weights.push_back(param(b->weights.empty() ? 1.0 : b->weights[i], 0));
            }
            for (double k : b->knots()) {
                spline.knots.push_back(param(k, 0));
            }
            spline.mult = b->multiplicities();
            spline.degree = b->effectiveDegree();
            spline.periodic = false;
            spline.start = point(b->poles.front(), id);
            spline.end = point(b->poles.back(), id);
            spline.knotpointGeoids.assign(spline.knots.size(), 0);
            bsplines_.push_back(spline);
            bsplines_.back().setupFlattenedKnots();
            geos_[id] = {Geo::Kind::BSpline, bsplines_.size() - 1};
            // Sujeta: los extremos son el primer y el último polo.
            system_.addConstraintP2PCoincident(bsplines_.back().poles.front(), bsplines_.back().start);
            system_.addConstraintP2PCoincident(bsplines_.back().poles.back(), bsplines_.back().end);
        }
        if (e.reference || e.locked) {
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
        case Geo::Kind::Ellipse:
            return ellipses_[g.index].center;
        case Geo::Kind::EllipseArc:
            if (r.part == Part::Start) return ellipseArcs_[g.index].start;
            if (r.part == Part::End) return ellipseArcs_[g.index].end;
            return ellipseArcs_[g.index].center;
        case Geo::Kind::BSpline:
            if (r.part == Part::Start) return bsplines_[g.index].start;
            if (r.part == Part::End) return bsplines_[g.index].end;
            return bsplines_[g.index].poles.at(std::size_t(r.index));
        }
        throw std::logic_error("Tipo de entidad desconocido");
    }

    GCS::Ellipse& ellipseOf(Ref r)
    {
        const Geo& g = geo(r.entity);
        if (g.kind == Geo::Kind::EllipseArc) {
            return ellipseArcs_[g.index];
        }
        return ellipses_[g.index];
    }

    // Parámetro de la spline más cercano a p (para empezar «punto sobre spline»).
    double closestSplineParam(EntityId id, Vec2 p) const
    {
        const auto& b = std::get<BSpline>(sketch_.entity(id)->geometry);
        double best = 0.0;
        double bestDistance = std::numeric_limits<double>::infinity();
        constexpr int kSamples = 400;
        for (int i = 0; i <= kSamples; ++i) {
            const double u = double(i) / kSamples;
            const Vec2 q = b.pointAt(u);
            const double d = std::hypot(q.x - p.x, q.y - p.y);
            if (d < bestDistance) {
                bestDistance = d;
                best = u;
            }
        }
        return best * b.knots().back();
    }

    // Curvatura al salir de un extremo: tipo y parámetros para CurvatureContinuity.
    EndCurvature endCurvature(Ref r, std::vector<double*>& params)
    {
        EndCurvature e;
        e.fromStart = r.part == Part::Start;
        e.offset = params.size();
        const Geo& g = geo(r.entity);
        switch (g.kind) {
        case Geo::Kind::Arc:
            e.kind = EndCurvature::Kind::Arc;
            params.push_back(arcs_[g.index].rad);
            break;
        case Geo::Kind::EllipseArc: {
            e.kind = EndCurvature::Kind::EllipseArc;
            GCS::ArcOfEllipse& a = ellipseArcs_[g.index];
            params.insert(params.end(), {a.center.x, a.center.y, a.focus1.x, a.focus1.y, a.radmin,
                                         e.fromStart ? a.startAngle : a.endAngle});
            break;
        }
        case Geo::Kind::BSpline: {
            e.kind = EndCurvature::Kind::BSpline;
            const GCS::BSpline& s = bsplines_[g.index];
            const int n = int(s.poles.size());
            const int p = s.degree;
            if (n < 3 || p < 2) {
                e.kind = EndCurvature::Kind::Line;  // recta o de grado 1: sin curvatura
                break;
            }
            const auto idx = [&](int k) { return std::size_t(e.fromStart ? k : n - 1 - k); };
            for (int k = 0; k < 3; ++k) {
                params.push_back(s.poles[idx(k)].x);
                params.push_back(s.poles[idx(k)].y);
            }
            // κ = (p−1)/p · (w0·w2 / w1²) · (u1/u2) · (Δ1 × Δ2) / |Δ1|³, con nudos
            // uniformes: u1/u2 = 1 si es un solo tramo (Bézier) y 1/2 si no.
            const double w0 = *s.weights[idx(0)];
            const double w1 = *s.weights[idx(1)];
            const double w2 = *s.weights[idx(2)];
            const double knotRatio = n == p + 1 ? 1.0 : 0.5;
            e.splineFactor = double(p - 1) / p * (w0 * w2 / (w1 * w1)) * knotRatio;
            break;
        }
        default:
            e.kind = EndCurvature::Kind::Line;
            break;
        }
        return e;
    }

    GCS::Line& lineOf(Ref r) { return lines_[geo(r.entity).index]; }

    GCS::Curve& curveOf(Ref r)
    {
        const Geo& g = geo(r.entity);
        switch (g.kind) {
        case Geo::Kind::Line: return lines_[g.index];
        case Geo::Kind::Arc: return arcs_[g.index];
        case Geo::Kind::Circle: return circles_[g.index];
        case Geo::Kind::Ellipse: return ellipses_[g.index];
        case Geo::Kind::EllipseArc: return ellipseArcs_[g.index];
        case Geo::Kind::BSpline: return bsplines_[g.index];
        case Geo::Kind::Point: break;
        }
        throw std::logic_error("La entidad no es una curva");
    }

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

    // Tangencia en el punto de unión, como FreeCAD: puntos unidos y ángulo
    // entre las curvas en ese punto de 0 o π (el más cercano al actual).
    void addEndpointTangency(const Constraint& c, int tag)
    {
        // PlaneGCS solo sabe la tangente de una spline en sus extremos y los
        // reconoce comparando coordenadas: el punto debe ser el de la spline.
        const bool secondIsSpline = geo(c.second.entity).kind == Geo::Kind::BSpline;
        GCS::Point& p = pointOf(secondIsSpline ? c.second : c.first);
        GCS::Curve& a = curveOf(c.first);
        GCS::Curve& b = curveOf(c.second);
        const double err = std::remainder(system_.calculateAngleViaPoint(a, b, p), 2.0 * std::numbers::pi);
        const double angle = std::abs(err) > std::numbers::pi / 2.0 ? std::numbers::pi : 0.0;
        system_.addConstraintP2PCoincident(pointOf(c.first), pointOf(c.second), tag);
        system_.addConstraintAngleViaPoint(a, b, p, param(angle, 0), tag);
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
            const Geo::Kind kind = geo(c.second.entity).kind;
            if (kind == Geo::Kind::Line) {
                s.addConstraintPointOnLine(p, lineOf(c.second), tag);
            }
            else if (kind == Geo::Kind::Arc) {
                s.addConstraintPointOnArc(p, arcs_[geo(c.second.entity).index], tag);
            }
            else if (kind == Geo::Kind::Ellipse || kind == Geo::Kind::EllipseArc) {
                s.addConstraintPointOnEllipse(p, ellipseOf(c.second), tag);
            }
            else if (kind == Geo::Kind::BSpline) {
                double* u = auxiliary(closestSplineParam(c.second.entity, {*p.x, *p.y}));
                s.addConstraintPointOnBSpline(p, bsplines_[geo(c.second.entity).index], u, tag);
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
            if (c.first.part != Part::Whole) {
                addEndpointTangency(c, tag);
                break;
            }
            Ref line = isLine(c.first) ? c.first : (isLine(c.second) ? c.second : Ref{});
            if (!line.empty()) {
                Ref other = line.entity == c.first.entity ? c.second : c.first;
                GCS::Line& l = lineOf(line);
                if (geo(other.entity).kind == Geo::Kind::Ellipse) {
                    s.addConstraintTangent(l, ellipses_[geo(other.entity).index], tag);
                    break;
                }
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
            s.addConstraintP2PCoincident(pointOf(Ref::center(c.first.entity)), pointOf(Ref::center(c.second.entity)), tag);
            break;
        case ConstraintType::Curvature: {
            // G2 = tangentes en la unión (como Tangent en un extremo) + misma curvatura.
            addEndpointTangency(c, tag);
            std::vector<double*> params;
            const EndCurvature ka = endCurvature(c.first, params);
            const EndCurvature kb = endCurvature(c.second, params);
            if (!params.empty()) {
                auto* g2 = new CurvatureContinuity(std::move(params), ka, kb);
                g2->setTag(tag);
                s.addConstraint(g2);
            }
            break;
        }
        case ConstraintType::MajorRadius: {
            // Dos puntos auxiliares en los extremos del eje mayor, a 2·a.
            GCS::Ellipse& e = ellipseOf(c.first);
            const double m = sketch_.measure(c);
            const Vec2 center{*e.center.x, *e.center.y};
            const Vec2 dir{*e.focus1.x - center.x, *e.focus1.y - center.y};
            const double len = std::hypot(dir.x, dir.y);
            const Vec2 u = len > 1e-12 ? Vec2{dir.x / len, dir.y / len} : Vec2{1.0, 0.0};
            GCS::Point p1{auxiliary(center.x + m * u.x), auxiliary(center.y + m * u.y)};
            GCS::Point p2{auxiliary(center.x - m * u.x), auxiliary(center.y - m * u.y)};
            auxPoints_.push_back(p1);
            auxPoints_.push_back(p2);
            s.addConstraintInternalAlignmentEllipseMajorDiameter(e, auxPoints_[auxPoints_.size() - 2],
                                                                 auxPoints_.back(), tag);
            s.addConstraintP2PDistance(auxPoints_[auxPoints_.size() - 2], auxPoints_.back(),
                                       param(2.0 * c.value, 0), tag);
            break;
        }
        case ConstraintType::MinorRadius:
            s.addConstraintDifference(param(0.0, 0), ellipseOf(c.first).radmin, param(c.value, 0), tag);
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
            case Geo::Kind::Ellipse: {
                const Ellipse previous = std::get<Ellipse>(e->geometry);
                e->geometry = ellipseFrom(ellipses_[g.index], previous.rotation);
                break;
            }
            case Geo::Kind::EllipseArc: {
                const GCS::ArcOfEllipse& a = ellipseArcs_[g.index];
                const EllipseArc previous = std::get<EllipseArc>(e->geometry);
                e->geometry = EllipseArc{ellipseFrom(a, previous.ellipse.rotation), *a.startAngle, *a.endAngle};
                break;
            }
            case Geo::Kind::BSpline: {
                auto spline = std::get<BSpline>(e->geometry);
                const GCS::BSpline& s = bsplines_[g.index];
                for (std::size_t i = 0; i < spline.poles.size(); ++i) {
                    spline.poles[i] = vec(s.poles[i]);
                }
                e->geometry = spline;
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

    static Ellipse ellipseFrom(const GCS::Ellipse& e, double previousRotation)
    {
        const double fx = *e.focus1.x - *e.center.x;
        const double fy = *e.focus1.y - *e.center.y;
        const double b = std::abs(*e.radmin);
        const double c = std::hypot(fx, fy);
        // Con el foco en el centro (círculo) el giro no está definido: se conserva.
        const double rotation = c > 1e-12 ? std::atan2(fy, fx) : previousRotation;
        return Ellipse{{*e.center.x, *e.center.y}, std::sqrt(c * c + b * b), b, rotation};
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
    std::deque<GCS::Ellipse> ellipses_;
    std::deque<GCS::ArcOfEllipse> ellipseArcs_;
    std::deque<GCS::BSpline> bsplines_;
    std::deque<GCS::Point> auxPoints_;
    GCS::VEC_pD auxiliary_;
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
