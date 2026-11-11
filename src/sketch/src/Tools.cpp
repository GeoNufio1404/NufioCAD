#include <nuf/sketch/Tools.hpp>

#include "ToolSupport.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <optional>

namespace nuf::sketch {

namespace {

using namespace detail;

// --- Línea -------------------------------------------------------------------

class LineTool final : public SketchTool {
public:
    void move(const Snap& cursor) override { cursor_ = cursor; }

    bool click(Sketch& sketch, const Snap& cursor) override
    {
        if (!start_) {
            start_ = cursor;
            first_ = cursor;
            // Si empieza en el extremo de una línea, esa es la referencia para
            // inferir perpendicular o paralela.
            reference_.reset();
            if (cursor.kind == SnapKind::Point && (cursor.ref.part == Part::Start || cursor.ref.part == Part::End)) {
                if (const Entity* e = sketch.entity(cursor.ref.entity)) {
                    if (const auto* line = std::get_if<Line>(&e->geometry)) {
                        reference_ = Reference{e->id, *line};
                    }
                }
            }
            // Si empieza en el extremo de una curva, se puede seguir con un arco tangente.
            direction_ = cursor.kind == SnapKind::Point ? exitDirection(sketch, cursor.ref) : std::nullopt;
            return false;
        }
        Snap end = cursor;
        const Straight straight = straighten(start_->point, end);
        Relation relation = Relation::None;
        if (straight == Straight::None && reference_) {
            relation = relate(start_->point, reference_->line, end);
        }
        return commit(sketch, end, straight, std::nullopt, std::nullopt, relation);
    }

    // Arrastrar desde el punto actual de la cadena dibuja un arco tangente
    // (como en Fusion); al soltar, la cadena sigue desde su final.
    bool dragMove(const Snap& from, const Snap& cursor) override
    {
        dragArc_.reset();
        if (!start_ || !direction_ || distance(from.point, start_->point) > kMinLength) {
            return false;
        }
        dragArc_ = tangentArc(start_->point, *direction_, cursor.point);
        return true;
    }

    bool dragRelease(Sketch& sketch, const Snap& from, const Snap& cursor) override
    {
        if (!dragMove(from, cursor) || !dragArc_ || start_->kind != SnapKind::Point) {
            dragArc_.reset();
            return false;
        }
        const ArcFit fit = *dragArc_;
        dragArc_.reset();
        const Arc& a = fit.arc;
        const EntityId arc = sketch.addArc(a.center, a.radius, a.startAngle, a.endAngle);
        const Ref arcStart = fit.swapped ? Ref::end(arc) : Ref::start(arc);
        const Ref arcEnd = fit.swapped ? Ref::start(arc) : Ref::end(arc);
        sketch.addConstraint(Constraint::tangentAt(arcStart, start_->ref));
        attach(sketch, arcEnd, cursor);
        start_ = Snap{*sketch.point(arcEnd), SnapKind::Point, arcEnd};
        direction_ = exitDirection(sketch, arcEnd);
        return true;
    }

    std::vector<std::string> valueFields() const override
    {
        if (!start_) {
            return {};
        }
        return {"Longitud", "Ángulo"};
    }

    std::vector<double> currentValues() const override
    {
        if (!start_) {
            return {};
        }
        Snap end = cursor_;
        if (straighten(start_->point, end) == Straight::None && reference_) {
            relate(start_->point, reference_->line, end);
        }
        const double dx = end.point.x - start_->point.x;
        const double dy = end.point.y - start_->point.y;
        return {std::hypot(dx, dy), std::atan2(dy, dx) * 180.0 / std::numbers::pi};
    }

    bool applyValues(Sketch& sketch, const std::vector<std::optional<double>>& values) override
    {
        if (!start_ || values.size() < 2) {
            return false;
        }
        const std::vector<double> current = currentValues();
        const double length = values[0].value_or(current[0]);
        const double degrees = values[1].value_or(current[1]);
        if (!(length > kMinLength)) {
            return false;
        }
        const double radians = degrees * std::numbers::pi / 180.0;
        Snap end{{start_->point.x + length * std::cos(radians), start_->point.y + length * std::sin(radians)}};
        // Un ángulo escrito múltiplo de 90° se fija como horizontal/vertical.
        Straight straight = Straight::None;
        std::optional<double> angle;
        if (values[1]) {
            const double quarter = std::remainder(degrees, 90.0);
            if (std::abs(quarter) < 1e-9) {
                straight = std::abs(std::remainder(degrees, 180.0)) < 1e-9 ? Straight::Horizontal : Straight::Vertical;
            } else {
                angle = radians;
            }
        } else {
            straight = straighten(start_->point, end);
            if (straight != Straight::None) {
                // Enderezado: se conserva la longitud escrita.
                const double sx = end.point.x >= start_->point.x ? 1.0 : -1.0;
                const double sy = end.point.y >= start_->point.y ? 1.0 : -1.0;
                end.point = straight == Straight::Horizontal ? Vec2{start_->point.x + sx * length, start_->point.y}
                                                             : Vec2{start_->point.x, start_->point.y + sy * length};
            }
        }
        return commit(sketch, end, straight, values[0], angle);
    }

private:
    struct Reference {
        EntityId id;
        Line line;
    };

    bool commit(Sketch& sketch, const Snap& end, Straight straight, std::optional<double> length,
                std::optional<double> angle, Relation relation = Relation::None)
    {
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
        if (relation != Relation::None && reference_) {
            const Constraint c = relation == Relation::Parallel ? Constraint::parallel(line, reference_->id)
                                                                : Constraint::perpendicular(line, reference_->id);
            if (sketch.canAdd(c)) {
                sketch.addConstraint(c);
            }
        }
        if (length) {
            sketch.addConstraint(Constraint::length(line, *length));
        }
        if (angle) {
            sketch.addConstraint(Constraint::angle(Sketch::kAxisX, line, *angle));
        }
        reference_ = Reference{line, Line{start_->point, end.point}};

        // Cerrar en el primer punto de la cadena la termina.
        const bool closed = end.kind == SnapKind::Point && distance(end.point, first_->point) < kMinLength;
        if (closed) {
            start_.reset();
            first_.reset();
        } else {
            direction_ = Vec2{(end.point.x - start_->point.x) / distance(start_->point, end.point),
                              (end.point.y - start_->point.y) / distance(start_->point, end.point)};
            start_ = Snap{end.point, SnapKind::Point, Ref::end(line)};
            if (first_->kind != SnapKind::Point) {
                // El primer punto de la cadena es ahora el inicio de esta línea.
                first_ = Snap{start_->point, SnapKind::Point, Ref::start(line)};
            }
        }
        return true;
    }

public:
    bool cancel() override
    {
        const bool active = start_.has_value();
        start_.reset();
        first_.reset();
        direction_.reset();
        dragArc_.reset();
        reference_.reset();
        return active;
    }

    Preview preview() const override
    {
        if (!start_) {
            return {};
        }
        Preview result;
        if (dragArc_) {
            result.geometry = {dragArc_->arc};
            return result;
        }
        Snap end = cursor_;
        Relation relation = Relation::None;
        if (straighten(start_->point, end) == Straight::None && reference_) {
            relation = relate(start_->point, reference_->line, end);
        }
        result.geometry = {Line{start_->point, end.point}};
        if (relation != Relation::None) {
            result.guides.push_back({reference_->line.start, reference_->line.end});
            result.badge = relation == Relation::Parallel ? "∥" : "⊥";
            result.badgeAt = {(start_->point.x + end.point.x) / 2, (start_->point.y + end.point.y) / 2};
        }
        return result;
    }

    std::string hint() const override
    {
        return start_ ? "Clic para el siguiente punto (o escribe la longitud; arrastra para un arco tangente) · Esc o doble clic para terminar · Ctrl: sin ajustes"
                      : "Clic para el primer punto de la línea · Ctrl: sin ajustes";
    }

private:
    Snap cursor_;
    std::optional<Snap> start_;
    std::optional<Snap> first_;
    std::optional<Vec2> direction_;  // con la que llega la cadena a start_
    std::optional<ArcFit> dragArc_;  // arco tangente mientras se arrastra
    std::optional<Reference> reference_;  // línea anterior de la cadena (o la que se continúa)
};

// --- Rectángulo ----------------------------------------------------------------

class RectangleTool final : public SketchTool {
public:
    void move(const Snap& cursor) override { cursor_ = cursor; }

    bool click(Sketch& sketch, const Snap& cursor) override
    {
        open_.reset();  // un clic nuevo da por buena la figura abierta
        if (!corner_) {
            corner_ = cursor;
            return false;
        }
        return create(sketch, cursor, std::nullopt, std::nullopt);
    }

    std::vector<std::string> valueFields() const override
    {
        if (!corner_ && !open_) {
            return {};
        }
        return {"Ancho", "Alto"};
    }

    std::vector<double> currentValues() const override
    {
        if (open_) {
            return {std::abs(open_->second.point.x - open_->first.point.x),
                    std::abs(open_->second.point.y - open_->first.point.y)};
        }
        if (!corner_) {
            return {};
        }
        return {std::abs(cursor_.point.x - corner_->point.x), std::abs(cursor_.point.y - corner_->point.y)};
    }

    bool applyValues(Sketch& sketch, const std::vector<std::optional<double>>& values) override
    {
        amended_ = false;
        if (open_) {
            amended_ = reopen(sketch, values);
            return amended_;
        }
        if (!corner_ || values.size() < 2) {
            return false;
        }
        // El cuadrante lo da el cursor; el tamaño, lo escrito.
        const Vec2 a = corner_->point;
        const double sx = cursor_.point.x >= a.x ? 1.0 : -1.0;
        const double sy = cursor_.point.y >= a.y ? 1.0 : -1.0;
        const std::vector<double> current = currentValues();
        const Snap opposite{{a.x + sx * values[0].value_or(current[0]), a.y + sy * values[1].value_or(current[1])}};
        return create(sketch, opposite, values[0], values[1]);
    }

private:
    // Figura abierta: la rehace con el ancho o el alto escritos.
    bool reopen(Sketch& sketch, const std::vector<std::optional<double>>& values)
    {
        if (values.size() < 2 || (!values[0] && !values[1])) {
            open_.reset();  // Enter sin valores: queda como está
            return false;
        }
        const OpenFigure old = *open_;
        const Vec2 a = old.first.point;
        const double sx = old.second.point.x >= a.x ? 1.0 : -1.0;
        const double sy = old.second.point.y >= a.y ? 1.0 : -1.0;
        const double width = values[0].value_or(std::abs(old.second.point.x - a.x));
        const double height = values[1].value_or(std::abs(old.second.point.y - a.y));
        if (!(width > kMinLength && height > kMinLength)) {
            return false;
        }
        if (!discardEntities(sketch, old.ids)) {
            open_.reset();
            return false;
        }
        corner_ = old.first;
        return create(sketch, Snap{{a.x + sx * width, a.y + sy * height}}, values[0] ? values[0] : old.typed[0],
                      values[1] ? values[1] : old.typed[1]);
    }

    bool create(Sketch& sketch, const Snap& cursor, std::optional<double> width, std::optional<double> height)
    {
        const Vec2 a = corner_->point;
        const Vec2 c = cursor.point;
        if (std::abs(c.x - a.x) < kMinLength || std::abs(c.y - a.y) < kMinLength) {
            return false;
        }
        const std::size_t mark = sketch.entities().size();
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
        if (width) {
            sketch.addConstraint(Constraint::length(ab, *width));
        }
        if (height) {
            sketch.addConstraint(Constraint::length(bc, *height));
        }
        open_ = OpenFigure{entitiesSince(sketch, mark), *corner_, cursor, {}, 0.0, {width, height}};
        corner_.reset();
        return true;
    }

public:
    bool cancel() override
    {
        if (open_) {
            open_.reset();
            return true;
        }
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
        if (open_) {
            return "Rectángulo creado: escribe ancho, Tab, alto y Enter para cambiarlo · clic o Esc para terminar";
        }
        return corner_ ? "Clic para la esquina opuesta (o escribe ancho, Tab, alto)"
                       : "Clic para la primera esquina del rectángulo";
    }

private:
    Snap cursor_;
    std::optional<Snap> corner_;
    std::optional<OpenFigure> open_;
    bool amended_ = false;

public:
    bool amendedLast() const override { return amended_; }
};

// --- Círculo -----------------------------------------------------------------

class CircleTool final : public SketchTool {
public:
    void move(const Snap& cursor) override { cursor_ = cursor; }

    bool click(Sketch& sketch, const Snap& cursor) override
    {
        open_.reset();  // un clic nuevo da por buena la figura abierta
        if (!center_) {
            center_ = cursor;
            return false;
        }
        return create(sketch, distance(center_->point, cursor.point), std::nullopt);
    }

    std::vector<std::string> valueFields() const override
    {
        if (!center_ && !open_) {
            return {};
        }
        return {"Diámetro"};
    }

    std::vector<double> currentValues() const override
    {
        if (open_) {
            return {2.0 * distance(open_->first.point, open_->second.point)};
        }
        if (!center_) {
            return {};
        }
        return {2.0 * distance(center_->point, cursor_.point)};
    }

    bool applyValues(Sketch& sketch, const std::vector<std::optional<double>>& values) override
    {
        amended_ = false;
        if (open_) {
            // Círculo abierto: se rehace con el diámetro escrito.
            if (values.empty() || !values[0]) {
                open_.reset();
                return false;
            }
            const OpenFigure old = *open_;
            if (!(*values[0] / 2.0 > kMinLength)) {
                return false;
            }
            if (!discardEntities(sketch, old.ids)) {
                open_.reset();
                return false;
            }
            center_ = old.first;
            amended_ = create(sketch, *values[0] / 2.0, values[0]);
            return amended_;
        }
        if (!center_ || values.empty()) {
            return false;
        }
        const double diameter = values[0].value_or(currentValues()[0]);
        return create(sketch, diameter / 2.0, values[0]);
    }

private:
    bool create(Sketch& sketch, double radius, std::optional<double> diameter)
    {
        if (radius < kMinLength) {
            return false;
        }
        const std::size_t mark = sketch.entities().size();
        const EntityId circle = sketch.addCircle(center_->point, radius);
        attach(sketch, Ref::center(circle), *center_);
        if (diameter) {
            sketch.addConstraint(Constraint::diameter(circle, *diameter));
        }
        open_ = OpenFigure{entitiesSince(sketch, mark), *center_,
                           Snap{{center_->point.x + radius, center_->point.y}}, {}, 0.0, {diameter}};
        center_.reset();
        return true;
    }

public:

    bool cancel() override
    {
        if (open_) {
            open_.reset();
            return true;
        }
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
        if (open_) {
            return "Círculo creado: escribe el diámetro y Enter para cambiarlo · clic o Esc para terminar";
        }
        return center_ ? "Clic para fijar el tamaño (o escribe el diámetro)" : "Clic para el centro del círculo";
    }

private:
    Snap cursor_;
    std::optional<Snap> center_;
    std::optional<OpenFigure> open_;
    bool amended_ = false;

public:
    bool amendedLast() const override { return amended_; }
};

// --- Arco por 3 puntos -------------------------------------------------------------


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
