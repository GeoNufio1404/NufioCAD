// Herramientas de curvas: elipse, arco de elipse, splines, cónica y ranura
// en arco.

#include <nuf/sketch/Tools.hpp>

#include "ToolSupport.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace nuf::sketch {

namespace {

using namespace detail;

constexpr double kPi = std::numbers::pi;
constexpr double kTwoPi = 2.0 * kPi;

double ccwSweep(double from, double to)
{
    const double delta = std::fmod(to - from, kTwoPi);
    return delta < 0.0 ? delta + kTwoPi : delta;
}

// --- Elipse y arco de elipse -----------------------------------------------------

class EllipseTool final : public SketchTool {
public:
    explicit EllipseTool(bool arc) : arc_(arc) {}

    void move(const Snap& cursor) override { cursor_ = cursor; }

    bool click(Sketch& sketch, const Snap& cursor) override
    {
        if (!center_) {
            center_ = cursor;
            return false;
        }
        if (!major_) {
            if (distance(center_->point, cursor.point) >= kMinLength) {
                major_ = cursor;
            }
            return false;
        }
        if (!ellipse_) {
            const auto e = ellipseThrough(cursor.point, std::nullopt);
            if (!e) {
                return false;
            }
            if (!arc_) {
                return create(sketch, *e);
            }
            ellipse_ = e;
            return false;
        }
        if (!startParam_) {
            startParam_ = paramAt(cursor.point);
            return false;
        }
        const double start = *startParam_;
        const double end = start + ccwSweep(start, paramAt(cursor.point));
        if (end - start < 1e-6) {
            return false;
        }
        const EntityId id = sketch.addEllipseArc({*ellipse_, start, end});
        attach(sketch, Ref::center(id), *center_);
        cancel();
        return true;
    }

    std::vector<std::string> valueFields() const override
    {
        if (!center_ || ellipse_) {
            return {};
        }
        return major_ ? std::vector<std::string>{"Semieje menor"} : std::vector<std::string>{"Semieje mayor"};
    }

    std::vector<double> currentValues() const override
    {
        if (!center_ || ellipse_) {
            return {};
        }
        if (!major_) {
            return {distance(center_->point, cursor_.point)};
        }
        const auto e = ellipseThrough(cursor_.point, std::nullopt);
        return {e ? e->minorRadius : 0.0};
    }

    bool applyValues(Sketch& sketch, const std::vector<std::optional<double>>& values) override
    {
        if (!center_ || ellipse_ || values.empty() || !values[0] || !(*values[0] > kMinLength)) {
            return false;
        }
        if (!major_) {
            // Semieje mayor escrito, en la dirección del cursor.
            const Vec2 c = center_->point;
            const double len = distance(c, cursor_.point);
            const Vec2 u = len > kMinLength ? Vec2{(cursor_.point.x - c.x) / len, (cursor_.point.y - c.y) / len}
                                            : Vec2{1.0, 0.0};
            major_ = Snap{{c.x + *values[0] * u.x, c.y + *values[0] * u.y}};
            majorTyped_ = *values[0];
            return false;
        }
        const auto e = ellipseThrough(cursor_.point, *values[0]);
        if (!e) {
            return false;
        }
        minorTyped_ = *values[0];
        if (!arc_) {
            return create(sketch, *e);
        }
        ellipse_ = e;
        return false;
    }

    bool cancel() override
    {
        const bool active = center_.has_value();
        center_.reset();
        major_.reset();
        ellipse_.reset();
        startParam_.reset();
        majorTyped_.reset();
        minorTyped_.reset();
        return active;
    }

    Preview preview() const override
    {
        if (!center_) {
            return {};
        }
        if (!major_) {
            return {{Line{center_->point, cursor_.point}}};
        }
        if (!ellipse_) {
            if (const auto e = ellipseThrough(cursor_.point, std::nullopt)) {
                return {{*e}};
            }
            return {{Line{center_->point, major_->point}}};
        }
        if (!startParam_) {
            return {{*ellipse_, Line{ellipse_->center, cursor_.point}}};
        }
        const double start = *startParam_;
        const double end = start + ccwSweep(start, paramAt(cursor_.point));
        return {{EllipseArc{*ellipse_, start, end}}};
    }

    std::string hint() const override
    {
        if (!center_) {
            return arc_ ? "Clic para el centro de la elipse del arco" : "Clic para el centro de la elipse";
        }
        if (!major_) {
            return "Clic para el extremo del eje mayor (o escribe el semieje)";
        }
        if (!ellipse_) {
            return "Clic para el ancho del eje menor (o escribe el semieje)";
        }
        return startParam_ ? "Clic para el final del arco (sentido antihorario)" : "Clic para el inicio del arco";
    }

private:
    // Elipse con el eje mayor hacia major_ y el semieje menor dado (o la
    // distancia de p al eje mayor). Si el menor sale mayor, se intercambian.
    std::optional<Ellipse> ellipseThrough(Vec2 p, std::optional<double> minor) const
    {
        const Vec2 c = center_->point;
        const double a = distance(c, major_->point);
        const Vec2 u{(major_->point.x - c.x) / a, (major_->point.y - c.y) / a};
        const double b = minor.value_or(std::abs((p.x - c.x) * -u.y + (p.y - c.y) * u.x));
        if (b < kMinLength) {
            return std::nullopt;
        }
        const double rotation = std::atan2(u.y, u.x);
        if (b > a) {
            return Ellipse{c, b, a, rotation + kPi / 2.0};
        }
        return Ellipse{c, a, b, rotation};
    }

    // Ángulo paramétrico del punto de la elipse en la dirección de p.
    double paramAt(Vec2 p) const
    {
        const Ellipse& e = *ellipse_;
        const double dx = p.x - e.center.x;
        const double dy = p.y - e.center.y;
        const double c = std::cos(e.rotation);
        const double s = std::sin(e.rotation);
        const double x = dx * c + dy * s;
        const double y = -dx * s + dy * c;
        return std::atan2(y / e.minorRadius, x / e.majorRadius);
    }

    bool create(Sketch& sketch, const Ellipse& e)
    {
        const EntityId id = sketch.addEllipse(e);
        attach(sketch, Ref::center(id), *center_);
        if (majorTyped_) {
            sketch.addConstraint(Constraint::majorRadius(id, std::max(*majorTyped_, minorTyped_.value_or(0.0))));
        }
        if (minorTyped_) {
            sketch.addConstraint(Constraint::minorRadius(id, std::min(*minorTyped_, majorTyped_.value_or(*minorTyped_))));
        }
        cancel();
        return true;
    }

    bool arc_;
    Snap cursor_;
    std::optional<Snap> center_;
    std::optional<Snap> major_;
    std::optional<Ellipse> ellipse_;
    std::optional<double> startParam_;
    std::optional<double> majorTyped_;
    std::optional<double> minorTyped_;
};

// --- Splines -------------------------------------------------------------------------

class SplineTool final : public SketchTool {
public:
    explicit SplineTool(SplineMode mode) : mode_(mode) {}

    void move(const Snap& cursor) override { cursor_ = cursor; }

    bool click(Sketch& /*sketch*/, const Snap& cursor) override
    {
        if (points_.empty() || distance(points_.back().point, cursor.point) >= kMinLength) {
            points_.push_back(cursor);
        }
        return false;
    }

    bool finish(Sketch& sketch) override
    {
        if (points_.size() < 2) {
            cancel();
            return false;
        }
        std::vector<Vec2> positions;
        for (const Snap& s : points_) {
            positions.push_back(s.point);
        }
        if (mode_ == SplineMode::ControlPoints) {
            const EntityId id = sketch.addBSpline({positions, {}, 3});
            attach(sketch, Ref::start(id), points_.front());
            attach(sketch, Ref::end(id), points_.back());
            for (std::size_t i = 1; i + 1 < points_.size(); ++i) {
                attach(sketch, Ref::pole(id, int(i)), points_[i]);
            }
        } else {
            // Puntos de paso: la spline los interpola y quedan como puntos de
            // construcción sobre ella, para arrastrarlos o restringirlos.
            const EntityId id = sketch.addBSpline(interpolatingSpline(positions));
            for (std::size_t i = 0; i < points_.size(); ++i) {
                Ref at;
                if (i == 0) {
                    at = Ref::start(id);
                } else if (i + 1 == points_.size()) {
                    at = Ref::end(id);
                } else {
                    const EntityId fit = sketch.addPoint(positions[i], true);
                    sketch.addConstraint(Constraint::pointOnObject(Ref::of(fit), id));
                    at = Ref::of(fit);
                }
                attach(sketch, at, points_[i]);
            }
        }
        points_.clear();
        return true;
    }

    bool cancel() override
    {
        const bool active = !points_.empty();
        points_.clear();
        return active;
    }

    Preview preview() const override
    {
        if (points_.empty()) {
            return {};
        }
        std::vector<Vec2> positions;
        for (const Snap& s : points_) {
            positions.push_back(s.point);
        }
        if (distance(positions.back(), cursor_.point) >= kMinLength) {
            positions.push_back(cursor_.point);
        }
        if (positions.size() < 2) {
            return {};
        }
        if (mode_ == SplineMode::ControlPoints) {
            Preview p{{BSpline{positions, {}, 3}}};
            for (std::size_t i = 1; i < positions.size(); ++i) {
                p.geometry.push_back(Line{positions[i - 1], positions[i]});
            }
            return p;
        }
        return {{interpolatingSpline(positions)}};
    }

    std::string hint() const override
    {
        const char* what = mode_ == SplineMode::FitPoints ? "un punto de paso" : "un polo";
        if (points_.size() < 2) {
            return std::string("Clic para ") + what;
        }
        return std::string("Clic para ") + what + " · doble clic o Enter para terminar · Esc cancela";
    }

private:
    SplineMode mode_;
    Snap cursor_;
    std::vector<Snap> points_;
};

// --- Cónica ----------------------------------------------------------------------------

class ConicTool final : public SketchTool {
public:
    void move(const Snap& cursor) override { cursor_ = cursor; }

    bool click(Sketch& sketch, const Snap& cursor) override
    {
        if (!start_) {
            start_ = cursor;
            return false;
        }
        if (!end_) {
            if (distance(start_->point, cursor.point) >= kMinLength) {
                end_ = cursor;
            }
            return false;
        }
        return create(sketch, cursor.point);
    }

    std::vector<std::string> valueFields() const override
    {
        return end_ ? std::vector<std::string>{"Rho"} : std::vector<std::string>{};
    }

    std::vector<double> currentValues() const override
    {
        return end_ ? std::vector<double>{rho_} : std::vector<double>{};
    }

    bool applyValues(Sketch& sketch, const std::vector<std::optional<double>>& values) override
    {
        if (!end_ || values.empty() || !values[0]) {
            return false;
        }
        rho_ = std::clamp(*values[0], 0.01, 0.99);
        return create(sketch, cursor_.point);
    }

    bool cancel() override
    {
        const bool active = start_.has_value();
        start_.reset();
        end_.reset();
        return active;
    }

    Preview preview() const override
    {
        if (!start_) {
            return {};
        }
        if (!end_) {
            return {{Line{start_->point, cursor_.point}}};
        }
        return {{conic(cursor_.point), Line{start_->point, cursor_.point}, Line{cursor_.point, end_->point}}};
    }

    std::string hint() const override
    {
        if (!start_) {
            return "Clic para el inicio de la cónica";
        }
        return end_ ? "Clic para el vértice (rho " + std::to_string(rho_).substr(0, 4) + "; escríbelo para cambiarlo)"
                    : "Clic para el final de la cónica";
    }

private:
    // Cuadrática racional: peso central rho / (1 − rho).
    BSpline conic(Vec2 apex) const
    {
        return BSpline{{start_->point, apex, end_->point}, {1.0, rho_ / (1.0 - rho_), 1.0}, 2};
    }

    bool create(Sketch& sketch, Vec2 apex)
    {
        // Vértice alineado con los extremos: no hay curva.
        const Vec2 a = start_->point;
        const Vec2 b = end_->point;
        if (std::abs((b.x - a.x) * (apex.y - a.y) - (b.y - a.y) * (apex.x - a.x)) < kMinLength) {
            return false;
        }
        const EntityId id = sketch.addBSpline(conic(apex));
        attach(sketch, Ref::start(id), *start_);
        attach(sketch, Ref::end(id), *end_);
        cancel();
        return true;
    }

    double rho_ = 0.5;
    Snap cursor_;
    std::optional<Snap> start_;
    std::optional<Snap> end_;
};

// --- Ranura en arco --------------------------------------------------------------------

class ArcSlotTool final : public SketchTool {
public:
    void move(const Snap& cursor) override { cursor_ = cursor; }

    bool click(Sketch& sketch, const Snap& cursor) override
    {
        if (points_.size() < 2) {
            if (points_.empty() || distance(points_.back().point, cursor.point) >= kMinLength) {
                points_.push_back(cursor);
            }
            return false;
        }
        if (!center_) {
            center_ = fitArc(points_[0].point, points_[1].point, cursor.point);
            return false;
        }
        return create(sketch, halfWidthAt(cursor_.point), std::nullopt);
    }

    std::vector<std::string> valueFields() const override
    {
        return center_ ? std::vector<std::string>{"Ancho"} : std::vector<std::string>{};
    }

    std::vector<double> currentValues() const override
    {
        return center_ ? std::vector<double>{2.0 * halfWidthAt(cursor_.point)} : std::vector<double>{};
    }

    bool applyValues(Sketch& sketch, const std::vector<std::optional<double>>& values) override
    {
        if (!center_ || values.empty() || !values[0] || !(*values[0] > kMinLength)) {
            return false;
        }
        return create(sketch, *values[0] / 2.0, values[0]);
    }

    bool cancel() override
    {
        const bool active = !points_.empty();
        points_.clear();
        center_.reset();
        return active;
    }

    Preview preview() const override
    {
        if (points_.empty()) {
            return {};
        }
        if (points_.size() == 1) {
            return {{Line{points_[0].point, cursor_.point}}};
        }
        if (!center_) {
            if (const auto fit = fitArc(points_[0].point, points_[1].point, cursor_.point)) {
                return {{fit->arc}};
            }
            return {{Line{points_[0].point, points_[1].point}}};
        }
        const auto shape = layout(halfWidthAt(cursor_.point));
        if (!shape) {
            return {{center_->arc}};
        }
        return {{shape->outer, shape->inner, shape->capStart, shape->capEnd}};
    }

    std::string hint() const override
    {
        static const char* steps[] = {"Clic para el inicio del arco central", "Clic para el final del arco central",
                                      "Clic para un punto por el que pasa el arco central"};
        if (!center_) {
            return steps[points_.size()];
        }
        return "Clic para el ancho (o escríbelo)";
    }

private:
    struct Shape {
        Arc outer, inner, capStart, capEnd;
    };

    double halfWidthAt(Vec2 p) const
    {
        return std::abs(distance(p, center_->arc.center) - center_->arc.radius);
    }

    std::optional<Shape> layout(double r) const
    {
        const Arc& c = center_->arc;
        if (r < kMinLength || r >= c.radius) {
            return std::nullopt;
        }
        const double a0 = c.startAngle;
        const double a1 = c.endAngle;
        const Vec2 s0{c.center.x + c.radius * std::cos(a0), c.center.y + c.radius * std::sin(a0)};
        const Vec2 s1{c.center.x + c.radius * std::cos(a1), c.center.y + c.radius * std::sin(a1)};
        Shape s;
        s.outer = {c.center, c.radius + r, a0, a1};
        s.inner = {c.center, c.radius - r, a0, a1};
        // Tapas semicirculares hacia fuera del arco: la del inicio va del
        // borde interior al exterior; la del final, del exterior al interior.
        s.capStart = {s0, r, a0 + kPi, a0 + kTwoPi};
        s.capEnd = {s1, r, a1, a1 + kPi};
        return s;
    }

    bool create(Sketch& sketch, double r, std::optional<double> width)
    {
        const auto s = layout(r);
        if (!s) {
            return false;
        }
        const EntityId outer = sketch.addArc(s->outer.center, s->outer.radius, s->outer.startAngle, s->outer.endAngle);
        const EntityId inner = sketch.addArc(s->inner.center, s->inner.radius, s->inner.startAngle, s->inner.endAngle);
        const EntityId capStart =
            sketch.addArc(s->capStart.center, r, s->capStart.startAngle, s->capStart.endAngle);
        const EntityId capEnd = sketch.addArc(s->capEnd.center, r, s->capEnd.startAngle, s->capEnd.endAngle);
        // Conjunto mínimo y sin redundancias: bordes concéntricos y tapas
        // tangentes a ellos en las cuatro uniones (lo demás se deduce). Un
        // arco central de construcción unido a los centros de las tapas
        // repetiría información y el solver lo marcaría como redundante.
        sketch.addConstraint(Constraint::concentric(outer, inner));
        sketch.addConstraint(Constraint::tangentAt(Ref::start(inner), Ref::start(capStart)));
        sketch.addConstraint(Constraint::tangentAt(Ref::start(outer), Ref::end(capStart)));
        sketch.addConstraint(Constraint::tangentAt(Ref::end(outer), Ref::start(capEnd)));
        sketch.addConstraint(Constraint::tangentAt(Ref::end(inner), Ref::end(capEnd)));
        // Los clics del arco central quedan en los centros de las tapas.
        const bool swapped = center_->swapped;
        attach(sketch, Ref::center(swapped ? capEnd : capStart), points_[0]);
        attach(sketch, Ref::center(swapped ? capStart : capEnd), points_[1]);
        if (width) {
            sketch.addConstraint(Constraint::diameter(capStart, *width));
        }
        cancel();
        return true;
    }

    Snap cursor_;
    std::vector<Snap> points_;
    std::optional<ArcFit> center_;
};

// Resuelve A·x = b (Gauss con pivote parcial); A es n×n por filas.
std::vector<double> solveLinear(std::vector<double> a, std::vector<double> b)
{
    const std::size_t n = b.size();
    for (std::size_t col = 0; col < n; ++col) {
        std::size_t pivot = col;
        for (std::size_t row = col + 1; row < n; ++row) {
            if (std::abs(a[row * n + col]) > std::abs(a[pivot * n + col])) {
                pivot = row;
            }
        }
        for (std::size_t k = 0; k < n; ++k) {
            std::swap(a[col * n + k], a[pivot * n + k]);
        }
        std::swap(b[col], b[pivot]);
        for (std::size_t row = col + 1; row < n; ++row) {
            const double f = a[row * n + col] / a[col * n + col];
            for (std::size_t k = col; k < n; ++k) {
                a[row * n + k] -= f * a[col * n + k];
            }
            b[row] -= f * b[col];
        }
    }
    std::vector<double> x(n);
    for (std::size_t i = n; i-- > 0;) {
        double sum = b[i];
        for (std::size_t k = i + 1; k < n; ++k) {
            sum -= a[i * n + k] * x[k];
        }
        x[i] = sum / a[i * n + i];
    }
    return x;
}

} // namespace

BSpline interpolatingSpline(const std::vector<Vec2>& points)
{
    const std::size_t n = points.size();
    BSpline spline{points, std::vector<double>(n, 1.0), 3};
    const int p = spline.effectiveDegree();
    if (n <= 2) {
        return spline;
    }
    // Parámetros de interpolación: abscisas de Greville de los nudos
    // uniformes (dan un sistema bien condicionado).
    std::vector<double> flat;
    const auto knots = spline.knots();
    const auto mult = spline.multiplicities();
    for (std::size_t i = 0; i < knots.size(); ++i) {
        flat.insert(flat.end(), std::size_t(mult[i]), knots[i]);
    }
    std::vector<double> params(n);
    for (std::size_t i = 0; i < n; ++i) {
        double sum = 0.0;
        for (int k = 1; k <= p; ++k) {
            sum += flat[i + std::size_t(k)];
        }
        params[i] = sum / p / knots.back();
    }
    // Matriz de funciones base: N[i][j] = B_j(u_i), evaluando la spline con
    // un único polo unitario.
    std::vector<double> basis(n * n);
    for (std::size_t j = 0; j < n; ++j) {
        BSpline unit{std::vector<Vec2>(n, Vec2{0.0, 0.0}), std::vector<double>(n, 1.0), p};
        unit.poles[j] = {1.0, 0.0};
        for (std::size_t i = 0; i < n; ++i) {
            basis[i * n + j] = unit.pointAt(params[i]).x;
        }
    }
    std::vector<double> xs(n), ys(n);
    for (std::size_t i = 0; i < n; ++i) {
        xs[i] = points[i].x;
        ys[i] = points[i].y;
    }
    const auto px = solveLinear(basis, xs);
    const auto py = solveLinear(basis, ys);
    for (std::size_t i = 0; i < n; ++i) {
        spline.poles[i] = {px[i], py[i]};
    }
    return spline;
}

std::unique_ptr<SketchTool> makeEllipseTool() { return std::make_unique<EllipseTool>(false); }
std::unique_ptr<SketchTool> makeEllipseArcTool() { return std::make_unique<EllipseTool>(true); }
std::unique_ptr<SketchTool> makeSplineTool(SplineMode mode) { return std::make_unique<SplineTool>(mode); }
std::unique_ptr<SketchTool> makeConicTool() { return std::make_unique<ConicTool>(); }
std::unique_ptr<SketchTool> makeArcSlotTool() { return std::make_unique<ArcSlotTool>(); }

} // namespace nuf::sketch
