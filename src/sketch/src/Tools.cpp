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
            // Si empieza en el extremo de un arco, un tramo casi tangente se
            // endereza a la tangente y se crea la restricción Tangente.
            arcExit_.reset();
            if (cursor.kind == SnapKind::Point && (cursor.ref.part == Part::Start || cursor.ref.part == Part::End)) {
                if (const Entity* e = sketch.entity(cursor.ref.entity); e && std::holds_alternative<Arc>(e->geometry)) {
                    arcExit_ = exitDirection(sketch, cursor.ref);
                }
            }
            // Si empieza en el extremo de una curva, se puede seguir con un arco tangente.
            direction_ = cursor.kind == SnapKind::Point ? exitDirection(sketch, cursor.ref) : std::nullopt;
            return false;
        }
        Snap end = cursor;
        const Straight straight = straighten(start_->point, end);
        Relation relation = Relation::None;
        bool tangent = false;
        if (straight == Straight::None) {
            relation = relateEnd(end, tangent);
        }
        return commit(sketch, end, straight, std::nullopt, std::nullopt, relation, tangent);
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
        if (straighten(start_->point, end) == Straight::None) {
            bool tangent = false;
            relateEnd(end, tangent);
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
        std::optional<Circle> round{};  // referencia lejana que es un arco o un círculo
    };

    // Relación inferida del tramo con la tangente del arco del que se parte
    // (solo hacia delante) o con la línea de referencia.
    Relation relateEnd(Snap& end, bool& tangent) const
    {
        tangent = false;
        matched_.reset();
        if (arcExit_) {
            Snap probe = end;
            const Line along{start_->point, {start_->point.x + arcExit_->x, start_->point.y + arcExit_->y}};
            if (relate(start_->point, along, probe) == Relation::Parallel
                && (probe.point.x - start_->point.x) * arcExit_->x + (probe.point.y - start_->point.y) * arcExit_->y > 0.0) {
                end = probe;
                tangent = true;
                return Relation::Parallel;
            }
        }
        // La línea de la que se parte (o el tramo anterior) manda; si no, una línea despierta lejana.
        if (reference_) {
            const Relation relation = relate(start_->point, reference_->line, end);
            if (relation != Relation::None) {
                matched_ = reference_;
                return relation;
            }
        }
        for (const AwakeLine& awake : awakeLines_) {
            if (reference_ && awake.id == reference_->id) {
                continue;
            }
            const Relation relation = awake.round ? relateRound(start_->point, *awake.round, end)
                                                  : relate(start_->point, awake.line, end);
            if (relation != Relation::None) {
                matched_ = Reference{awake.id, awake.line, awake.round};
                return relation;
            }
        }
        return Relation::None;
    }

    bool commit(Sketch& sketch, const Snap& end, Straight straight, std::optional<double> length,
                std::optional<double> angle, Relation relation = Relation::None, bool tangent = false)
    {
        if (distance(start_->point, end.point) < kMinLength) {
            return false;
        }
        const EntityId line = sketch.addLine(start_->point, end.point);
        // La tangente ya une los dos extremos: no se añade además la coincidencia.
        if (!tangent) {
            attach(sketch, Ref::start(line), *start_);
        }
        attach(sketch, Ref::end(line), end);
        if (straight == Straight::Horizontal) {
            sketch.addConstraint(Constraint::horizontal(line));
        } else if (straight == Straight::Vertical) {
            sketch.addConstraint(Constraint::vertical(line));
        }
        if (tangent) {
            const Constraint c = Constraint::tangentAt(Ref::start(line), start_->ref);
            if (sketch.canAdd(c)) {
                sketch.addConstraint(c);
            }
        } else if (relation != Relation::None && matched_) {
            // Con un arco o un círculo: tangente a él, o recta que pasa por su centro (perpendicular a la curva).
            const Constraint c = matched_->round
                ? (relation == Relation::Tangent ? Constraint::tangent(line, matched_->id)
                                                 : Constraint::pointOnObject(Ref::center(matched_->id), line))
                : (relation == Relation::Parallel ? Constraint::parallel(line, matched_->id)
                                                  : Constraint::perpendicular(line, matched_->id));
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
        arcExit_.reset();

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
        arcExit_.reset();
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
        bool tangent = false;
        if (straighten(start_->point, end) == Straight::None) {
            relation = relateEnd(end, tangent);
        }
        result.geometry = {Line{start_->point, end.point}};
        if (tangent) {
            result.badge = "T";
            result.badgeAt = {(start_->point.x + end.point.x) / 2, (start_->point.y + end.point.y) / 2};
        } else if (relation != Relation::None && matched_ && matched_->round) {
            // Referencia redonda: con ⊥ la guía va del tramo al centro; con T solo se muestra la «T».
            const Vec2 center = matched_->round->center;
            if (relation == Relation::Perpendicular) {
                result.guides.push_back({start_->point, center});
            }
            result.badge = relation == Relation::Tangent ? "T" : "⊥";
            result.badgeAt = {(start_->point.x + end.point.x) / 2, (start_->point.y + end.point.y) / 2};
        } else if (relation != Relation::None) {
            result.guides.push_back({matched_->line.start, matched_->line.end});
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
    std::optional<Vec2> arcExit_;  // tangente del arco del que se parte
    std::optional<Reference> reference_;  // línea anterior de la cadena (o la que se continúa)
    std::vector<AwakeLine> awakeLines_;   // líneas despiertas (referencias lejanas)
    mutable std::optional<Reference> matched_;  // la referencia con la que se infirió la última relación

public:
    void setAwakeLines(std::vector<AwakeLine> lines) override { awakeLines_ = std::move(lines); }
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
            // Si parte del extremo de una línea o un arco, un arco casi
            // tangente a ella se endereza a la tangente.
            direction_ = cursor.kind == SnapKind::Point ? exitDirection(sketch, cursor.ref) : std::nullopt;
            return false;
        }
        if (!end_) {
            if (distance(start_->point, cursor.point) >= kMinLength) {
                end_ = cursor;
            }
            return false;
        }
        auto fit = fitArc(start_->point, end_->point, cursor.point);
        if (!fit) {
            return false;
        }
        bool tangent = false;
        if (const auto t = inferTangent(*fit, cursor.point)) {
            fit = t;
            tangent = true;
        }
        const Arc& a = fit->arc;
        const EntityId arc = sketch.addArc(a.center, a.radius, a.startAngle, a.endAngle);
        // Si el arco quedó invertido, su inicio es el segundo punto marcado.
        const Ref atStart = fit->swapped ? Ref::end(arc) : Ref::start(arc);
        const Ref atEnd = fit->swapped ? Ref::start(arc) : Ref::end(arc);
        if (tangent) {
            // La tangente ya une el extremo: no se añade además la coincidencia.
            const Constraint c = Constraint::tangentAt(atStart, start_->ref);
            if (sketch.canAdd(c)) {
                sketch.addConstraint(c);
            } else {
                attach(sketch, atStart, *start_);
            }
        } else {
            attach(sketch, atStart, *start_);
        }
        attach(sketch, atEnd, *end_);
        start_.reset();
        end_.reset();
        direction_.reset();
        return true;
    }

    bool cancel() override
    {
        const bool active = start_.has_value();
        start_.reset();
        end_.reset();
        direction_.reset();
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
        if (auto fit = fitArc(start_->point, end_->point, cursor_.point)) {
            Preview result;
            if (const auto t = inferTangent(*fit, cursor_.point)) {
                fit = t;
                result.badge = "T";
                result.badgeAt = {(start_->point.x + end_->point.x) / 2, (start_->point.y + end_->point.y) / 2};
            }
            result.geometry = {fit->arc};
            return result;
        }
        return {{Line{start_->point, end_->point}}};
    }

    std::string hint() const override
    {
        if (!start_) {
            return "Clic para el inicio del arco";
        }
        return end_ ? "Clic para un punto por el que pasa el arco (cerca de la tangente se endereza)" : "Clic para el final del arco";
    }

private:
    // Si el arco por los tres puntos sale del inicio a menos de
    // kStraightenAngle de la tangente de la curva de la que parte (hacia
    // delante), devuelve el arco exactamente tangente con ese inicio y final.
    std::optional<ArcFit> inferTangent(const ArcFit& fit, Vec2 through) const
    {
        if (!direction_ || start_->kind != SnapKind::Point) {
            return std::nullopt;
        }
        const Vec2 radial{start_->point.x - fit.arc.center.x, start_->point.y - fit.arc.center.y};
        // Sentido en que se recorre el arco: inicio → por el punto → final.
        const double cross = (through.x - start_->point.x) * (end_->point.y - start_->point.y)
                             - (through.y - start_->point.y) * (end_->point.x - start_->point.x);
        const double sign = cross > 0.0 ? 1.0 : -1.0;
        const Vec2 along{-sign * radial.y, sign * radial.x};
        const double length = std::hypot(along.x, along.y);
        if (length < kMinLength) {
            return std::nullopt;
        }
        const double cosine = (along.x * direction_->x + along.y * direction_->y) / length;
        if (cosine < std::cos(kStraightenAngle)) {
            return std::nullopt;
        }
        return tangentArc(start_->point, *direction_, end_->point);
    }

    Snap cursor_;
    std::optional<Snap> start_;
    std::optional<Snap> end_;
    std::optional<Vec2> direction_;
};

} // namespace

std::unique_ptr<SketchTool> makeLineTool() { return std::make_unique<LineTool>(); }
std::unique_ptr<SketchTool> makeRectangleTool() { return std::make_unique<RectangleTool>(); }
std::unique_ptr<SketchTool> makeCircleTool() { return std::make_unique<CircleTool>(); }
std::unique_ptr<SketchTool> makeArcTool() { return std::make_unique<ArcTool>(); }

} // namespace nuf::sketch
