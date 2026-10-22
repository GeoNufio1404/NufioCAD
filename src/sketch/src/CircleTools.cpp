// Variantes de círculos y arcos: círculo por 2 y 3 puntos, círculo tangente,
// arco por centro y arco tangente.

#include <nuf/sketch/Tools.hpp>

#include <nuf/sketch/Pick.hpp>

#include "ToolSupport.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace nuf::sketch {

namespace {

using namespace detail;

constexpr double kTwoPi = 2.0 * std::numbers::pi;

// --- Círculo por 2 puntos -----------------------------------------------------------

class TwoPointCircleTool final : public SketchTool {
public:
    void move(const Snap& cursor) override { cursor_ = cursor; }

    bool click(Sketch& sketch, const Snap& cursor) override
    {
        if (!first_) {
            first_ = cursor;
            return false;
        }
        return create(sketch, cursor, std::nullopt);
    }

    std::vector<std::string> valueFields() const override
    {
        return first_ ? std::vector<std::string>{"Diámetro"} : std::vector<std::string>{};
    }

    std::vector<double> currentValues() const override
    {
        return first_ ? std::vector<double>{distance(first_->point, cursor_.point)} : std::vector<double>{};
    }

    bool applyValues(Sketch& sketch, const std::vector<std::optional<double>>& values) override
    {
        if (!first_ || values.empty() || !values[0] || !(*values[0] > kMinLength)) {
            return false;
        }
        const Vec2 a = first_->point;
        const double len = distance(a, cursor_.point);
        const Vec2 u = len > kMinLength ? Vec2{(cursor_.point.x - a.x) / len, (cursor_.point.y - a.y) / len}
                                        : Vec2{1.0, 0.0};
        return create(sketch, Snap{{a.x + *values[0] * u.x, a.y + *values[0] * u.y}}, values[0]);
    }

    bool cancel() override
    {
        const bool active = first_.has_value();
        first_.reset();
        return active;
    }

    Preview preview() const override
    {
        if (!first_ || distance(first_->point, cursor_.point) < kMinLength) {
            return {};
        }
        return {{circleThrough(first_->point, cursor_.point)}};
    }

    std::string hint() const override
    {
        return first_ ? "Clic para el otro extremo del diámetro (o escribe el diámetro)"
                      : "Clic para un extremo del diámetro";
    }

private:
    static Circle circleThrough(Vec2 a, Vec2 b)
    {
        return {{(a.x + b.x) / 2.0, (a.y + b.y) / 2.0}, distance(a, b) / 2.0};
    }

    bool create(Sketch& sketch, const Snap& second, std::optional<double> diameter)
    {
        if (distance(first_->point, second.point) < kMinLength) {
            return false;
        }
        const Circle c = circleThrough(first_->point, second.point);
        const EntityId circle = sketch.addCircle(c.center, c.radius);
        attachOnCurve(sketch, circle, *first_);
        attachOnCurve(sketch, circle, second);
        if (diameter) {
            sketch.addConstraint(Constraint::diameter(circle, *diameter));
        }
        first_.reset();
        return true;
    }

    Snap cursor_;
    std::optional<Snap> first_;
};

// --- Círculo por 3 puntos -----------------------------------------------------------

class ThreePointCircleTool final : public SketchTool {
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
        const auto center = circumcenter(points_[0].point, points_[1].point, cursor.point);
        if (!center) {
            return false;
        }
        const EntityId circle = sketch.addCircle(*center, distance(*center, cursor.point));
        attachOnCurve(sketch, circle, points_[0]);
        attachOnCurve(sketch, circle, points_[1]);
        attachOnCurve(sketch, circle, cursor);
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
        if (points_.size() == 1) {
            return {{Line{points_[0].point, cursor_.point}}};
        }
        if (const auto center = circumcenter(points_[0].point, points_[1].point, cursor_.point)) {
            return {{Circle{*center, distance(*center, cursor_.point)}}};
        }
        return {{Line{points_[0].point, points_[1].point}}};
    }

    std::string hint() const override
    {
        static const char* steps[] = {"Clic para el primer punto del círculo", "Clic para el segundo punto",
                                      "Clic para el tercer punto"};
        return steps[points_.size()];
    }

private:
    Snap cursor_;
    std::vector<Snap> points_;
};

// --- Círculo tangente a 2 o 3 curvas ---------------------------------------------------

class TangentCircleTool final : public SketchTool {
public:
    explicit TangentCircleTool(int count) : count_(count) {}

    void move(const Snap& cursor) override { cursor_ = cursor; }

    bool click(Sketch& sketch, const Snap& cursor) override
    {
        if (int(curves_.size()) < count_) {
            const EntityId curve = curveAt(sketch, cursor);
            if (curve != 0 && std::ranges::find(curves_, curve) == curves_.end()) {
                curves_.push_back(curve);
                // Copia de las curvas para estimar el radio en la vista previa.
                entities_.push_back(*sketch.entity(curve));
            }
            return false;
        }
        const double radius = estimateRadius(cursor_.point);
        if (radius < kMinLength) {
            return false;
        }
        // Se crea donde indica el cursor; el solver lo lleva a la tangencia.
        const EntityId circle = sketch.addCircle(cursor.point, radius);
        for (EntityId curve : curves_) {
            sketch.addConstraint(Constraint::tangent(curve, circle));
        }
        cancel();
        return true;
    }

    bool cancel() override
    {
        const bool active = !curves_.empty();
        curves_.clear();
        entities_.clear();
        return active;
    }

    Preview preview() const override
    {
        if (int(curves_.size()) < count_) {
            return {};
        }
        const double radius = estimateRadius(cursor_.point);
        if (radius < kMinLength) {
            return {};
        }
        return {{Circle{cursor_.point, radius}}};
    }

    std::string hint() const override
    {
        if (int(curves_.size()) < count_) {
            return "Clic en la curva " + std::to_string(curves_.size() + 1) + " de " + std::to_string(count_)
                   + " a la que será tangente";
        }
        return "Clic cerca de donde debe quedar el círculo";
    }

private:
    // Radio inicial: la distancia media del cursor a las curvas elegidas.
    double estimateRadius(Vec2 p) const
    {
        double sum = 0.0;
        for (const Entity& e : entities_) {
            sum += distanceToCurve(e, p);
        }
        return entities_.empty() ? 0.0 : sum / double(entities_.size());
    }

    int count_;
    Snap cursor_;
    std::vector<EntityId> curves_;
    std::vector<Entity> entities_;
};

// --- Arco por centro ----------------------------------------------------------------

class CenterArcTool final : public SketchTool {
public:
    void move(const Snap& cursor) override
    {
        cursor_ = cursor;
        if (center_ && start_) {
            // Se acumula el giro del cursor para saber en qué sentido va el arco.
            const double angle = angleOf(cursor.point);
            sweep_ += std::remainder(angle - lastAngle_, kTwoPi);
            sweep_ = std::clamp(sweep_, -kTwoPi + 1e-3, kTwoPi - 1e-3);
            lastAngle_ = angle;
        }
    }

    bool click(Sketch& sketch, const Snap& cursor) override
    {
        if (!center_) {
            center_ = cursor;
            return false;
        }
        if (!start_) {
            if (distance(center_->point, cursor.point) >= kMinLength) {
                start_ = cursor;
                lastAngle_ = angleOf(cursor.point);
                sweep_ = 0.0;
            }
            return false;
        }
        const auto arc = currentArc();
        if (!arc) {
            return false;
        }
        const EntityId id = sketch.addArc(arc->arc.center, arc->arc.radius, arc->arc.startAngle, arc->arc.endAngle);
        attach(sketch, Ref::center(id), *center_);
        attach(sketch, arc->swapped ? Ref::end(id) : Ref::start(id), *start_);
        cancel();
        return true;
    }

    bool cancel() override
    {
        const bool active = center_.has_value();
        center_.reset();
        start_.reset();
        return active;
    }

    Preview preview() const override
    {
        if (!center_) {
            return {};
        }
        if (!start_) {
            return {{Line{center_->point, cursor_.point}}};
        }
        if (const auto arc = currentArc()) {
            return {{arc->arc}};
        }
        return {};
    }

    std::string hint() const override
    {
        if (!center_) {
            return "Clic para el centro del arco";
        }
        return start_ ? "Clic para el final (el sentido sigue al cursor)" : "Clic para el inicio del arco";
    }

private:
    double angleOf(Vec2 p) const { return std::atan2(p.y - center_->point.y, p.x - center_->point.x); }

    std::optional<ArcFit> currentArc() const
    {
        if (std::abs(sweep_) < 1e-6) {
            return std::nullopt;
        }
        const double radius = distance(center_->point, start_->point);
        const double from = angleOf(start_->point);
        if (sweep_ > 0.0) {
            return ArcFit{Arc{center_->point, radius, from, from + sweep_}, false};
        }
        // En sentido horario: el arco (antihorario) va del final al inicio.
        return ArcFit{Arc{center_->point, radius, from + sweep_, from}, true};
    }

    Snap cursor_;
    std::optional<Snap> center_;
    std::optional<Snap> start_;
    double lastAngle_ = 0.0;
    double sweep_ = 0.0;
};

// --- Arco tangente ------------------------------------------------------------------

class TangentArcTool final : public SketchTool {
public:
    void move(const Snap& cursor) override { cursor_ = cursor; }

    bool click(Sketch& sketch, const Snap& cursor) override
    {
        if (!start_) {
            // Debe empezar en el extremo de una línea o un arco.
            if (cursor.kind == SnapKind::Point) {
                if (const auto dir = exitDirection(sketch, cursor.ref)) {
                    start_ = cursor;
                    direction_ = *dir;
                }
            }
            return false;
        }
        const auto arc = currentArc(cursor.point);
        if (!arc) {
            return false;
        }
        const EntityId id = sketch.addArc(arc->arc.center, arc->arc.radius, arc->arc.startAngle, arc->arc.endAngle);
        sketch.addConstraint(Constraint::tangentAt(arc->swapped ? Ref::end(id) : Ref::start(id), start_->ref));
        attach(sketch, arc->swapped ? Ref::start(id) : Ref::end(id), cursor);
        // Se puede seguir encadenando desde el final del arco.
        start_ = Snap{cursor.point, SnapKind::Point, arc->swapped ? Ref::start(id) : Ref::end(id)};
        direction_ = *exitDirection(sketch, start_->ref);
        return true;
    }

    bool cancel() override
    {
        const bool active = start_.has_value();
        start_.reset();
        return active;
    }

    Preview preview() const override
    {
        if (!start_) {
            return {};
        }
        if (const auto arc = currentArc(cursor_.point)) {
            return {{arc->arc}};
        }
        return {{Line{start_->point, cursor_.point}}};
    }

    std::string hint() const override
    {
        return start_ ? "Clic para el final del arco · Esc para terminar"
                      : "Clic en el extremo de una línea o arco para continuarlo con un arco tangente";
    }

private:
    std::optional<ArcFit> currentArc(Vec2 end) const { return tangentArc(start_->point, direction_, end); }

    Snap cursor_;
    std::optional<Snap> start_;
    Vec2 direction_;
};

} // namespace

std::unique_ptr<SketchTool> makeTwoPointCircleTool() { return std::make_unique<TwoPointCircleTool>(); }
std::unique_ptr<SketchTool> makeThreePointCircleTool() { return std::make_unique<ThreePointCircleTool>(); }
std::unique_ptr<SketchTool> makeTangentCircleTool(int curves)
{
    return std::make_unique<TangentCircleTool>(curves);
}
std::unique_ptr<SketchTool> makeCenterArcTool() { return std::make_unique<CenterArcTool>(); }
std::unique_ptr<SketchTool> makeTangentArcTool() { return std::make_unique<TangentArcTool>(); }

} // namespace nuf::sketch
