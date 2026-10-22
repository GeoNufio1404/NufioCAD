// Herramientas de edición del sketch (Fase 2c): recortar, extender, partir,
// empalme, chaflán, desfase, simetría, mover/copiar, girar, escalar y
// patrones. La geometría la calculan las funciones de Edit.hpp.

#include <nuf/sketch/Edit.hpp>
#include <nuf/sketch/Pick.hpp>
#include <nuf/sketch/Tools.hpp>

#include "ToolSupport.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace nuf::sketch {

namespace {

using namespace detail;

constexpr double kPi = std::numbers::pi;

// --- Recortar, extender, partir ---------------------------------------------------

class CurveEditTool final : public SketchTool {
public:
    explicit CurveEditTool(CurveEdit edit) : edit_(edit) {}

    void move(const Snap& /*cursor*/) override {}

    bool click(Sketch& sketch, const Snap& cursor) override
    {
        const EntityId curve = curveAt(sketch, cursor);
        if (curve == 0) {
            return false;
        }
        switch (edit_) {
        case CurveEdit::Trim: return trim(sketch, curve, cursor.point);
        case CurveEdit::Extend: return extend(sketch, curve, cursor.point);
        case CurveEdit::Break: return breakCurve(sketch, curve, cursor.point);
        }
        return false;
    }

    bool cancel() override { return false; }
    Preview preview() const override { return {}; }

    std::string hint() const override
    {
        switch (edit_) {
        case CurveEdit::Trim: return "Clic en el tramo de curva que quieras quitar";
        case CurveEdit::Extend: return "Clic cerca del extremo que quieras alargar hasta la siguiente curva";
        case CurveEdit::Break: return "Clic en una curva para partirla en sus cruces";
        }
        return {};
    }

private:
    CurveEdit edit_;
};

// --- Empalme y chaflán -------------------------------------------------------------

class FilletTool final : public SketchTool {
public:
    explicit FilletTool(bool chamfer) : chamfer_(chamfer) {}

    void move(const Snap& /*cursor*/) override {}

    bool click(Sketch& sketch, const Snap& cursor) override
    {
        if (cursor.kind != SnapKind::Point) {
            return false;  // hace falta clicar justo en la esquina
        }
        const auto created = chamfer_ ? chamfer(sketch, cursor.point, value_) : fillet(sketch, cursor.point, value_);
        return created.has_value();
    }

    std::vector<std::string> valueFields() const override { return {chamfer_ ? "Distancia" : "Radio"}; }
    std::vector<double> currentValues() const override { return {value_}; }

    bool applyValues(Sketch& /*sketch*/, const std::vector<std::optional<double>>& values) override
    {
        if (!values.empty() && values[0] && *values[0] > 0.0) {
            value_ = *values[0];
        }
        return false;
    }

    bool cancel() override { return false; }
    Preview preview() const override { return {}; }

    std::string hint() const override
    {
        return chamfer_ ? "Clic en la esquina entre dos líneas (escribe la distancia para cambiarla)"
                        : "Clic en la esquina entre dos líneas (escribe el radio para cambiarlo)";
    }

private:
    bool chamfer_;
    double value_ = 5.0;
};

// --- Desfase -------------------------------------------------------------------------------

class OffsetTool final : public SketchTool {
public:
    void move(const Snap& cursor) override { cursor_ = cursor; }

    bool click(Sketch& sketch, const Snap& cursor) override
    {
        if (!snapshot_) {
            const EntityId curve = curveAt(sketch, cursor);
            if (curve == 0) {
                return false;
            }
            chain_ = chainOf(sketch, curve);
            if (chain_.empty()) {
                return false;
            }
            snapshot_ = sketch;
            return false;
        }
        return apply(sketch, signedDistance(cursor_.point, std::nullopt));
    }

    std::vector<std::string> valueFields() const override
    {
        return snapshot_ ? std::vector<std::string>{"Distancia"} : std::vector<std::string>{};
    }

    std::vector<double> currentValues() const override
    {
        return snapshot_ ? std::vector<double>{std::abs(signedDistance(cursor_.point, std::nullopt))}
                         : std::vector<double>{};
    }

    bool applyValues(Sketch& sketch, const std::vector<std::optional<double>>& values) override
    {
        if (!snapshot_ || values.empty() || !values[0] || !(*values[0] > 0.0)) {
            return false;
        }
        return apply(sketch, signedDistance(cursor_.point, *values[0]));
    }

    bool cancel() override
    {
        const bool active = snapshot_.has_value();
        snapshot_.reset();
        chain_.clear();
        return active;
    }

    Preview preview() const override
    {
        if (!snapshot_) {
            return {};
        }
        Sketch trial = *snapshot_;
        Preview p;
        for (EntityId id : offset(trial, chain_, signedDistance(cursor_.point, std::nullopt))) {
            p.geometry.push_back(trial.entity(id)->geometry);
        }
        return p;
    }

    std::string hint() const override
    {
        return snapshot_ ? "Mueve el cursor al lado del desfase y haz clic (o escribe la distancia)"
                         : "Clic en una curva (se desfasa toda su cadena)";
    }

private:
    // Distancia con signo: el lado (izquierda/derecha de la cadena) es el que
    // deja el desfase más cerca del cursor.
    double signedDistance(Vec2 p, std::optional<double> magnitude) const
    {
        const Entity& first = *snapshot_->entity(chain_.front());
        const double d = magnitude.value_or(distanceToCurve(first, p));
        double best = d;
        double bestGap = 1e300;
        for (double candidate : {d, -d}) {
            Sketch trial = *snapshot_;
            const auto created = offset(trial, chain_, candidate);
            if (created.empty()) {
                continue;
            }
            const double gap = distanceToCurve(*trial.entity(created.front()), p);
            if (gap < bestGap) {
                bestGap = gap;
                best = candidate;
            }
        }
        return best;
    }

    bool apply(Sketch& sketch, double distance)
    {
        const bool ok = !offset(sketch, chain_, distance).empty();
        cancel();
        return ok;
    }

    Snap cursor_;
    std::optional<Sketch> snapshot_;
    std::vector<EntityId> chain_;
};

// --- Herramientas sobre la selección -------------------------------------------------------

// Base: guarda la selección y su geometría (para la vista previa).
class SelectionTool : public SketchTool {
public:
    SelectionTool(const Sketch& sketch, std::vector<EntityId> selection) : selection_(std::move(selection))
    {
        for (EntityId id : selection_) {
            if (const Entity* e = sketch.entity(id); e && !e->reference) {
                shapes_.push_back(e->geometry);
            }
        }
    }

    void move(const Snap& cursor) override { cursor_ = cursor; }
    bool done() const override { return done_; }

protected:
    Preview previewWith(const Similarity& t, int copies = 1) const
    {
        Preview p;
        Similarity step = t;
        for (int k = 1; k <= copies; ++k) {
            for (const Geometry& g : shapes_) {
                p.geometry.push_back(transformed(g, step));
            }
            step.angle += t.angle;
            step.translation = {step.translation.x + t.translation.x, step.translation.y + t.translation.y};
        }
        return p;
    }

    bool finished(bool changed)
    {
        done_ = changed;
        return changed;
    }

    std::vector<EntityId> selection_;
    std::vector<Geometry> shapes_;
    Snap cursor_;
    bool done_ = false;
};

class MirrorTool final : public SelectionTool {
public:
    using SelectionTool::SelectionTool;

    bool click(Sketch& sketch, const Snap& cursor) override
    {
        const EntityId axis = curveAt(sketch, cursor);
        const Entity* e = sketch.entity(axis);
        if (!e || !std::holds_alternative<Line>(e->geometry)) {
            return false;
        }
        return finished(!mirror(sketch, selection_, axis).empty());
    }

    bool cancel() override { return false; }
    Preview preview() const override { return {}; }
    std::string hint() const override { return "Clic en la línea (o eje) de simetría"; }
};

class MoveTool final : public SelectionTool {
public:
    MoveTool(const Sketch& sketch, std::vector<EntityId> selection, bool copy)
        : SelectionTool(sketch, std::move(selection))
        , copy_(copy)
    {}

    bool click(Sketch& sketch, const Snap& cursor) override
    {
        if (!base_) {
            base_ = cursor.point;
            return false;
        }
        return apply(sketch, {cursor.point.x - base_->x, cursor.point.y - base_->y});
    }

    std::vector<std::string> valueFields() const override
    {
        return base_ ? std::vector<std::string>{"ΔX", "ΔY"} : std::vector<std::string>{};
    }

    std::vector<double> currentValues() const override
    {
        if (!base_) {
            return {};
        }
        return {cursor_.point.x - base_->x, cursor_.point.y - base_->y};
    }

    bool applyValues(Sketch& sketch, const std::vector<std::optional<double>>& values) override
    {
        if (!base_ || values.size() < 2) {
            return false;
        }
        const auto current = currentValues();
        return apply(sketch, {values[0].value_or(current[0]), values[1].value_or(current[1])});
    }

    bool cancel() override
    {
        const bool active = base_.has_value();
        base_.reset();
        return active;
    }

    Preview preview() const override
    {
        if (!base_) {
            return {};
        }
        Similarity t;
        t.translation = {cursor_.point.x - base_->x, cursor_.point.y - base_->y};
        return previewWith(t);
    }

    std::string hint() const override
    {
        return base_ ? "Clic en el destino (o escribe ΔX, Tab, ΔY)"
                     : (copy_ ? "Copiar: clic en el punto base" : "Mover: clic en el punto base");
    }

private:
    bool apply(Sketch& sketch, Vec2 delta)
    {
        Similarity t;
        t.translation = delta;
        return finished(!transform(sketch, selection_, t, copy_).empty());
    }

    bool copy_;
    std::optional<Vec2> base_;
};

class RotateTool final : public SelectionTool {
public:
    using SelectionTool::SelectionTool;

    bool click(Sketch& sketch, const Snap& cursor) override
    {
        if (!center_) {
            center_ = cursor.point;
            return false;
        }
        return apply(sketch, cursorAngle());
    }

    std::vector<std::string> valueFields() const override
    {
        return center_ ? std::vector<std::string>{"Ángulo"} : std::vector<std::string>{};
    }

    std::vector<double> currentValues() const override
    {
        return center_ ? std::vector<double>{cursorAngle() * 180.0 / kPi} : std::vector<double>{};
    }

    bool applyValues(Sketch& sketch, const std::vector<std::optional<double>>& values) override
    {
        if (!center_ || values.empty() || !values[0]) {
            return false;
        }
        return apply(sketch, *values[0] * kPi / 180.0);
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
        Similarity t;
        t.pivot = *center_;
        t.angle = cursorAngle();
        return previewWith(t);
    }

    std::string hint() const override
    {
        return center_ ? "Clic para el ángulo (medido desde el eje X) o escríbelo en grados"
                       : "Girar: clic en el centro de giro";
    }

private:
    double cursorAngle() const { return std::atan2(cursor_.point.y - center_->y, cursor_.point.x - center_->x); }

    bool apply(Sketch& sketch, double angle)
    {
        Similarity t;
        t.pivot = *center_;
        t.angle = angle;
        return finished(!transform(sketch, selection_, t, false).empty());
    }

    std::optional<Vec2> center_;
};

class ScaleTool final : public SelectionTool {
public:
    using SelectionTool::SelectionTool;

    bool click(Sketch& /*sketch*/, const Snap& cursor) override
    {
        if (!base_) {
            base_ = cursor.point;
        }
        return false;
    }

    std::vector<std::string> valueFields() const override
    {
        return base_ ? std::vector<std::string>{"Factor"} : std::vector<std::string>{};
    }

    std::vector<double> currentValues() const override
    {
        return base_ ? std::vector<double>{1.0} : std::vector<double>{};
    }

    bool applyValues(Sketch& sketch, const std::vector<std::optional<double>>& values) override
    {
        if (!base_ || values.empty() || !values[0] || !(*values[0] > 0.0)) {
            return false;
        }
        Similarity t;
        t.pivot = *base_;
        t.scale = *values[0];
        return finished(!transform(sketch, selection_, t, false).empty());
    }

    bool cancel() override
    {
        const bool active = base_.has_value();
        base_.reset();
        return active;
    }

    Preview preview() const override { return {}; }

    std::string hint() const override
    {
        return base_ ? "Escribe el factor de escala y pulsa Enter" : "Escalar: clic en el punto base";
    }

private:
    std::optional<Vec2> base_;
};

class RectangularPatternTool final : public SelectionTool {
public:
    using SelectionTool::SelectionTool;

    bool click(Sketch& sketch, const Snap& /*cursor*/) override { return apply(sketch, values_); }

    std::vector<std::string> valueFields() const override { return {"Columnas", "Filas", "Paso X", "Paso Y"}; }
    std::vector<double> currentValues() const override { return values_; }

    bool applyValues(Sketch& sketch, const std::vector<std::optional<double>>& values) override
    {
        std::vector<double> v = values_;
        for (std::size_t i = 0; i < v.size() && i < values.size(); ++i) {
            if (values[i]) {
                v[i] = *values[i];
            }
        }
        return apply(sketch, v);
    }

    bool cancel() override { return false; }

    Preview preview() const override
    {
        Preview p;
        for (int j = 0; j < int(values_[1]); ++j) {
            for (int i = 0; i < int(values_[0]); ++i) {
                if (i == 0 && j == 0) {
                    continue;
                }
                Similarity t;
                t.translation = {i * values_[2], j * values_[3]};
                for (const Geometry& g : shapes_) {
                    p.geometry.push_back(transformed(g, t));
                }
            }
        }
        return p;
    }

    std::string hint() const override
    {
        return "Patrón rectangular: escribe columnas, filas y pasos (Tab entre ellos) y pulsa Enter, o haz clic";
    }

private:
    bool apply(Sketch& sketch, const std::vector<double>& v)
    {
        const int columns = std::max(1, int(std::lround(v[0])));
        const int rows = std::max(1, int(std::lround(v[1])));
        return finished(!rectangularPattern(sketch, selection_, columns, rows, {v[2], v[3]}).empty());
    }

    std::vector<double> values_{3.0, 1.0, 10.0, 10.0};
};

class CircularPatternTool final : public SelectionTool {
public:
    using SelectionTool::SelectionTool;

    bool click(Sketch& sketch, const Snap& cursor) override
    {
        if (!center_) {
            center_ = cursor.point;
            return false;
        }
        return apply(sketch, values_);
    }

    std::vector<std::string> valueFields() const override
    {
        return center_ ? std::vector<std::string>{"Copias", "Ángulo"} : std::vector<std::string>{};
    }

    std::vector<double> currentValues() const override { return center_ ? values_ : std::vector<double>{}; }

    bool applyValues(Sketch& sketch, const std::vector<std::optional<double>>& values) override
    {
        if (!center_) {
            return false;
        }
        std::vector<double> v = values_;
        for (std::size_t i = 0; i < v.size() && i < values.size(); ++i) {
            if (values[i]) {
                v[i] = *values[i];
            }
        }
        return apply(sketch, v);
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
        const int count = std::max(2, int(std::lround(values_[0])));
        const double total = values_[1] * kPi / 180.0;
        const bool full = std::abs(std::abs(values_[1]) - 360.0) < 1e-9;
        Similarity t;
        t.pivot = *center_;
        t.angle = full ? total / count : total / (count - 1);
        return previewWith(t, count - 1);
    }

    std::string hint() const override
    {
        return center_ ? "Escribe copias y ángulo total (Tab) y pulsa Enter, o haz clic"
                       : "Patrón circular: clic en el centro";
    }

private:
    bool apply(Sketch& sketch, const std::vector<double>& v)
    {
        const int count = std::max(2, int(std::lround(v[0])));
        return finished(!circularPattern(sketch, selection_, *center_, count, v[1] * kPi / 180.0).empty());
    }

    std::optional<Vec2> center_;
    std::vector<double> values_{6.0, 360.0};
};

} // namespace

std::unique_ptr<SketchTool> makeCurveEditTool(CurveEdit edit) { return std::make_unique<CurveEditTool>(edit); }
std::unique_ptr<SketchTool> makeFilletTool(bool chamfer) { return std::make_unique<FilletTool>(chamfer); }
std::unique_ptr<SketchTool> makeOffsetTool() { return std::make_unique<OffsetTool>(); }
std::unique_ptr<SketchTool> makeMirrorTool(const Sketch& sketch, std::vector<EntityId> selection)
{
    return std::make_unique<MirrorTool>(sketch, std::move(selection));
}
std::unique_ptr<SketchTool> makeMoveTool(const Sketch& sketch, std::vector<EntityId> selection, bool copy)
{
    return std::make_unique<MoveTool>(sketch, std::move(selection), copy);
}
std::unique_ptr<SketchTool> makeRotateTool(const Sketch& sketch, std::vector<EntityId> selection)
{
    return std::make_unique<RotateTool>(sketch, std::move(selection));
}
std::unique_ptr<SketchTool> makeScaleTool(const Sketch& sketch, std::vector<EntityId> selection)
{
    return std::make_unique<ScaleTool>(sketch, std::move(selection));
}
std::unique_ptr<SketchTool> makeRectangularPatternTool(const Sketch& sketch, std::vector<EntityId> selection)
{
    return std::make_unique<RectangularPatternTool>(sketch, std::move(selection));
}
std::unique_ptr<SketchTool> makeCircularPatternTool(const Sketch& sketch, std::vector<EntityId> selection)
{
    return std::make_unique<CircularPatternTool>(sketch, std::move(selection));
}

} // namespace nuf::sketch
