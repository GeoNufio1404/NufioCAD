#include <nuf/sketch/Tools.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <optional>

namespace nuf::sketch {

namespace {

constexpr double kMinLength = 1e-6;

double distance(Vec2 a, Vec2 b) { return std::hypot(b.x - a.x, b.y - a.y); }

// Une un punto recién creado con aquello a lo que se ajustó el cursor.
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
    case SnapKind::None:
    case SnapKind::Grid:
        break;
    }
}

// Si el segmento start→end está a menos de kStraightenAngle de la horizontal
// o la vertical, lo endereza. Solo si el extremo no se apoya en un punto:
// moverlo rompería esa coincidencia.
enum class Straight { None, Horizontal, Vertical };

Straight straighten(Vec2 start, Snap& end)
{
    if (end.kind == SnapKind::Point || end.kind == SnapKind::Center || end.kind == SnapKind::Midpoint) {
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

// --- Línea -------------------------------------------------------------------

class LineTool final : public SketchTool {
public:
    void move(const Snap& cursor) override { cursor_ = cursor; }

    bool click(Sketch& sketch, const Snap& cursor) override
    {
        if (!start_) {
            start_ = cursor;
            first_ = cursor;
            return false;
        }
        Snap end = cursor;
        const Straight straight = straighten(start_->point, end);
        if (distance(start_->point, end.point) < kMinLength) {
            return false;
        }
        const EntityId line = sketch.addLine(start_->point, end.point);
        attach(sketch, Ref::start(line), *start_);
        attach(sketch, Ref::end(line), end);
        if (straight == Straight::Horizontal) {
            sketch.addConstraint(Constraint::horizontal(line));
        } else if (straight == Straight::Vertical) {
            sketch.addConstraint(Constraint::vertical(line));
        }

        // Cerrar en el primer punto de la cadena la termina.
        const bool closed = end.kind == SnapKind::Point && distance(end.point, first_->point) < kMinLength;
        if (closed) {
            start_.reset();
            first_.reset();
        } else {
            start_ = Snap{end.point, SnapKind::Point, Ref::end(line)};
            if (first_->kind != SnapKind::Point) {
                // El primer punto de la cadena es ahora el inicio de esta línea.
                first_ = Snap{start_->point, SnapKind::Point, Ref::start(line)};
            }
        }
        return true;
    }

    bool cancel() override
    {
        const bool active = start_.has_value();
        start_.reset();
        first_.reset();
        return active;
    }

    Preview preview() const override
    {
        if (!start_) {
            return {};
        }
        Snap end = cursor_;
        straighten(start_->point, end);
        return {{Line{start_->point, end.point}}};
    }

    std::string hint() const override
    {
        return start_ ? "Clic para el siguiente punto · Esc o doble clic para terminar"
                      : "Clic para el primer punto de la línea";
    }

private:
    Snap cursor_;
    std::optional<Snap> start_;
    std::optional<Snap> first_;
};

// --- Rectángulo ----------------------------------------------------------------

class RectangleTool final : public SketchTool {
public:
    void move(const Snap& cursor) override { cursor_ = cursor; }

    bool click(Sketch& sketch, const Snap& cursor) override
    {
        if (!corner_) {
            corner_ = cursor;
            return false;
        }
        const Vec2 a = corner_->point;
        const Vec2 c = cursor.point;
        if (std::abs(c.x - a.x) < kMinLength || std::abs(c.y - a.y) < kMinLength) {
            return false;
        }
        const Vec2 b{c.x, a.y};
        const Vec2 d{a.x, c.y};
        const EntityId ab = sketch.addLine(a, b);
        const EntityId bc = sketch.addLine(b, c);
        const EntityId cd = sketch.addLine(c, d);
        const EntityId da = sketch.addLine(d, a);
        sketch.addConstraint(Constraint::coincident(Ref::end(ab), Ref::start(bc)));
        sketch.addConstraint(Constraint::coincident(Ref::end(bc), Ref::start(cd)));
        sketch.addConstraint(Constraint::coincident(Ref::end(cd), Ref::start(da)));
        sketch.addConstraint(Constraint::coincident(Ref::end(da), Ref::start(ab)));
        sketch.addConstraint(Constraint::horizontal(ab));
        sketch.addConstraint(Constraint::horizontal(cd));
        sketch.addConstraint(Constraint::vertical(bc));
        sketch.addConstraint(Constraint::vertical(da));
        attach(sketch, Ref::start(ab), *corner_);
        attach(sketch, Ref::start(cd), cursor);
        corner_.reset();
        return true;
    }

    bool cancel() override
    {
        const bool active = corner_.has_value();
        corner_.reset();
        return active;
    }

    Preview preview() const override
    {
        if (!corner_) {
            return {};
        }
        const Vec2 a = corner_->point;
        const Vec2 c = cursor_.point;
        const Vec2 b{c.x, a.y};
        const Vec2 d{a.x, c.y};
        return {{Line{a, b}, Line{b, c}, Line{c, d}, Line{d, a}}};
    }

    std::string hint() const override
    {
        return corner_ ? "Clic para la esquina opuesta" : "Clic para la primera esquina del rectángulo";
    }

private:
    Snap cursor_;
    std::optional<Snap> corner_;
};

// --- Círculo -----------------------------------------------------------------

class CircleTool final : public SketchTool {
public:
    void move(const Snap& cursor) override { cursor_ = cursor; }

    bool click(Sketch& sketch, const Snap& cursor) override
    {
        if (!center_) {
            center_ = cursor;
            return false;
        }
        const double radius = distance(center_->point, cursor.point);
        if (radius < kMinLength) {
            return false;
        }
        const EntityId circle = sketch.addCircle(center_->point, radius);
        attach(sketch, Ref::center(circle), *center_);
        center_.reset();
        return true;
    }

    bool cancel() override
    {
        const bool active = center_.has_value();
        center_.reset();
        return active;
    }

    Preview preview() const override
    {
        if (!center_) {
            return {};
        }
        const double radius = distance(center_->point, cursor_.point);
        if (radius < kMinLength) {
            return {};
        }
        return {{Circle{center_->point, radius}}};
    }

    std::string hint() const override
    {
        return center_ ? "Clic para fijar el radio" : "Clic para el centro del círculo";
    }

private:
    Snap cursor_;
    std::optional<Snap> center_;
};

// --- Arco por 3 puntos -------------------------------------------------------------

// Arco antihorario de a a b que pasa por through. Si through queda en el otro
// lado, el arco va de b a a (swapped = true). nullopt si están alineados.
struct ArcFit {
    Arc arc;
    bool swapped = false;
};

std::optional<ArcFit> fitArc(Vec2 a, Vec2 b, Vec2 through)
{
    // Centro del círculo que pasa por los tres puntos.
    const double d = 2.0 * (a.x * (b.y - through.y) + b.x * (through.y - a.y) + through.x * (a.y - b.y));
    const double scale = std::max({distance(a, b), distance(a, through), distance(b, through)});
    if (std::abs(d) < 1e-9 * scale * scale) {
        return std::nullopt;
    }
    const double a2 = a.x * a.x + a.y * a.y;
    const double b2 = b.x * b.x + b.y * b.y;
    const double t2 = through.x * through.x + through.y * through.y;
    const Vec2 center{(a2 * (b.y - through.y) + b2 * (through.y - a.y) + t2 * (a.y - b.y)) / d,
                      (a2 * (through.x - b.x) + b2 * (a.x - through.x) + t2 * (b.x - a.x)) / d};
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

class ArcTool final : public SketchTool {
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
        const auto fit = fitArc(start_->point, end_->point, cursor.point);
        if (!fit) {
            return false;
        }
        const Arc& a = fit->arc;
        const EntityId arc = sketch.addArc(a.center, a.radius, a.startAngle, a.endAngle);
        // Si el arco quedó invertido, su inicio es el segundo punto marcado.
        attach(sketch, Ref::start(arc), fit->swapped ? *end_ : *start_);
        attach(sketch, Ref::end(arc), fit->swapped ? *start_ : *end_);
        start_.reset();
        end_.reset();
        return true;
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
        if (const auto fit = fitArc(start_->point, end_->point, cursor_.point)) {
            return {{fit->arc}};
        }
        return {{Line{start_->point, end_->point}}};
    }

    std::string hint() const override
    {
        if (!start_) {
            return "Clic para el inicio del arco";
        }
        return end_ ? "Clic para un punto por el que pasa el arco" : "Clic para el final del arco";
    }

private:
    Snap cursor_;
    std::optional<Snap> start_;
    std::optional<Snap> end_;
};

} // namespace

std::unique_ptr<SketchTool> makeLineTool() { return std::make_unique<LineTool>(); }
std::unique_ptr<SketchTool> makeRectangleTool() { return std::make_unique<RectangleTool>(); }
std::unique_ptr<SketchTool> makeCircleTool() { return std::make_unique<CircleTool>(); }
std::unique_ptr<SketchTool> makeArcTool() { return std::make_unique<ArcTool>(); }

} // namespace nuf::sketch
