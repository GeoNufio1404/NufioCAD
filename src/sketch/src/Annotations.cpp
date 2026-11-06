#include <nuf/sketch/Annotations.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <numbers>
#include <optional>

namespace nuf::sketch {

namespace {

// Medidas en píxeles de pantalla.
constexpr double kGlyphOffsetPx = 14.0;   // separación del icono a su entidad
constexpr double kGlyphSpacingPx = 18.0;  // entre iconos de la misma entidad
constexpr double kDimensionOffsetPx = 28.0;
constexpr double kExtensionPx = 5.0;      // las líneas de referencia sobrepasan la de cota
constexpr double kArrowPx = 9.0;
constexpr double kTextGapPx = 11.0;

Vec2 operator+(Vec2 a, Vec2 b) { return {a.x + b.x, a.y + b.y}; }
Vec2 operator-(Vec2 a, Vec2 b) { return {a.x - b.x, a.y - b.y}; }
Vec2 operator*(double k, Vec2 v) { return {k * v.x, k * v.y}; }
double length(Vec2 v) { return std::hypot(v.x, v.y); }
Vec2 unit(Vec2 v)
{
    const double len = length(v);
    return len > 0.0 ? (1.0 / len) * v : Vec2{1.0, 0.0};
}
Vec2 normalOf(Vec2 u) { return {-u.y, u.x}; }
Vec2 midpoint(Vec2 a, Vec2 b) { return 0.5 * (a + b); }

double gLengthScale = 1.0;
int gLengthDecimals = 3;

std::string trimmed(double value, int decimals)
{
    char buffer[64];
    std::snprintf(buffer, sizeof buffer, "%.*f", decimals, value);
    std::string text(buffer);
    if (text.find('.') != std::string::npos) {
        while (text.back() == '0') {
            text.pop_back();
        }
        if (text.back() == '.') {
            text.pop_back();
        }
    }
    return text == "-0" ? "0" : text;
}

// Punta de flecha en «tip» apuntando en la dirección «dir».
void addArrow(std::vector<Geometry>& lines, Vec2 tip, Vec2 dir, double size)
{
    const Vec2 back = -1.0 * unit(dir);
    const double c = std::cos(0.35);
    const double s = std::sin(0.35);
    const Vec2 left{back.x * c - back.y * s, back.x * s + back.y * c};
    const Vec2 right{back.x * c + back.y * s, -back.x * s + back.y * c};
    lines.push_back(Line{tip, tip + size * left});
    lines.push_back(Line{tip, tip + size * right});
}

// Cota alineada entre a y b, desplazada hacia «side».
Annotation alignedDimension(Vec2 a, Vec2 b, Vec2 side, double px)
{
    Annotation note;
    const Vec2 offset = kDimensionOffsetPx * px * side;
    const Vec2 extension = kExtensionPx * px * unit(side);
    const Vec2 a2 = a + offset;
    const Vec2 b2 = b + offset;
    note.lines.push_back(Line{a, a2 + extension});
    note.lines.push_back(Line{b, b2 + extension});
    note.lines.push_back(Line{a2, b2});
    addArrow(note.lines, a2, a2 - b2, kArrowPx * px);
    addArrow(note.lines, b2, b2 - a2, kArrowPx * px);
    note.anchor = midpoint(a2, b2) + kTextGapPx * px * unit(side);
    return note;
}

struct Subject {
    Vec2 base;       // dónde se apoya el icono
    Vec2 offsetDir;  // hacia dónde se separa
    Vec2 stackDir;   // hacia dónde se apilan varios iconos
};

std::optional<Subject> subjectOf(const Sketch& sketch, Ref ref, double px)
{
    if (const auto p = sketch.point(ref)) {
        return Subject{*p, unit({1.0, 1.0}), {1.0, 0.0}};
    }
    const Entity* e = sketch.entity(ref.entity);
    if (!e) {
        return std::nullopt;
    }
    if (const auto* l = std::get_if<Line>(&e->geometry)) {
        const Vec2 u = unit(l->end - l->start);
        Vec2 n = normalOf(u);
        if (n.y < 0.0 || (n.y == 0.0 && n.x < 0.0)) {
            n = -1.0 * n;  // siempre hacia arriba (o a la izquierda): más legible
        }
        if (e->reference) {
            // Ejes: un poco más allá del origen, no en el «punto medio».
            return Subject{l->start + 4.0 * kGlyphSpacingPx * px * u, n, u};
        }
        return Subject{midpoint(l->start, l->end), n, u};
    }
    if (const auto* c = std::get_if<Circle>(&e->geometry)) {
        return Subject{c->center + Vec2{0.0, c->radius}, {0.0, 1.0}, {1.0, 0.0}};
    }
    if (const auto* a = std::get_if<Arc>(&e->geometry)) {
        double end = a->endAngle;
        while (end <= a->startAngle) {
            end += 2.0 * std::numbers::pi;
        }
        const double mid = 0.5 * (a->startAngle + end);
        const Vec2 radial{std::cos(mid), std::sin(mid)};
        return Subject{a->center + a->radius * radial, radial, normalOf(radial)};
    }
    // Elipses y splines: en el punto central de su polilínea.
    const std::vector<Vec2> points = polyline(e->geometry);
    if (points.size() >= 2) {
        const std::size_t i = points.size() / 2;
        const Vec2 u = unit(points[i] - points[i - 1]);
        Vec2 n = normalOf(u);
        if (n.y < 0.0) {
            n = -1.0 * n;
        }
        return Subject{points[i], n, u};
    }
    return std::nullopt;
}

// Solo caracteres que tienen todas las fuentes: OCCT no busca en otras
// fuentes como Qt (hasta que haya iconos propios).
const char* glyph(ConstraintType type)
{
    switch (type) {
    case ConstraintType::Horizontal: return "H";
    case ConstraintType::Vertical: return "V";
    case ConstraintType::Parallel: return "//";
    case ConstraintType::Perpendicular: return "L";
    case ConstraintType::Tangent: return "T";
    case ConstraintType::Equal: return "=";
    case ConstraintType::Collinear: return "CL";
    case ConstraintType::Concentric: return "CC";
    case ConstraintType::Midpoint: return "M";
    case ConstraintType::Symmetric: return "S";
    case ConstraintType::Fix: return "F";
    case ConstraintType::Curvature: return "G2";
    case ConstraintType::PointOnObject: return "•";
    default: return "";
    }
}

// Entidades junto a las que se dibuja el icono de una restricción geométrica.
std::vector<Ref> subjects(const Constraint& c)
{
    switch (c.type) {
    case ConstraintType::Coincident:
        return {};
    case ConstraintType::Horizontal:
    case ConstraintType::Vertical:
    case ConstraintType::Fix:
    case ConstraintType::PointOnObject:
    case ConstraintType::Midpoint:
        return {c.first};
    case ConstraintType::Equal:
        // La primera entidad suele ser la misma en una cadena (polígono): el
        // icono va en la otra, así cada lado lleva uno.
        return {c.second};
    case ConstraintType::Curvature:
        return {c.first};
    case ConstraintType::Tangent:
        // En un extremo, un solo icono junto al punto de unión.
        return c.first.part == Part::Whole ? std::vector<Ref>{c.first, c.second} : std::vector<Ref>{c.first};
    default:
        return {c.first, c.second};
    }
}

std::optional<Annotation> dimensionAnnotation(const Sketch& sketch, const Constraint& c, double px)
{
    const auto lineOf = [&](Ref r) -> const Line* {
        const Entity* e = sketch.entity(r.entity);
        return e ? std::get_if<Line>(&e->geometry) : nullptr;
    };

    std::optional<Annotation> note;
    switch (c.type) {
    case ConstraintType::Distance: {
        if (c.second.empty()) {
            const Line* l = lineOf(c.first);
            if (!l) return std::nullopt;
            const Vec2 n = normalOf(unit(l->end - l->start));
            note = alignedDimension(l->start, l->end, n, px);
        } else if (const Line* l = (c.second.part == Part::Whole) ? lineOf(c.second) : nullptr) {
            // Distancia de un punto a una recta: perpendicular desde el punto.
            const auto p = sketch.point(c.first);
            if (!p) return std::nullopt;
            const Vec2 u = unit(l->end - l->start);
            const Vec2 d = *p - l->start;
            const Vec2 foot = l->start + (d.x * u.x + d.y * u.y) * u;
            note = Annotation{};
            note->lines.push_back(Line{*p, foot});
            addArrow(note->lines, *p, *p - foot, kArrowPx * px);
            addArrow(note->lines, foot, foot - *p, kArrowPx * px);
            note->anchor = midpoint(*p, foot) + kTextGapPx * px * u;
        } else {
            const auto a = sketch.point(c.first);
            const auto b = sketch.point(c.second);
            if (!a || !b) return std::nullopt;
            note = alignedDimension(*a, *b, normalOf(unit(*b - *a)), px);
        }
        break;
    }
    case ConstraintType::DistanceX:
    case ConstraintType::DistanceY: {
        Vec2 a, b;
        if (c.second.empty()) {
            const Line* l = lineOf(c.first);
            if (!l) return std::nullopt;
            a = l->start;
            b = l->end;
        } else {
            const auto pa = sketch.point(c.first);
            const auto pb = sketch.point(c.second);
            if (!pa || !pb) return std::nullopt;
            a = *pa;
            b = *pb;
        }
        // Se proyectan a una línea horizontal (o vertical) por encima (o a la derecha).
        const bool x = c.type == ConstraintType::DistanceX;
        const double level = x ? std::max(a.y, b.y) : std::max(a.x, b.x);
        const Vec2 a1 = x ? Vec2{a.x, level} : Vec2{level, a.y};
        const Vec2 b1 = x ? Vec2{b.x, level} : Vec2{level, b.y};
        note = alignedDimension(a1, b1, x ? Vec2{0.0, 1.0} : Vec2{1.0, 0.0}, px);
        note->lines[0] = Line{a, std::get<Line>(note->lines[0]).end};
        note->lines[1] = Line{b, std::get<Line>(note->lines[1]).end};
        break;
    }
    case ConstraintType::Angle: {
        const Line* l1 = lineOf(c.first);
        const Line* l2 = lineOf(c.second);
        if (!l1 || !l2) return std::nullopt;
        const Vec2 d1 = l1->end - l1->start;
        const Vec2 d2 = l2->end - l2->start;
        const double denom = d1.x * d2.y - d1.y * d2.x;
        note = Annotation{};
        if (std::abs(denom) < 1e-12) {
            note->anchor = midpoint(l1->start, l1->end);
            break;
        }
        // Vértice: intersección de las dos rectas.
        const Vec2 w = l2->start - l1->start;
        const double t = (w.x * d2.y - w.y * d2.x) / denom;
        const Vec2 vertex = l1->start + t * d1;
        const double from = std::atan2(d1.y, d1.x);
        const double sweep = std::atan2(denom, d1.x * d2.x + d1.y * d2.y);
        const double radius = 3.0 * kDimensionOffsetPx * px;
        const double start = sweep >= 0.0 ? from : from + sweep;
        note->lines.push_back(Arc{vertex, radius, start, start + std::abs(sweep)});
        const double mid = from + 0.5 * sweep;
        note->anchor = vertex + (radius + kTextGapPx * px) * Vec2{std::cos(mid), std::sin(mid)};
        break;
    }
    case ConstraintType::MajorRadius:
    case ConstraintType::MinorRadius: {
        const Entity* e = sketch.entity(c.first.entity);
        const Ellipse* el = nullptr;
        if (e) {
            if (const auto* full = std::get_if<Ellipse>(&e->geometry)) el = full;
            if (const auto* arc = std::get_if<EllipseArc>(&e->geometry)) el = &arc->ellipse;
        }
        if (!el) return std::nullopt;
        // Del centro al vértice del eje (mayor o menor).
        const bool major = c.type == ConstraintType::MajorRadius;
        const Vec2 tip = el->pointAt(major ? 0.0 : std::numbers::pi / 2.0);
        const Vec2 u = unit(tip - el->center);
        note = Annotation{};
        note->lines.push_back(Line{el->center, tip});
        addArrow(note->lines, tip, u, kArrowPx * px);
        note->anchor = tip + 2.0 * kTextGapPx * px * u;
        break;
    }
    case ConstraintType::Radius:
    case ConstraintType::Diameter: {
        const Entity* e = sketch.entity(c.first.entity);
        if (!e) return std::nullopt;
        Vec2 center;
        double radius = 0.0;
        double angle = std::numbers::pi / 4.0;
        if (const auto* circle = std::get_if<Circle>(&e->geometry)) {
            center = circle->center;
            radius = circle->radius;
        } else if (const auto* arc = std::get_if<Arc>(&e->geometry)) {
            center = arc->center;
            radius = arc->radius;
            double end = arc->endAngle;
            while (end <= arc->startAngle) {
                end += 2.0 * std::numbers::pi;
            }
            angle = 0.5 * (arc->startAngle + end);
        } else {
            return std::nullopt;
        }
        const Vec2 u{std::cos(angle), std::sin(angle)};
        const Vec2 rim = center + radius * u;
        note = Annotation{};
        if (c.type == ConstraintType::Diameter) {
            const Vec2 opposite = center - radius * u;
            note->lines.push_back(Line{opposite, rim});
            addArrow(note->lines, opposite, opposite - rim, kArrowPx * px);
        } else {
            note->lines.push_back(Line{center, rim});
        }
        addArrow(note->lines, rim, u, kArrowPx * px);
        note->anchor = rim + 2.0 * kTextGapPx * px * u;
        break;
    }
    default:
        return std::nullopt;
    }
    note->dimension = true;
    note->reference = !c.driving;
    note->text = formatDimension(c);
    note->constraint = c.id;
    return note;
}

} // namespace

void setDimensionLengthScale(double mmPerUnit, int decimals)
{
    gLengthScale = mmPerUnit > 0.0 ? mmPerUnit : 1.0;
    gLengthDecimals = decimals;
}

std::string formatDimension(const Constraint& c)
{
    std::string text;
    switch (c.type) {
    case ConstraintType::Angle:
        text = trimmed(std::abs(c.value) * 180.0 / std::numbers::pi, 2) + "°";
        break;
    case ConstraintType::Radius:
    case ConstraintType::MajorRadius:
    case ConstraintType::MinorRadius:
        text = "R" + trimmed(c.value / gLengthScale, gLengthDecimals);
        break;
    case ConstraintType::Diameter:
        text = "Ø" + trimmed(c.value / gLengthScale, gLengthDecimals);
        break;
    case ConstraintType::DistanceX:
    case ConstraintType::DistanceY:
        text = trimmed(std::abs(c.value) / gLengthScale, gLengthDecimals);
        break;
    default:
        text = trimmed(c.value / gLengthScale, gLengthDecimals);
        break;
    }
    return c.driving ? text : "(" + text + ")";
}

std::vector<Annotation> layoutAnnotations(const Sketch& sketch, double pixelSize)
{
    std::vector<Annotation> result;
    // Cuántos iconos lleva ya cada punto o entidad, para apilarlos.
    std::map<std::pair<EntityId, Part>, int> stacked;

    for (const Constraint& c : sketch.constraints()) {
        if (isDimensional(c.type)) {
            if (auto note = dimensionAnnotation(sketch, c, pixelSize)) {
                result.push_back(std::move(*note));
            }
            continue;
        }
        const char* text = glyph(c.type);
        for (const Ref& r : subjects(c)) {
            const auto subject = subjectOf(sketch, r, pixelSize);
            if (!subject || *text == '\0') {
                continue;
            }
            int& index = stacked[{r.entity, r.part}];
            Annotation note;
            note.constraint = c.id;
            note.text = text;
            note.anchor = subject->base + kGlyphOffsetPx * pixelSize * subject->offsetDir
                          + (index * kGlyphSpacingPx * pixelSize) * subject->stackDir;
            ++index;
            result.push_back(std::move(note));
        }
    }
    return result;
}

ConstraintId pickAnnotation(const std::vector<Annotation>& annotations, Vec2 cursor, double pixelSize)
{
    ConstraintId best = 0;
    double bestDistance = 0.0;
    for (const Annotation& a : annotations) {
        // Radio aproximado del texto (unos 4 px por carácter, mínimo 9 px).
        const double radius = std::max(9.0, 4.0 * double(a.text.size())) * pixelSize;
        const double d = length(cursor - a.anchor);
        if (d <= radius && (best == 0 || d < bestDistance)) {
            best = a.constraint;
            bestDistance = d;
        }
    }
    return best;
}

} // namespace nuf::sketch
