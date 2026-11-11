#include <nuf/sketch/Annotations.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <numbers>
#include <optional>
#include <unordered_map>

namespace nuf::sketch {

namespace {

// Medidas en píxeles de pantalla.
constexpr double kGlyphOffsetPx = 14.0;   // separación del icono a su entidad
constexpr double kGlyphSpacingPx = 18.0;  // entre iconos de la misma entidad
constexpr double kDimensionOffsetPx = 28.0;
constexpr double kExtensionPx = 5.0;      // las líneas de referencia sobrepasan la de cota
constexpr double kArrowPx = 9.0;
constexpr double kTextGapPx = 11.0;
constexpr double kRowPx = 18.0;           // salto al apartar una anotación de otra
constexpr int kMaxLevels = 8;             // posiciones que se prueban por anotación
constexpr int kMaxRadialLevels = 12;      // las radiales dan la vuelta entera (12 × 30°)
constexpr double kLineSamplePx = 6.0;     // paso de las cajas con que se representa una línea de cota
constexpr double kCharWidthPx = 8.0;      // ancho aproximado de un carácter del texto
constexpr double kTextHeightPx = 18.0;

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

// Cota alineada entre a y b, desplazada hacia «side» (level: renglones más lejos).
Annotation alignedDimension(Vec2 a, Vec2 b, Vec2 side, double px, int level)
{
    Annotation note;
    const Vec2 offset = (kDimensionOffsetPx + level * kRowPx) * px * side;
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

// «level» aparta la cota de otras: 0 es su sitio natural; cada nivel la lleva
// un paso más allá según su grado de libertad (lineal: más lejos de la
// geometría; radial: otro ángulo; ángulo: arco mayor; el resto: el texto).
std::optional<Annotation> dimensionAnnotation(const Sketch& sketch, const Constraint& c, double px, int level)
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
            note = alignedDimension(l->start, l->end, n, px, level);
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
            note->anchor = midpoint(*p, foot) + (kTextGapPx + level * kRowPx) * px * u;
        } else {
            const auto a = sketch.point(c.first);
            const auto b = sketch.point(c.second);
            if (!a || !b) return std::nullopt;
            note = alignedDimension(*a, *b, normalOf(unit(*b - *a)), px, level);
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
        const double lineAt = x ? std::max(a.y, b.y) : std::max(a.x, b.x);
        const Vec2 a1 = x ? Vec2{a.x, lineAt} : Vec2{lineAt, a.y};
        const Vec2 b1 = x ? Vec2{b.x, lineAt} : Vec2{lineAt, b.y};
        note = alignedDimension(a1, b1, x ? Vec2{0.0, 1.0} : Vec2{1.0, 0.0}, px, level);
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
        // El ángulo de la restricción va de d1 a d2, pero la esquina que se ve
        // es la de los segmentos dibujados. Entre las esquinas posibles
        // (±d1, ±d2) con ese mismo valor (todas si es de 90°) se usa la que
        // cae sobre los segmentos: así el arco queda en el vértice, dentro de
        // la esquina, y no en el cuadrante opuesto ni en el complementario.
        const auto ray = [&](const Line& l) {
            const Vec2 tip = length(l.start - vertex) > length(l.end - vertex) ? l.start : l.end;
            return unit(tip - vertex);
        };
        const Vec2 r1 = ray(*l1);
        const Vec2 r2 = ray(*l2);
        const Vec2 u1 = unit(d1);
        const Vec2 u2 = unit(d2);
        const double value = std::abs(std::atan2(denom, d1.x * d2.x + d1.y * d2.y));
        double bestScore = -10.0;
        double from = std::atan2(u1.y, u1.x);
        double sweep = std::atan2(denom, d1.x * d2.x + d1.y * d2.y);
        for (const double sa : {1.0, -1.0}) {
            for (const double sb : {1.0, -1.0}) {
                const Vec2 ra = sa * u1;
                const Vec2 rb = sb * u2;
                const double corner = std::atan2(ra.x * rb.y - ra.y * rb.x, ra.x * rb.x + ra.y * rb.y);
                if (std::abs(std::abs(corner) - value) > 1e-6) {
                    continue;
                }
                const double score = ra.x * r1.x + ra.y * r1.y + rb.x * r2.x + rb.y * r2.y;
                if (score > bestScore + 1e-9) {
                    bestScore = score;
                    from = std::atan2(ra.y, ra.x);
                    sweep = corner;
                }
            }
        }
        const double radius = (3.0 * kDimensionOffsetPx + level * kRowPx) * px;
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
        note->anchor = tip + (2.0 * kTextGapPx + level * kRowPx) * px * u;
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
            // Varios círculos con el mismo centro: cada cota va a otro ángulo (30° más que la del anterior), para que
            // los textos y las líneas de cota no se pisen.
            int rank = 0;
            for (const Entity& other : sketch.entities()) {
                if (other.id == e->id) {
                    break;
                }
                if (const auto* concentric = std::get_if<Circle>(&other.geometry);
                    concentric != nullptr && std::hypot(concentric->center.x - center.x, concentric->center.y - center.y) < 1e-6) {
                    ++rank;
                }
            }
            angle += double(rank) * std::numbers::pi / 6.0;
            angle += double(level) * std::numbers::pi / 6.0;
        } else if (const auto* arc = std::get_if<Arc>(&e->geometry)) {
            center = arc->center;
            radius = arc->radius;
            double end = arc->endAngle;
            while (end <= arc->startAngle) {
                end += 2.0 * std::numbers::pi;
            }
            angle = 0.5 * (arc->startAngle + end) + double(level) * std::numbers::pi / 6.0;
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

namespace {

// Caja que ocupa el texto de una anotación (centrada en su ancla), en mm.
struct Box {
    double x0, y0, x1, y1;
};

Box textBox(const Annotation& note, double px)
{
    std::size_t chars = 0;
    for (const char ch : note.text) {
        chars += (static_cast<unsigned char>(ch) & 0xC0) != 0x80 ? 1 : 0;  // sin bytes de continuación UTF-8
    }
    const double halfWidth = 0.5 * (double(chars) * kCharWidthPx + 8.0) * px;
    const double halfHeight = 0.5 * kTextHeightPx * px;
    return {note.anchor.x - halfWidth, note.anchor.y - halfHeight, note.anchor.x + halfWidth, note.anchor.y + halfHeight};
}

// Las cajas ya colocadas, en una rejilla de celdas: comprobar una nueva solo
// mira las celdas que toca, no todas las anteriores.
class Placed {
public:
    explicit Placed(double cell)
        : cell_(cell)
    {}

    double overlap(const Box& box) const
    {
        std::vector<int> seen;
        double total = 0.0;
        forCells(box, [&](long long key) {
            const auto it = cells_.find(key);
            if (it == cells_.end()) {
                return;
            }
            for (const int index : it->second) {
                if (std::ranges::find(seen, index) != seen.end()) {
                    continue;
                }
                seen.push_back(index);
                const Box& other = boxes_[std::size_t(index)];
                const double w = std::min(box.x1, other.x1) - std::max(box.x0, other.x0);
                const double h = std::min(box.y1, other.y1) - std::max(box.y0, other.y0);
                if (w > 0.0 && h > 0.0) {
                    total += w * h;
                }
            }
        });
        return total;
    }

    void add(const Box& box)
    {
        const int index = int(boxes_.size());
        boxes_.push_back(box);
        forCells(box, [&](long long key) { cells_[key].push_back(index); });
    }

private:
    template <class F>
    void forCells(const Box& box, F&& visit) const
    {
        const long long x0 = (long long)std::floor(box.x0 / cell_);
        const long long x1 = (long long)std::floor(box.x1 / cell_);
        const long long y0 = (long long)std::floor(box.y0 / cell_);
        const long long y1 = (long long)std::floor(box.y1 / cell_);
        for (long long x = x0; x <= x1; ++x) {
            for (long long y = y0; y <= y1; ++y) {
                visit(x * 1000003LL + y);
            }
        }
    }

    double cell_;
    std::vector<Box> boxes_;
    std::unordered_map<long long, std::vector<int>> cells_;
};

// Cajas pequeñas a lo largo de las líneas de una cota (la de radio o diámetro
// cruza el dibujo y no debe pasar por encima de otros textos ni de otras cotas
// radiales).
std::vector<Box> lineBoxes(const Annotation& note, double px)
{
    std::vector<Box> boxes;
    const double half = 0.5 * kLineSamplePx * px;
    for (const Geometry& g : note.lines) {
        const auto* line = std::get_if<Line>(&g);
        if (line == nullptr) {
            continue;
        }
        const double len = std::hypot(line->end.x - line->start.x, line->end.y - line->start.y);
        const int steps = std::clamp(int(std::ceil(len / (kLineSamplePx * px))), 1, 64);
        for (int i = 0; i <= steps; ++i) {
            const double t = double(i) / double(steps);
            const double x = line->start.x + t * (line->end.x - line->start.x);
            const double y = line->start.y + t * (line->end.y - line->start.y);
            boxes.push_back({x - half, y - half, x + half, y + half});
        }
    }
    return boxes;
}

// Cajas pequeñas a lo largo de la geometría del sketch: los textos procuran no
// taparla (pesa menos que chocar con otro texto).
void addGeometryBoxes(Placed& geometry, const Sketch& sketch, double px)
{
    const double half = 0.5 * kLineSamplePx * px;
    for (const Entity& e : sketch.entities()) {
        if (e.reference) {
            continue;  // los ejes son infinitos y ya se ven
        }
        const std::vector<Vec2> points = polyline(e.geometry);
        if (points.size() == 1) {
            geometry.add({points[0].x - half, points[0].y - half, points[0].x + half, points[0].y + half});
            continue;
        }
        for (std::size_t i = 1; i < points.size(); ++i) {
            const double len = std::hypot(points[i].x - points[i - 1].x, points[i].y - points[i - 1].y);
            const int steps = std::clamp(int(std::ceil(len / (kLineSamplePx * px))), 1, 64);
            for (int k = 0; k <= steps; ++k) {
                const double t = double(k) / double(steps);
                const double x = points[i - 1].x + t * (points[i].x - points[i - 1].x);
                const double y = points[i - 1].y + t * (points[i].y - points[i - 1].y);
                geometry.add({x - half, y - half, x + half, y + half});
            }
        }
    }
}

constexpr double kGeometryWeight = 0.25;  // un texto sobre el dibujo molesta menos que sobre otro texto

// Prueba las posiciones de una anotación (make(nivel)) hasta dar con una que no
// pise a ninguna ya colocada; si ninguna queda libre, la menos solapada.
// «radial»: también cuentan las líneas de la cota (radio, diámetro).
template <class Make>
std::optional<Annotation> placeAnnotation(Placed& placed, const Placed& geometry, double px, Make&& make,
                                          bool radial = false)
{
    std::optional<Annotation> best;
    std::vector<Box> bestLines;
    double bestOverlap = 0.0;
    const int levels = radial ? kMaxRadialLevels : kMaxLevels;
    for (int level = 0; level < levels; ++level) {
        std::optional<Annotation> note = make(level);
        if (!note) {
            return std::nullopt;
        }
        const Box text = textBox(*note, px);
        double overlap = placed.overlap(text) + kGeometryWeight * geometry.overlap(text);
        std::vector<Box> lines;
        if (radial) {
            lines = lineBoxes(*note, px);
            for (const Box& box : lines) {
                overlap += placed.overlap(box);
            }
        }
        if (!best || overlap < bestOverlap) {
            bestOverlap = overlap;
            best = std::move(note);
            bestLines = std::move(lines);
        }
        if (bestOverlap <= 0.0) {
            break;
        }
    }
    placed.add(textBox(*best, px));
    for (const Box& box : bestLines) {
        placed.add(box);
    }
    return best;
}

} // namespace

std::vector<Annotation> layoutAnnotations(const Sketch& sketch, double pixelSize)
{
    // El resultado sale en el orden de las restricciones, aunque se coloquen
    // antes las cotas que los iconos.
    std::vector<std::pair<std::size_t, Annotation>> ordered;
    // Cuántos iconos lleva ya cada punto o entidad, para apilarlos.
    std::map<std::pair<EntityId, Part>, int> stacked;
    Placed placed(64.0 * pixelSize);
    Placed geometry(64.0 * pixelSize);
    addGeometryBoxes(geometry, sketch, pixelSize);

    // Primero las cotas (lo que más importa leer), luego los iconos: un icono
    // se aparta de una cota, no al revés.
    std::size_t order = 0;
    for (const Constraint& c : sketch.constraints()) {
        order += 8;
        if (!isDimensional(c.type)) {
            continue;
        }
        const bool radial = c.type == ConstraintType::Radius || c.type == ConstraintType::Diameter;
        if (auto note = placeAnnotation(
                placed, geometry, pixelSize, [&](int level) { return dimensionAnnotation(sketch, c, pixelSize, level); },
                radial)) {
            ordered.emplace_back(order, std::move(*note));
        }
    }
    order = 0;
    for (const Constraint& c : sketch.constraints()) {
        order += 8;
        if (isDimensional(c.type)) {
            continue;
        }
        const char* text = glyph(c.type);
        std::size_t slot = 0;
        for (const Ref& r : subjects(c)) {
            const auto subject = subjectOf(sketch, r, pixelSize);
            if (!subject || *text == '\0') {
                continue;
            }
            const int index = stacked[{r.entity, r.part}]++;
            if (auto note = placeAnnotation(placed, geometry, pixelSize, [&](int level) -> std::optional<Annotation> {
                    Annotation icon;
                    icon.constraint = c.id;
                    icon.text = text;
                    icon.anchor = subject->base + kGlyphOffsetPx * pixelSize * subject->offsetDir
                                  + ((index + level) * kGlyphSpacingPx * pixelSize) * subject->stackDir;
                    return icon;
                })) {
                ordered.emplace_back(order + (++slot), std::move(*note));
            }
        }
    }
    std::ranges::stable_sort(ordered, [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<Annotation> result;
    result.reserve(ordered.size());
    for (auto& entry : ordered) {
        result.push_back(std::move(entry.second));
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
