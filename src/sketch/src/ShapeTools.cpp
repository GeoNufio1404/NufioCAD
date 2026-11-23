// Herramientas de formas compuestas: rectángulo por 3 puntos y por centro,
// polígonos regulares y ranuras.

#include <nuf/sketch/Tools.hpp>

#include "ToolSupport.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace nuf::sketch {

namespace {

using namespace detail;

Vec2 add(Vec2 a, Vec2 b) { return {a.x + b.x, a.y + b.y}; }
Vec2 sub(Vec2 a, Vec2 b) { return {a.x - b.x, a.y - b.y}; }
Vec2 scale(double k, Vec2 v) { return {k * v.x, k * v.y}; }
double dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
Vec2 unit(Vec2 v)
{
    const double len = std::hypot(v.x, v.y);
    return len > 0.0 ? scale(1.0 / len, v) : Vec2{1.0, 0.0};
}
Vec2 leftNormal(Vec2 u) { return {-u.y, u.x}; }

// Contorno cerrado de líneas unidas por sus extremos. Devuelve las líneas.
std::vector<EntityId> closedPolyline(Sketch& sketch, const std::vector<Vec2>& points)
{
    std::vector<EntityId> lines;
    for (std::size_t i = 0; i < points.size(); ++i) {
        lines.push_back(sketch.addLine(points[i], points[(i + 1) % points.size()]));
    }
    for (std::size_t i = 0; i < lines.size(); ++i) {
        sketch.addConstraint(Constraint::coincident(Ref::end(lines[i]), Ref::start(lines[(i + 1) % lines.size()])));
    }
    return lines;
}

std::vector<Geometry> polylinePreview(const std::vector<Vec2>& points)
{
    std::vector<Geometry> result;
    for (std::size_t i = 0; i < points.size(); ++i) {
        result.push_back(Line{points[i], points[(i + 1) % points.size()]});
    }
    return result;
}

// --- Rectángulo por 3 puntos -----------------------------------------------------

class ThreePointRectangleTool final : public SketchTool {
public:
    void move(const Snap& cursor) override { cursor_ = cursor; }

    bool click(Sketch& sketch, const Snap& cursor) override
    {
        open_.reset();  // un clic nuevo da por buena la figura abierta
        if (!a_) {
            a_ = cursor;
            // Si parte del extremo de una línea, el primer lado puede quedar
            // paralelo o perpendicular a ella.
            reference_.reset();
            relation_ = Relation::None;
            if (cursor.kind == SnapKind::Point && (cursor.ref.part == Part::Start || cursor.ref.part == Part::End)) {
                if (const Entity* e = sketch.entity(cursor.ref.entity)) {
                    if (const auto* line = std::get_if<Line>(&e->geometry)) {
                        reference_ = {e->id, *line};
                    }
                }
            }
            return false;
        }
        if (!b_) {
            Snap b = cursor;
            straight_ = straighten(a_->point, b);
            relation_ = straight_ == Straight::None ? relateFirst(b) : Relation::None;
            if (distance(a_->point, b.point) >= kMinLength) {
                b_ = b;
            }
            return false;
        }
        return create(sketch, cursor, std::nullopt);
    }

    std::vector<std::string> valueFields() const override
    {
        if (open_) {
            return {"Longitud", "Alto"};
        }
        if (!a_) {
            return {};
        }
        return b_ ? std::vector<std::string>{"Alto"} : std::vector<std::string>{"Longitud"};
    }

    std::vector<double> currentValues() const override
    {
        if (open_) {
            const Vec2 n = leftNormal(unit(sub(open_->second.point, open_->first.point)));
            return {distance(open_->first.point, open_->second.point),
                    std::abs(dot(sub(open_->third.point, open_->second.point), n))};
        }
        if (!a_) {
            return {};
        }
        if (!b_) {
            Snap b = cursor_;
            if (straighten(a_->point, b) == Straight::None) {
                relateFirst(b);
            }
            return {distance(a_->point, b.point)};
        }
        return {std::abs(height(cursor_.point))};
    }

    bool applyValues(Sketch& sketch, const std::vector<std::optional<double>>& values) override
    {
        amended_ = false;
        if (open_) {
            if (values.size() < 2 || (!values[0] && !values[1])) {
                open_.reset();  // Enter sin valores: queda como está
                return false;
            }
            const OpenFigure old = *open_;
            const Vec2 u = unit(sub(old.second.point, old.first.point));
            const Vec2 n = leftNormal(u);
            const double signedHeight = dot(sub(old.third.point, old.second.point), n);
            const double length = values[0].value_or(distance(old.first.point, old.second.point));
            const double height = values[1].value_or(std::abs(signedHeight));
            if (!(length > kMinLength && height > kMinLength)) {
                return false;
            }
            if (!discardEntities(sketch, old.ids)) {
                open_.reset();
                return false;
            }
            a_ = old.first;
            b_ = Snap{add(old.first.point, scale(length, u))};
            length_ = values[0] ? values[0] : old.typed[0];
            amended_ = create(sketch, Snap{add(b_->point, scale(signedHeight < 0.0 ? -height : height, n))},
                              values[1] ? values[1] : old.typed[1]);
            return amended_;
        }
        if (!a_ || values.empty() || !values[0] || !(*values[0] > kMinLength)) {
            return false;
        }
        if (!b_) {
            // Lado con la longitud escrita, en la dirección del cursor.
            Snap b = cursor_;
            straight_ = straighten(a_->point, b);
            relation_ = straight_ == Straight::None ? relateFirst(b) : Relation::None;
            const Vec2 u = unit(sub(b.point, a_->point));
            b_ = Snap{add(a_->point, scale(*values[0], u))};
            length_ = *values[0];
            return false;
        }
        const double sign = height(cursor_.point) < 0.0 ? -1.0 : 1.0;
        const Vec2 n = leftNormal(unit(sub(b_->point, a_->point)));
        return create(sketch, Snap{add(b_->point, scale(sign * *values[0], n))}, values[0]);
    }

    bool cancel() override
    {
        if (open_) {
            open_.reset();
            return true;
        }
        const bool active = a_.has_value();
        a_.reset();
        b_.reset();
        length_.reset();
        return active;
    }

    Preview preview() const override
    {
        if (!a_) {
            return {};
        }
        if (!b_) {
            Snap b = cursor_;
            Preview result;
            const Relation relation = straighten(a_->point, b) == Straight::None ? relateFirst(b) : Relation::None;
            result.geometry = {Line{a_->point, b.point}};
            if (relation != Relation::None) {
                result.guides.push_back({matched_->second.start, matched_->second.end});
                result.badge = relation == Relation::Parallel ? "∥" : "⊥";
                result.badgeAt = {(a_->point.x + b.point.x) / 2, (a_->point.y + b.point.y) / 2};
            }
            return result;
        }
        Preview result;
        result.geometry = polylinePreview(corners(cursor_.point));
        return result;
    }

    std::string hint() const override
    {
        if (open_) {
            return "Rectángulo creado: escribe longitud, Tab, alto y Enter para cambiarlo · clic o Esc para terminar";
        }
        if (!a_) {
            return "Clic para la primera esquina";
        }
        return b_ ? "Clic para la altura (o escríbela)" : "Clic para el final del primer lado (o escribe su longitud)";
    }

private:
    // La línea de la que parte el lado manda; si no, una línea despierta lejana.
    Relation relateFirst(Snap& b) const
    {
        matched_.reset();
        if (reference_) {
            const Relation relation = relate(a_->point, reference_->second, b);
            if (relation != Relation::None) {
                matched_ = reference_;
                return relation;
            }
        }
        for (const AwakeLine& awake : awakeLines_) {
            if (reference_ && awake.id == reference_->first) {
                continue;
            }
            const Relation relation = relate(a_->point, awake.line, b);
            if (relation != Relation::None) {
                matched_ = std::make_pair(awake.id, awake.line);
                return relation;
            }
        }
        return Relation::None;
    }

    // Altura con signo de p respecto al primer lado (positiva a su izquierda).
    double height(Vec2 p) const
    {
        const Vec2 n = leftNormal(unit(sub(b_->point, a_->point)));
        return dot(sub(p, b_->point), n);
    }

    std::vector<Vec2> corners(Vec2 p) const
    {
        const Vec2 n = leftNormal(unit(sub(b_->point, a_->point)));
        const Vec2 h = scale(height(p), n);
        return {a_->point, b_->point, add(b_->point, h), add(a_->point, h)};
    }

    bool create(Sketch& sketch, const Snap& third, std::optional<double> typedHeight)
    {
        if (std::abs(height(third.point)) < kMinLength) {
            return false;
        }
        const std::size_t mark = sketch.entities().size();
        const auto lines = closedPolyline(sketch, corners(third.point));
        sketch.addConstraint(Constraint::perpendicular(lines[0], lines[1]));
        sketch.addConstraint(Constraint::parallel(lines[0], lines[2]));
        sketch.addConstraint(Constraint::parallel(lines[1], lines[3]));
        if (straight_ == Straight::Horizontal) {
            sketch.addConstraint(Constraint::horizontal(lines[0]));
        } else if (straight_ == Straight::Vertical) {
            sketch.addConstraint(Constraint::vertical(lines[0]));
        } else if (relation_ != Relation::None && matched_) {
            const Constraint c = relation_ == Relation::Parallel ? Constraint::parallel(lines[0], matched_->first)
                                                                 : Constraint::perpendicular(lines[0], matched_->first);
            if (sketch.canAdd(c)) {
                sketch.addConstraint(c);
            }
        }
        attach(sketch, Ref::start(lines[0]), *a_);
        attach(sketch, Ref::end(lines[0]), *b_);
        attachOnCurve(sketch, lines[2], third);
        if (length_) {
            sketch.addConstraint(Constraint::length(lines[0], *length_));
        }
        if (typedHeight) {
            sketch.addConstraint(Constraint::length(lines[1], *typedHeight));
        }
        open_ = OpenFigure{entitiesSince(sketch, mark), *a_, *b_, third, 0.0, {length_, typedHeight}};
        a_.reset();
        b_.reset();
        length_.reset();
        return true;
    }

    Snap cursor_;
    std::optional<Snap> a_;
    std::optional<Snap> b_;
    Straight straight_ = Straight::None;
    Relation relation_ = Relation::None;
    std::optional<std::pair<EntityId, Line>> reference_;  // línea de la que parte el primer lado
    std::vector<AwakeLine> awakeLines_;                   // líneas despiertas (referencias lejanas)
    mutable std::optional<std::pair<EntityId, Line>> matched_;  // con la que se infirió la relación
    std::optional<double> length_;
    std::optional<OpenFigure> open_;
    bool amended_ = false;

public:
    bool amendedLast() const override { return amended_; }
    void setAwakeLines(std::vector<AwakeLine> lines) override { awakeLines_ = std::move(lines); }
};

// --- Rectángulo por centro -----------------------------------------------------------

class CenterRectangleTool final : public SketchTool {
public:
    void move(const Snap& cursor) override { cursor_ = cursor; }

    bool click(Sketch& sketch, const Snap& cursor) override
    {
        open_.reset();  // un clic nuevo da por buena la figura abierta
        if (!center_) {
            center_ = cursor;
            return false;
        }
        return create(sketch, cursor, std::nullopt, std::nullopt);
    }

    std::vector<std::string> valueFields() const override
    {
        return center_ || open_ ? std::vector<std::string>{"Ancho", "Alto"} : std::vector<std::string>{};
    }

    std::vector<double> currentValues() const override
    {
        if (open_) {
            return {2.0 * std::abs(open_->second.point.x - open_->first.point.x),
                    2.0 * std::abs(open_->second.point.y - open_->first.point.y)};
        }
        if (!center_) {
            return {};
        }
        return {2.0 * std::abs(cursor_.point.x - center_->point.x), 2.0 * std::abs(cursor_.point.y - center_->point.y)};
    }

    bool applyValues(Sketch& sketch, const std::vector<std::optional<double>>& values) override
    {
        amended_ = false;
        if (open_) {
            if (values.size() < 2 || (!values[0] && !values[1])) {
                open_.reset();  // Enter sin valores: queda como está
                return false;
            }
            const OpenFigure old = *open_;
            const Vec2 o = old.first.point;
            const double sx = old.second.point.x >= o.x ? 1.0 : -1.0;
            const double sy = old.second.point.y >= o.y ? 1.0 : -1.0;
            const double width = values[0].value_or(2.0 * std::abs(old.second.point.x - o.x));
            const double height = values[1].value_or(2.0 * std::abs(old.second.point.y - o.y));
            if (!(width > 2.0 * kMinLength && height > 2.0 * kMinLength)) {
                return false;
            }
            if (!discardEntities(sketch, old.ids)) {
                open_.reset();
                return false;
            }
            center_ = old.first;
            amended_ = create(sketch, Snap{{o.x + sx * width / 2.0, o.y + sy * height / 2.0}},
                              values[0] ? values[0] : old.typed[0], values[1] ? values[1] : old.typed[1]);
            return amended_;
        }
        if (!center_ || values.size() < 2) {
            return false;
        }
        const Vec2 c = center_->point;
        const std::vector<double> current = currentValues();
        const double sx = cursor_.point.x >= c.x ? 1.0 : -1.0;
        const double sy = cursor_.point.y >= c.y ? 1.0 : -1.0;
        const Snap corner{{c.x + sx * values[0].value_or(current[0]) / 2.0, c.y + sy * values[1].value_or(current[1]) / 2.0}};
        return create(sketch, corner, values[0], values[1]);
    }

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
        return {polylinePreview(corners(cursor_.point))};
    }

    std::string hint() const override
    {
        if (open_) {
            return "Rectángulo creado: escribe ancho, Tab, alto y Enter para cambiarlo · clic o Esc para terminar";
        }
        return center_ ? "Clic para una esquina (o escribe ancho, Tab, alto)" : "Clic para el centro del rectángulo";
    }

private:
    // Esquinas a, b, p, d con p = la esquina indicada; ab y pd horizontales.
    std::vector<Vec2> corners(Vec2 p) const
    {
        const Vec2 c = center_->point;
        const double dx = p.x - c.x;
        const double dy = p.y - c.y;
        return {{c.x - dx, c.y - dy}, {c.x + dx, c.y - dy}, p, {c.x - dx, c.y + dy}};
    }

    bool create(Sketch& sketch, const Snap& corner, std::optional<double> width, std::optional<double> height)
    {
        const Vec2 c = center_->point;
        if (std::abs(corner.point.x - c.x) < kMinLength || std::abs(corner.point.y - c.y) < kMinLength) {
            return false;
        }
        const std::size_t mark = sketch.entities().size();
        const auto pts = corners(corner.point);
        const auto lines = closedPolyline(sketch, pts);
        sketch.addConstraint(Constraint::horizontal(lines[0]));
        sketch.addConstraint(Constraint::horizontal(lines[2]));
        sketch.addConstraint(Constraint::vertical(lines[1]));
        sketch.addConstraint(Constraint::vertical(lines[3]));
        // Diagonal de construcción con el centro en su punto medio.
        const EntityId diagonal = sketch.addLine(pts[0], pts[2], true);
        sketch.addConstraint(Constraint::coincident(Ref::start(diagonal), Ref::start(lines[0])));
        sketch.addConstraint(Constraint::coincident(Ref::end(diagonal), Ref::start(lines[2])));
        const EntityId middle = sketch.addPoint(c, true);
        sketch.addConstraint(Constraint::midpoint(Ref::of(middle), diagonal));
        attach(sketch, Ref::of(middle), *center_);
        attach(sketch, Ref::start(lines[2]), corner);
        if (width) {
            sketch.addConstraint(Constraint::length(lines[0], *width));
        }
        if (height) {
            sketch.addConstraint(Constraint::length(lines[1], *height));
        }
        open_ = OpenFigure{entitiesSince(sketch, mark), *center_, corner, {}, 0.0, {width, height}};
        center_.reset();
        return true;
    }

    Snap cursor_;
    std::optional<Snap> center_;
    std::optional<OpenFigure> open_;
    bool amended_ = false;

public:
    bool amendedLast() const override { return amended_; }
};

// --- Polígono regular ------------------------------------------------------------------

class PolygonTool final : public SketchTool {
public:
    explicit PolygonTool(PolygonMode mode) : mode_(mode) {}

    void move(const Snap& cursor) override { cursor_ = cursor; }

    bool click(Sketch& sketch, const Snap& cursor) override
    {
        open_.reset();  // un clic nuevo da por buena la figura abierta
        if (!first_) {
            first_ = cursor;
            return false;
        }
        return create(sketch, cursor, std::nullopt);
    }

    std::vector<std::string> valueFields() const override
    {
        if (!first_ && !open_) {
            return {"Lados"};
        }
        return {"Lados", mode_ == PolygonMode::Edge ? "Lado" : "Diámetro"};
    }

    std::vector<double> currentValues() const override
    {
        if (open_) {
            const double d = distance(open_->first.point, open_->second.point);
            return {double(sides_), mode_ == PolygonMode::Edge ? d : 2.0 * d};
        }
        if (!first_) {
            return {double(sides_)};
        }
        const double d = distance(first_->point, cursor_.point);
        return {double(sides_), mode_ == PolygonMode::Edge ? d : 2.0 * d};
    }

    bool applyValues(Sketch& sketch, const std::vector<std::optional<double>>& values) override
    {
        amended_ = false;
        if (open_) {
            amended_ = reopen(sketch, values);
            return amended_;
        }
        if (!values.empty() && values[0]) {
            sides_ = std::clamp(int(std::lround(*values[0])), 3, 64);
        }
        if (!first_ || values.size() < 2) {
            return false;  // solo cambió el número de lados
        }
        if (!values[1]) {
            return create(sketch, cursor_, std::nullopt);
        }
        // Tamaño escrito, en la dirección del cursor.
        const double size = mode_ == PolygonMode::Edge ? *values[1] : *values[1] / 2.0;
        const Vec2 u = unit(sub(cursor_.point, first_->point));
        return create(sketch, Snap{add(first_->point, scale(size, u))}, values[1]);
    }

    bool cancel() override
    {
        if (open_) {
            open_.reset();
            return true;
        }
        const bool active = first_.has_value();
        first_.reset();
        return active;
    }

    Preview preview() const override
    {
        if (!first_) {
            return {};
        }
        const auto shape = layout(cursor_.point);
        if (!shape) {
            return {};
        }
        Preview p{polylinePreview(shape->vertices)};
        p.geometry.push_back(Circle{shape->center, shape->circleRadius});
        return p;
    }

    std::string hint() const override
    {
        const std::string sides = std::to_string(sides_) + " lados";
        if (open_) {
            return "Polígono creado (" + sides + "): escribe lados, Tab, tamaño y Enter para cambiarlos · clic o Esc para terminar";
        }
        if (!first_) {
            return mode_ == PolygonMode::Edge ? "Polígono (" + sides + "): clic para el primer vértice · escribe el número de lados"
                                              : "Polígono (" + sides + "): clic para el centro · escribe el número de lados";
        }
        switch (mode_) {
        case PolygonMode::Inscribed: return "Clic para un vértice (o escribe lados, Tab, diámetro)";
        case PolygonMode::Circumscribed: return "Clic para el punto medio de un lado (o escribe lados, Tab, diámetro)";
        case PolygonMode::Edge: return "Clic para el segundo vértice (o escribe lados, Tab, lado)";
        }
        return {};
    }

private:
    struct Shape {
        Vec2 center;
        std::vector<Vec2> vertices;
        double circleRadius;  // inscrito/lado: por los vértices; circunscrito: tangente a los lados
    };

    std::optional<Shape> layout(Vec2 second) const
    {
        const Vec2 a = first_->point;
        const double half = std::numbers::pi / sides_;
        Shape shape;
        double circumradius = 0.0;
        double startAngle = 0.0;
        switch (mode_) {
        case PolygonMode::Inscribed:
            shape.center = a;
            circumradius = distance(a, second);
            startAngle = std::atan2(second.y - a.y, second.x - a.x);
            shape.circleRadius = circumradius;
            break;
        case PolygonMode::Circumscribed: {
            shape.center = a;
            const double apothem = distance(a, second);
            circumradius = apothem / std::cos(half);
            startAngle = std::atan2(second.y - a.y, second.x - a.x) - half;
            shape.circleRadius = apothem;
            break;
        }
        case PolygonMode::Edge: {
            const double side = distance(a, second);
            if (side < kMinLength) {
                return std::nullopt;
            }
            // El polígono queda a la izquierda del lado a → second.
            const Vec2 u = unit(sub(second, a));
            const double apothem = side / 2.0 / std::tan(half);
            shape.center = add(scale(0.5, add(a, second)), scale(apothem, leftNormal(u)));
            circumradius = side / 2.0 / std::sin(half);
            startAngle = std::atan2(a.y - shape.center.y, a.x - shape.center.x);
            shape.circleRadius = circumradius;
            break;
        }
        }
        if (circumradius < kMinLength) {
            return std::nullopt;
        }
        for (int k = 0; k < sides_; ++k) {
            const double t = startAngle + 2.0 * std::numbers::pi * k / sides_;
            shape.vertices.push_back({shape.center.x + circumradius * std::cos(t), shape.center.y + circumradius * std::sin(t)});
        }
        return shape;
    }

    // Figura abierta: la rehace con otro número de lados o tamaño.
    bool reopen(Sketch& sketch, const std::vector<std::optional<double>>& values)
    {
        if (values.empty() || (!values[0] && (values.size() < 2 || !values[1]))) {
            open_.reset();  // Enter sin valores: queda como está
            return false;
        }
        const OpenFigure old = *open_;
        const int oldSides = sides_;
        if (values[0]) {
            sides_ = std::clamp(int(std::lround(*values[0])), 3, 64);
        }
        const bool typedSize = values.size() >= 2 && values[1].has_value();
        const double size = typedSize ? (mode_ == PolygonMode::Edge ? *values[1] : *values[1] / 2.0)
                                      : distance(old.first.point, old.second.point);
        const Vec2 second = add(old.first.point, scale(size, unit(sub(old.second.point, old.first.point))));
        first_ = old.first;
        if (!layout(second) || !discardEntities(sketch, old.ids)) {
            const bool gone = !layout(second) ? false : true;  // layout válido pero ya no existe: se cierra
            sides_ = oldSides;
            first_.reset();
            if (gone) {
                open_.reset();
            }
            return false;
        }
        return create(sketch, Snap{second}, typedSize ? values[1] : old.typed[0]);
    }

    bool create(Sketch& sketch, const Snap& second, std::optional<double> typedSize)
    {
        const auto shape = layout(second.point);
        if (!shape) {
            return false;
        }
        const std::size_t mark = sketch.entities().size();
        const auto lines = closedPolyline(sketch, shape->vertices);
        for (std::size_t k = 1; k < lines.size(); ++k) {
            sketch.addConstraint(Constraint::equal(lines[0], lines[k]));
        }
        // Regular = lados iguales con los vértices sobre un círculo (de
        // construcción). El circunscrito lleva además el círculo interior,
        // tangente a los lados, que es el que se acota.
        const double half = std::numbers::pi / sides_;
        const double circumradius =
            mode_ == PolygonMode::Circumscribed ? shape->circleRadius / std::cos(half) : shape->circleRadius;
        const EntityId around = sketch.addCircle(shape->center, circumradius, true);
        for (EntityId line : lines) {
            sketch.addConstraint(Constraint::pointOnObject(Ref::start(line), around));
        }
        EntityId circle = around;
        if (mode_ == PolygonMode::Circumscribed) {
            circle = sketch.addCircle(shape->center, shape->circleRadius, true);
            sketch.addConstraint(Constraint::concentric(around, circle));
            sketch.addConstraint(Constraint::tangent(lines[0], circle));
        }
        if (mode_ == PolygonMode::Edge) {
            attach(sketch, Ref::start(lines[0]), *first_);
            attach(sketch, Ref::end(lines[0]), second);
            if (typedSize) {
                sketch.addConstraint(Constraint::length(lines[0], *typedSize));
            }
        } else {
            attach(sketch, Ref::center(circle), *first_);
            if (mode_ == PolygonMode::Inscribed) {
                attach(sketch, Ref::start(lines[0]), second);
            }
            if (typedSize) {
                sketch.addConstraint(Constraint::diameter(circle, *typedSize));
            }
        }
        open_ = OpenFigure{entitiesSince(sketch, mark), *first_, second, {}, 0.0, {typedSize}};
        first_.reset();
        return true;
    }

    PolygonMode mode_;
    int sides_ = 6;
    Snap cursor_;
    std::optional<Snap> first_;
    std::optional<OpenFigure> open_;
    bool amended_ = false;

public:
    bool amendedLast() const override { return amended_; }
};

// --- Ranura ---------------------------------------------------------------------------------

class SlotTool final : public SketchTool {
public:
    explicit SlotTool(SlotMode mode) : mode_(mode) {}

    void move(const Snap& cursor) override { cursor_ = cursor; }

    bool click(Sketch& sketch, const Snap& cursor) override
    {
        open_.reset();  // un clic nuevo da por buena la figura abierta
        if (!p1_) {
            p1_ = cursor;
            return false;
        }
        if (!p2_) {
            Snap p2 = cursor;
            straight_ = straighten(p1_->point, p2);
            if (distance(p1_->point, p2.point) >= kMinLength) {
                p2_ = p2;
            }
            return false;
        }
        return create(sketch, radiusAt(cursor_.point), std::nullopt);
    }

    std::vector<std::string> valueFields() const override
    {
        return p2_ || open_ ? std::vector<std::string>{"Ancho"} : std::vector<std::string>{};
    }

    std::vector<double> currentValues() const override
    {
        if (open_) {
            return {2.0 * open_->value};
        }
        return p2_ ? std::vector<double>{2.0 * radiusAt(cursor_.point)} : std::vector<double>{};
    }

    bool applyValues(Sketch& sketch, const std::vector<std::optional<double>>& values) override
    {
        amended_ = false;
        if (open_) {
            // Ranura abierta: se rehace con el ancho escrito.
            if (values.empty() || !values[0]) {
                open_.reset();
                return false;
            }
            const OpenFigure old = *open_;
            if (!(*values[0] > kMinLength)) {
                return false;
            }
            p1_ = old.first;
            p2_ = old.second;
            if (!layout(*values[0] / 2.0) || !discardEntities(sketch, old.ids)) {
                const bool valid = layout(*values[0] / 2.0).has_value();
                p1_.reset();
                p2_.reset();
                if (valid) {
                    open_.reset();  // ya no existe (se deshizo)
                }
                return false;
            }
            amended_ = create(sketch, *values[0] / 2.0, values[0]);
            return amended_;
        }
        if (!p2_ || values.empty() || !values[0] || !(*values[0] > kMinLength)) {
            return false;
        }
        return create(sketch, *values[0] / 2.0, values[0]);
    }

    bool cancel() override
    {
        if (open_) {
            open_.reset();
            return true;
        }
        const bool active = p1_.has_value();
        p1_.reset();
        p2_.reset();
        return active;
    }

    Preview preview() const override
    {
        if (!p1_) {
            return {};
        }
        if (!p2_) {
            Snap p2 = cursor_;
            straighten(p1_->point, p2);
            return {{Line{p1_->point, p2.point}}};
        }
        const auto shape = layout(radiusAt(cursor_.point));
        if (!shape) {
            return {};
        }
        return {{shape->top, shape->bottom, shape->arcAtEnd, shape->arcAtStart}};
    }

    std::string hint() const override
    {
        if (open_) {
            return "Ranura creada: escribe el ancho y Enter para cambiarlo · clic o Esc para terminar";
        }
        if (!p1_) {
            switch (mode_) {
            case SlotMode::CenterToCenter: return "Clic para el centro de un extremo";
            case SlotMode::Overall: return "Clic para un extremo de la ranura";
            case SlotMode::CenterPoint: return "Clic para el centro de la ranura";
            }
        }
        if (!p2_) {
            return mode_ == SlotMode::Overall ? "Clic para el otro extremo" : "Clic para el centro del otro extremo";
        }
        return "Clic para el ancho (o escríbelo)";
    }

    // (la indicación de la ranura abierta va en hint(), arriba)

private:
    struct Shape {
        Vec2 c1, c2;
        Line top, bottom;
        Arc arcAtEnd, arcAtStart;  // en c2 y en c1
    };

    // Centros de los extremos según el modo (dependen del radio si se dio
    // la longitud total).
    std::pair<Vec2, Vec2> centers(double radius) const
    {
        const Vec2 a = p1_->point;
        const Vec2 b = p2_->point;
        switch (mode_) {
        case SlotMode::CenterToCenter: return {a, b};
        case SlotMode::CenterPoint: return {sub(scale(2.0, a), b), b};
        case SlotMode::Overall: {
            const Vec2 u = unit(sub(b, a));
            return {add(a, scale(radius, u)), sub(b, scale(radius, u))};
        }
        }
        return {a, b};
    }

    double radiusAt(Vec2 p) const
    {
        const Vec2 a = p1_->point;
        const Vec2 u = unit(sub(p2_->point, a));
        return std::abs(dot(sub(p, a), leftNormal(u)));
    }

    std::optional<Shape> layout(double r) const
    {
        if (r < kMinLength) {
            return std::nullopt;
        }
        const auto [c1, c2] = centers(r);
        if (distance(c1, c2) < kMinLength || dot(sub(c2, c1), sub(p2_->point, p1_->point)) <= 0.0) {
            return std::nullopt;
        }
        const Vec2 u = unit(sub(c2, c1));
        const Vec2 n = leftNormal(u);
        const double theta = std::atan2(u.y, u.x);
        Shape s;
        s.c1 = c1;
        s.c2 = c2;
        s.top = {add(c1, scale(r, n)), add(c2, scale(r, n))};
        s.bottom = {sub(c2, scale(r, n)), sub(c1, scale(r, n))};
        s.arcAtEnd = {c2, r, theta - std::numbers::pi / 2.0, theta + std::numbers::pi / 2.0};
        s.arcAtStart = {c1, r, theta + std::numbers::pi / 2.0, theta + 3.0 * std::numbers::pi / 2.0};
        return s;
    }

    bool create(Sketch& sketch, double r, std::optional<double> width)
    {
        const auto s = layout(r);
        if (!s) {
            return false;
        }
        const std::size_t mark = sketch.entities().size();
        const EntityId top = sketch.addLine(s->top.start, s->top.end);
        const EntityId bottom = sketch.addLine(s->bottom.start, s->bottom.end);
        const EntityId endArc = sketch.addArc(s->arcAtEnd.center, r, s->arcAtEnd.startAngle, s->arcAtEnd.endAngle);
        const EntityId startArc = sketch.addArc(s->arcAtStart.center, r, s->arcAtStart.startAngle, s->arcAtStart.endAngle);
        // Lados tangentes a los arcos en sus uniones, y los dos arcos iguales.
        sketch.addConstraint(Constraint::tangentAt(Ref::end(top), Ref::end(endArc)));
        sketch.addConstraint(Constraint::tangentAt(Ref::start(bottom), Ref::start(endArc)));
        sketch.addConstraint(Constraint::tangentAt(Ref::end(bottom), Ref::end(startArc)));
        sketch.addConstraint(Constraint::tangentAt(Ref::start(top), Ref::start(startArc)));
        sketch.addConstraint(Constraint::equal(endArc, startArc));
        // Eje de construcción entre los centros, para acotar y apoyarse.
        const EntityId axis = sketch.addLine(s->c1, s->c2, true);
        sketch.addConstraint(Constraint::coincident(Ref::start(axis), Ref::center(startArc)));
        sketch.addConstraint(Constraint::coincident(Ref::end(axis), Ref::center(endArc)));
        if (straight_ == Straight::Horizontal) {
            sketch.addConstraint(Constraint::horizontal(axis));
        } else if (straight_ == Straight::Vertical) {
            sketch.addConstraint(Constraint::vertical(axis));
        }
        switch (mode_) {
        case SlotMode::CenterToCenter:
            attach(sketch, Ref::start(axis), *p1_);
            attach(sketch, Ref::end(axis), *p2_);
            break;
        case SlotMode::CenterPoint:
            if (p1_->kind == SnapKind::Point || p1_->kind == SnapKind::Center) {
                sketch.addConstraint(Constraint::midpoint(p1_->ref, axis));
            }
            attach(sketch, Ref::end(axis), *p2_);
            break;
        case SlotMode::Overall:
            break;
        }
        if (width) {
            sketch.addConstraint(Constraint::diameter(endArc, *width));
        }
        open_ = OpenFigure{entitiesSince(sketch, mark), *p1_, *p2_, {}, r, {width}};
        p1_.reset();
        p2_.reset();
        return true;
    }

    SlotMode mode_;
    Snap cursor_;
    std::optional<Snap> p1_;
    std::optional<Snap> p2_;
    Straight straight_ = Straight::None;
    std::optional<OpenFigure> open_;
    bool amended_ = false;

public:
    bool amendedLast() const override { return amended_; }
};

// --- Punto ----------------------------------------------------------------------------------------------------
// Un clic deja un punto suelto (para marcar dónde van los agujeros, centros, referencias…). Sigue activa para poner
// más; Esc sale. Un punto sobre algo existente queda unido a ello (coincidente, punto medio o sobre la curva).

class PointTool final : public SketchTool {
public:
    void move(const Snap& cursor) override { cursor_ = cursor; }

    bool click(Sketch& sketch, const Snap& cursor) override
    {
        // Con el cursor en un punto que ya existe no se pone otro encima.
        if ((cursor.kind == SnapKind::Point || cursor.kind == SnapKind::Center) && cursor.alignX.empty()
            && cursor.alignY.empty()) {
            return false;
        }
        const EntityId id = sketch.addPoint(cursor.point);
        attach(sketch, Ref::of(id), cursor);
        return true;
    }

    // Terminar no tiene nada a medias: la interfaz sale de la herramienta.
    bool cancel() override { return false; }

    Preview preview() const override { return {}; }

    std::string hint() const override
    {
        return "Clic para poner un punto · Esc para terminar";
    }

private:
    Snap cursor_;
};

} // namespace

std::unique_ptr<SketchTool> makePointTool() { return std::make_unique<PointTool>(); }
std::unique_ptr<SketchTool> makeThreePointRectangleTool() { return std::make_unique<ThreePointRectangleTool>(); }
std::unique_ptr<SketchTool> makeCenterRectangleTool() { return std::make_unique<CenterRectangleTool>(); }
std::unique_ptr<SketchTool> makePolygonTool(PolygonMode mode) { return std::make_unique<PolygonTool>(mode); }
std::unique_ptr<SketchTool> makeSlotTool(SlotMode mode) { return std::make_unique<SlotTool>(mode); }

} // namespace nuf::sketch
