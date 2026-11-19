#include <nuf/sketch/Svg.hpp>

#include <nuf/sketch/Tools.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <map>
#include <iterator>
#include <numbers>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace nuf::sketch {

namespace {

constexpr double kPi = std::numbers::pi;

// Matriz 2×3: x' = a·x + c·y + e, y' = b·x + d·y + f.
struct Matrix {
    double a = 1, b = 0, c = 0, d = 1, e = 0, f = 0;

    Vec2 apply(Vec2 p) const { return {a * p.x + c * p.y + e, b * p.x + d * p.y + f}; }
    // this ∘ other: primero other y después this.
    Matrix operator*(const Matrix& o) const
    {
        return {a * o.a + c * o.b, b * o.a + d * o.b, a * o.c + c * o.d, b * o.c + d * o.d, a * o.e + c * o.f + e, b * o.e + d * o.f + f};
    }
    double determinant() const { return a * d - b * c; }
};

std::string_view trimView(std::string_view s)
{
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
        s.remove_prefix(1);
    }
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
        s.remove_suffix(1);
    }
    return s;
}

// Lector de números de una cadena (separados por espacios o comas, con signo y exponente; «1.5.5» son dos números).
class Numbers {
public:
    explicit Numbers(std::string_view text)
        : text_(text)
    {
    }

    void skipSeparators()
    {
        while (pos_ < text_.size() && (std::isspace(static_cast<unsigned char>(text_[pos_])) || text_[pos_] == ',')) {
            ++pos_;
        }
    }
    bool atEnd()
    {
        skipSeparators();
        return pos_ >= text_.size();
    }
    // Siguiente carácter si es una letra (un comando), sin consumirlo.
    std::optional<char> peekLetter()
    {
        skipSeparators();
        if (pos_ < text_.size() && std::isalpha(static_cast<unsigned char>(text_[pos_]))) {
            return text_[pos_];
        }
        return std::nullopt;
    }
    char take() { return text_[pos_++]; }
    std::optional<double> number()
    {
        skipSeparators();
        if (pos_ >= text_.size()) {
            return std::nullopt;
        }
        // from_chars no admite un «+» delante.
        std::size_t start = pos_;
        if (text_[start] == '+') {
            ++start;
        }
        double value = 0.0;
        const auto [end, error] = std::from_chars(text_.data() + start, text_.data() + text_.size(), value);
        if (error != std::errc() || end == text_.data() + start) {
            return std::nullopt;
        }
        pos_ = std::size_t(end - text_.data());
        return value;
    }
    // Una bandera de arco: un solo «0» o «1», pegada o no a lo siguiente.
    std::optional<bool> flag()
    {
        skipSeparators();
        if (pos_ < text_.size() && (text_[pos_] == '0' || text_[pos_] == '1')) {
            return text_[pos_++] == '1';
        }
        return std::nullopt;
    }

private:
    std::string_view text_;
    std::size_t pos_ = 0;
};

std::vector<double> allNumbers(std::string_view text)
{
    Numbers reader(text);
    std::vector<double> out;
    while (const auto v = reader.number()) {
        out.push_back(*v);
    }
    return out;
}

// «translate(10 20) rotate(30)» → una matriz.
Matrix parseTransform(std::string_view text)
{
    Matrix result;
    std::size_t pos = 0;
    while (pos < text.size()) {
        const std::size_t open = text.find('(', pos);
        if (open == std::string_view::npos) {
            break;
        }
        const std::size_t close = text.find(')', open);
        if (close == std::string_view::npos) {
            break;
        }
        const std::string name(trimView(text.substr(pos, open - pos)));
        const std::vector<double> v = allNumbers(text.substr(open + 1, close - open - 1));
        const auto at = [&](std::size_t i, double fallback) { return i < v.size() ? v[i] : fallback; };
        Matrix m;
        if (name == "translate") {
            m.e = at(0, 0);
            m.f = at(1, 0);
        } else if (name == "scale") {
            m.a = at(0, 1);
            m.d = at(1, at(0, 1));
        } else if (name == "rotate") {
            const double r = at(0, 0) * kPi / 180.0;
            m.a = std::cos(r);
            m.b = std::sin(r);
            m.c = -std::sin(r);
            m.d = std::cos(r);
            if (v.size() >= 3) {
                // Girar alrededor de (cx, cy).
                Matrix toOrigin;
                toOrigin.e = -v[1];
                toOrigin.f = -v[2];
                Matrix back;
                back.e = v[1];
                back.f = v[2];
                m = back * m * toOrigin;
            }
        } else if (name == "skewX") {
            m.c = std::tan(at(0, 0) * kPi / 180.0);
        } else if (name == "skewY") {
            m.b = std::tan(at(0, 0) * kPi / 180.0);
        } else if (name == "matrix" && v.size() >= 6) {
            m = {v[0], v[1], v[2], v[3], v[4], v[5]};
        }
        result = result * m;
        pos = close + 1;
    }
    return result;
}

// Longitud con unidad → milímetros (sin unidad: píxeles de 96 por pulgada).
std::optional<double> lengthMm(const std::string& text)
{
    const std::string_view view = trimView(text);
    double value = 0.0;
    const auto [end, error] = std::from_chars(view.data(), view.data() + view.size(), value);
    if (error != std::errc() || end == view.data()) {
        return std::nullopt;
    }
    const std::string unit(trimView(std::string_view(end, std::size_t(view.data() + view.size() - end))));
    if (unit == "%") {
        return std::nullopt;
    }
    double factor = 25.4 / 96.0;
    if (unit == "mm") {
        factor = 1.0;
    } else if (unit == "cm") {
        factor = 10.0;
    } else if (unit == "in") {
        factor = 25.4;
    } else if (unit == "pt") {
        factor = 25.4 / 72.0;
    } else if (unit == "pc") {
        factor = 25.4 / 6.0;
    }
    return value * factor;
}

// Pasa una curva por una matriz. Si es una semejanza (giro, escala uniforme y, a lo sumo, un espejo) las curvas conservan
// su tipo; si no (escala desigual o inclinación), se convierten en splines que pasan por sus puntos.
void addCurve(std::vector<Geometry>& out, const Geometry& g, const Matrix& m)
{
    const double det = m.determinant();
    if (std::abs(det) < 1e-14) {
        return;
    }
    const double s = std::sqrt(std::abs(det));
    const bool similarity = std::abs(m.a * m.a + m.b * m.b - s * s) < 1e-9 * s * s && std::abs(m.c * m.c + m.d * m.d - s * s) < 1e-9 * s * s &&
                            std::abs(m.a * m.c + m.b * m.d) < 1e-9 * s * s;
    const double mirror = det < 0.0 ? -1.0 : 1.0;
    const double theta = std::atan2(m.b, m.a);
    const auto wrap = [](double angle) {
        while (angle < 0.0) {
            angle += 2.0 * kPi;
        }
        while (angle >= 2.0 * kPi) {
            angle -= 2.0 * kPi;
        }
        return angle;
    };
    if (std::holds_alternative<Line>(g)) {
        const Line& l = std::get<Line>(g);
        out.push_back(Line{m.apply(l.start), m.apply(l.end)});
        return;
    }
    if (std::holds_alternative<Point>(g)) {
        out.push_back(Point{m.apply(std::get<Point>(g).position)});
        return;
    }
    if (std::holds_alternative<BSpline>(g)) {
        BSpline b = std::get<BSpline>(g);
        for (Vec2& pole : b.poles) {
            pole = m.apply(pole);
        }
        out.push_back(std::move(b));
        return;
    }
    if (similarity) {
        if (const auto* c = std::get_if<Circle>(&g)) {
            out.push_back(Circle{m.apply(c->center), c->radius * s});
        } else if (const auto* a = std::get_if<Arc>(&g)) {
            Arc r{m.apply(a->center), a->radius * s, 0.0, 0.0};
            if (mirror > 0) {
                r.startAngle = wrap(theta + a->startAngle);
                r.endAngle = wrap(theta + a->endAngle);
            } else {
                r.startAngle = wrap(theta - a->endAngle);
                r.endAngle = wrap(theta - a->startAngle);
            }
            out.push_back(r);
        } else if (const auto* e = std::get_if<Ellipse>(&g)) {
            out.push_back(Ellipse{m.apply(e->center), e->majorRadius * s, e->minorRadius * s, theta + mirror * e->rotation});
        } else if (const auto* ea = std::get_if<EllipseArc>(&g)) {
            EllipseArc r;
            r.ellipse = Ellipse{m.apply(ea->ellipse.center), ea->ellipse.majorRadius * s, ea->ellipse.minorRadius * s,
                                theta + mirror * ea->ellipse.rotation};
            if (mirror > 0) {
                r.startParam = ea->startParam;
                r.endParam = ea->endParam;
            } else {
                r.startParam = -ea->endParam;
                r.endParam = -ea->startParam;
            }
            out.push_back(r);
        }
        return;
    }
    // Sin semejanza: una spline por los puntos de la curva ya transformados (o la elipse entera cerrada).
    std::vector<Vec2> points = polyline(g);
    for (Vec2& p : points) {
        p = m.apply(p);
    }
    if (points.size() >= 3) {
        out.push_back(interpolatingSpline(points));
    }
}

// --- Trazados (path) ---------------------------------------------------------------------------------------

struct Pen {
    Vec2 at;
    Vec2 start;
    std::optional<Vec2> lastCubicControl;
    std::optional<Vec2> lastQuadControl;
};

void bezier(std::vector<Geometry>& out, const std::vector<Vec2>& poles, int degree)
{
    BSpline spline;
    spline.poles = poles;
    spline.weights.assign(poles.size(), 1.0);
    spline.degree = degree;
    out.push_back(std::move(spline));
}

bool nearly(Vec2 a, Vec2 b)
{
    return std::hypot(a.x - b.x, a.y - b.y) < 1e-9;
}

// Arco SVG con extremo (apéndice F.6 de la especificación): centro, radios y ángulos; arco circular o de elipse.
void svgArc(std::vector<Geometry>& out, Vec2 from, Vec2 to, double rx, double ry, double phiDeg, bool large, bool sweep)
{
    if (nearly(from, to)) {
        return;
    }
    rx = std::abs(rx);
    ry = std::abs(ry);
    if (rx < 1e-12 || ry < 1e-12) {
        out.push_back(Line{from, to});
        return;
    }
    const double phi = phiDeg * kPi / 180.0;
    const double cp = std::cos(phi), sp = std::sin(phi);
    const double dx = (from.x - to.x) / 2.0, dy = (from.y - to.y) / 2.0;
    const double x1 = cp * dx + sp * dy;
    const double y1 = -sp * dx + cp * dy;
    const double lambda = x1 * x1 / (rx * rx) + y1 * y1 / (ry * ry);
    if (lambda > 1.0) {
        rx *= std::sqrt(lambda);
        ry *= std::sqrt(lambda);
    }
    const double num = rx * rx * ry * ry - rx * rx * y1 * y1 - ry * ry * x1 * x1;
    const double den = rx * rx * y1 * y1 + ry * ry * x1 * x1;
    double coef = den > 0.0 ? std::sqrt(std::max(0.0, num / den)) : 0.0;
    if (large == sweep) {
        coef = -coef;
    }
    const double cxp = coef * rx * y1 / ry;
    const double cyp = -coef * ry * x1 / rx;
    const Vec2 center{cp * cxp - sp * cyp + (from.x + to.x) / 2.0, sp * cxp + cp * cyp + (from.y + to.y) / 2.0};
    const auto angle = [](double ux, double uy, double vx, double vy) {
        const double dot = ux * vx + uy * vy;
        const double len = std::hypot(ux, uy) * std::hypot(vx, vy);
        double a = std::acos(std::clamp(dot / len, -1.0, 1.0));
        if (ux * vy - uy * vx < 0.0) {
            a = -a;
        }
        return a;
    };
    const double theta1 = angle(1.0, 0.0, (x1 - cxp) / rx, (y1 - cyp) / ry);
    double delta = angle((x1 - cxp) / rx, (y1 - cyp) / ry, (-x1 - cxp) / rx, (-y1 - cyp) / ry);
    if (!sweep && delta > 0.0) {
        delta -= 2.0 * kPi;
    } else if (sweep && delta < 0.0) {
        delta += 2.0 * kPi;
    }
    const double begin = delta >= 0.0 ? theta1 : theta1 + delta;
    const double end = begin + std::abs(delta);
    if (std::abs(rx - ry) < 1e-9 * std::max(rx, ry)) {
        // Circular: el giro del eje no importa; los ángulos van medidos desde el eje X real.
        out.push_back(Arc{center, rx, begin + phi, end + phi});
        return;
    }
    Ellipse ellipse{center, rx, ry, phi};
    double startParam = begin, endParam = end;
    if (ry > rx) {
        ellipse = Ellipse{center, ry, rx, phi + kPi / 2.0};
        startParam -= kPi / 2.0;
        endParam -= kPi / 2.0;
    }
    out.push_back(EllipseArc{ellipse, startParam, endParam});
}

// Un trazado entero: cada subtrazado sale como curvas sueltas (la importación une los extremos que coinciden).
void parsePath(std::vector<Geometry>& out, std::string_view text)
{
    Numbers reader(text);
    Pen pen;
    char command = 0;
    bool first = true;
    while (!reader.atEnd()) {
        if (const auto letter = reader.peekLetter()) {
            command = reader.take();
            if (command == 'Z' || command == 'z') {
                if (!nearly(pen.at, pen.start)) {
                    out.push_back(Line{pen.at, pen.start});
                }
                pen.at = pen.start;
                pen.lastCubicControl.reset();
                pen.lastQuadControl.reset();
                continue;
            }
        } else if (command == 0) {
            return;
        }
        const bool relative = std::islower(static_cast<unsigned char>(command)) != 0;
        const char upper = char(std::toupper(static_cast<unsigned char>(command)));
        const auto read = [&](std::size_t count) -> std::optional<std::vector<double>> {
            std::vector<double> v;
            for (std::size_t i = 0; i < count; ++i) {
                const auto n = reader.number();
                if (!n) {
                    return std::nullopt;
                }
                v.push_back(*n);
            }
            return v;
        };
        const Vec2 origin = relative ? pen.at : Vec2{0, 0};
        const auto point = [&](double x, double y) { return Vec2{origin.x + x, origin.y + y}; };
        bool cubic = false, quad = false;
        switch (upper) {
        case 'M': {
            const auto v = read(2);
            if (!v) {
                return;
            }
            // El primer M siempre es absoluto; los pares siguientes son líneas.
            pen.at = first ? Vec2{(*v)[0], (*v)[1]} : point((*v)[0], (*v)[1]);
            pen.start = pen.at;
            command = relative ? 'l' : 'L';
            first = false;
            break;
        }
        case 'L': {
            const auto v = read(2);
            if (!v) {
                return;
            }
            const Vec2 to = point((*v)[0], (*v)[1]);
            if (!nearly(pen.at, to)) {
                out.push_back(Line{pen.at, to});
            }
            pen.at = to;
            break;
        }
        case 'H':
        case 'V': {
            const auto v = read(1);
            if (!v) {
                return;
            }
            const Vec2 to = upper == 'H' ? Vec2{relative ? pen.at.x + (*v)[0] : (*v)[0], pen.at.y}
                                         : Vec2{pen.at.x, relative ? pen.at.y + (*v)[0] : (*v)[0]};
            if (!nearly(pen.at, to)) {
                out.push_back(Line{pen.at, to});
            }
            pen.at = to;
            break;
        }
        case 'C':
        case 'S': {
            Vec2 c1;
            if (upper == 'C') {
                const auto v = read(6);
                if (!v) {
                    return;
                }
                c1 = point((*v)[0], (*v)[1]);
                const Vec2 c2 = point((*v)[2], (*v)[3]);
                const Vec2 to = point((*v)[4], (*v)[5]);
                bezier(out, {pen.at, c1, c2, to}, 3);
                pen.lastCubicControl = c2;
                pen.at = to;
            } else {
                const auto v = read(4);
                if (!v) {
                    return;
                }
                c1 = pen.lastCubicControl ? Vec2{2 * pen.at.x - pen.lastCubicControl->x, 2 * pen.at.y - pen.lastCubicControl->y} : pen.at;
                const Vec2 c2 = point((*v)[0], (*v)[1]);
                const Vec2 to = point((*v)[2], (*v)[3]);
                bezier(out, {pen.at, c1, c2, to}, 3);
                pen.lastCubicControl = c2;
                pen.at = to;
            }
            cubic = true;
            break;
        }
        case 'Q':
        case 'T': {
            Vec2 control;
            Vec2 to;
            if (upper == 'Q') {
                const auto v = read(4);
                if (!v) {
                    return;
                }
                control = point((*v)[0], (*v)[1]);
                to = point((*v)[2], (*v)[3]);
            } else {
                const auto v = read(2);
                if (!v) {
                    return;
                }
                control = pen.lastQuadControl ? Vec2{2 * pen.at.x - pen.lastQuadControl->x, 2 * pen.at.y - pen.lastQuadControl->y} : pen.at;
                to = point((*v)[0], (*v)[1]);
            }
            bezier(out, {pen.at, control, to}, 2);
            pen.lastQuadControl = control;
            pen.at = to;
            quad = true;
            break;
        }
        case 'A': {
            const auto rx = reader.number();
            const auto ry = reader.number();
            const auto rotation = reader.number();
            const auto large = reader.flag();
            const auto sweep = reader.flag();
            const auto x = reader.number();
            const auto y = reader.number();
            if (!rx || !ry || !rotation || !large || !sweep || !x || !y) {
                return;
            }
            const Vec2 to = point(*x, *y);
            svgArc(out, pen.at, to, *rx, *ry, *rotation, *large, *sweep);
            pen.at = to;
            break;
        }
        default:
            return;  // comando desconocido: se deja lo leído hasta aquí
        }
        if (!cubic) {
            pen.lastCubicControl.reset();
        }
        if (!quad) {
            pen.lastQuadControl.reset();
        }
    }
}

// --- Lectura del XML -----------------------------------------------------------------------------------

struct Tag {
    std::string name;
    std::map<std::string, std::string> attributes;
    bool closing = false;
    bool selfClosing = false;
};

std::optional<Tag> parseTag(std::string_view body)
{
    Tag tag;
    std::size_t i = 0;
    if (!body.empty() && body.front() == '/') {
        tag.closing = true;
        ++i;
    }
    if (!body.empty() && body.back() == '/') {
        tag.selfClosing = true;
        body.remove_suffix(1);
    }
    const std::size_t nameStart = i;
    while (i < body.size() && !std::isspace(static_cast<unsigned char>(body[i]))) {
        ++i;
    }
    tag.name = std::string(body.substr(nameStart, i - nameStart));
    // Sin el prefijo de espacio de nombres («svg:path» → «path»).
    if (const auto colon = tag.name.find(':'); colon != std::string::npos) {
        tag.name = tag.name.substr(colon + 1);
    }
    while (i < body.size()) {
        while (i < body.size() && std::isspace(static_cast<unsigned char>(body[i]))) {
            ++i;
        }
        const std::size_t keyStart = i;
        while (i < body.size() && body[i] != '=' && !std::isspace(static_cast<unsigned char>(body[i]))) {
            ++i;
        }
        const std::string key(body.substr(keyStart, i - keyStart));
        while (i < body.size() && (std::isspace(static_cast<unsigned char>(body[i])) || body[i] == '=')) {
            ++i;
        }
        if (i >= body.size() || (body[i] != '"' && body[i] != '\'')) {
            if (key.empty()) {
                break;
            }
            continue;
        }
        const char quote = body[i++];
        const std::size_t valueStart = i;
        while (i < body.size() && body[i] != quote) {
            ++i;
        }
        tag.attributes[key] = std::string(body.substr(valueStart, i - valueStart));
        ++i;
    }
    return tag;
}

double attribute(const Tag& tag, const char* name, double fallback = 0.0)
{
    const auto it = tag.attributes.find(name);
    if (it == tag.attributes.end()) {
        return fallback;
    }
    const auto v = allNumbers(it->second);
    return v.empty() ? fallback : v.front();
}

bool hidden(const Tag& tag)
{
    const auto style = tag.attributes.find("style");
    if (style != tag.attributes.end() && style->second.find("display:none") != std::string::npos) {
        return true;
    }
    const auto display = tag.attributes.find("display");
    return display != tag.attributes.end() && trimView(display->second) == "none";
}

} // namespace

DxfDrawing readSvg(std::istream& in)
{
    DxfDrawing drawing;
    const std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    std::set<std::string> skipped;
    // Todo en milímetros: raíz = voltear Y · escala de unidades · mover el origen del viewBox.
    Matrix root;
    root.d = -1.0;
    std::vector<Matrix> stack;
    stack.push_back(root);
    std::vector<std::string> names;       // las etiquetas abiertas
    std::vector<bool> counted;            // si cada una puso una matriz en la pila
    std::vector<bool> inIgnored;          // si cada una está dentro de defs, clipPath… (se ignora todo)
    int skipDepth = 0;
    std::vector<std::pair<Geometry, Matrix>> pending;  // curvas con su matriz, antes de saber la altura del documento
    bool haveHeight = false;
    double unitScale = 25.4 / 96.0;
    double viewX = 0.0, viewY = 0.0, viewH = 0.0;
    bool haveRoot = false;

    std::size_t pos = 0;
    while (pos < text.size()) {
        const std::size_t open = text.find('<', pos);
        if (open == std::string::npos) {
            break;
        }
        if (text.compare(open, 4, "<!--") == 0) {
            const std::size_t end = text.find("-->", open);
            pos = end == std::string::npos ? text.size() : end + 3;
            continue;
        }
        if (text.compare(open, 9, "<![CDATA[") == 0) {
            const std::size_t end = text.find("]]>", open);
            pos = end == std::string::npos ? text.size() : end + 3;
            continue;
        }
        if (text.compare(open, 2, "<?") == 0 || text.compare(open, 2, "<!") == 0) {
            const std::size_t end = text.find('>', open);
            pos = end == std::string::npos ? text.size() : end + 1;
            continue;
        }
        // El final de la etiqueta: el primer «>» fuera de comillas.
        std::size_t close = open + 1;
        char quote = 0;
        while (close < text.size() && (quote != 0 || text[close] != '>')) {
            if (quote != 0 && text[close] == quote) {
                quote = 0;
            } else if (quote == 0 && (text[close] == '"' || text[close] == '\'')) {
                quote = text[close];
            }
            ++close;
        }
        if (close >= text.size()) {
            break;
        }
        const auto parsed = parseTag(std::string_view(text).substr(open + 1, close - open - 1));
        pos = close + 1;
        if (!parsed || parsed->name.empty()) {
            continue;
        }
        const Tag& tag = *parsed;
        static const std::set<std::string> kIgnoredContainers = {"defs", "clipPath", "mask", "symbol", "pattern", "marker", "metadata", "style", "title", "desc"};

        if (tag.closing) {
            if (!names.empty()) {
                if (inIgnored.back()) {
                    --skipDepth;
                }
                if (counted.back() && stack.size() > 1) {
                    stack.pop_back();
                }
                names.pop_back();
                counted.pop_back();
                inIgnored.pop_back();
            }
            continue;
        }
        if (skipDepth > 0 || kIgnoredContainers.contains(tag.name)) {
            if (!tag.selfClosing) {
                names.push_back(tag.name);
                counted.push_back(false);
                inIgnored.push_back(true);
                ++skipDepth;
            }
            continue;
        }
        Matrix local;
        if (const auto t = tag.attributes.find("transform"); t != tag.attributes.end()) {
            local = parseTransform(t->second);
        }
        const bool isHidden = hidden(tag);
        const Matrix current = stack.back() * local;

        if (tag.name == "svg" && !haveRoot) {
            haveRoot = true;
            double vx = 0, vy = 0, vw = 0, vh = 0;
            bool viewBox = false;
            if (const auto it = tag.attributes.find("viewBox"); it != tag.attributes.end()) {
                const auto v = allNumbers(it->second);
                if (v.size() == 4 && v[2] > 0 && v[3] > 0) {
                    vx = v[0];
                    vy = v[1];
                    vw = v[2];
                    vh = v[3];
                    viewBox = true;
                }
            }
            // Ancho y alto del documento en mm (0 = no vienen o no tienen unidad que se pueda convertir).
            double widthMm = 0.0;
            double heightMm = 0.0;
            if (const auto it = tag.attributes.find("width"); it != tag.attributes.end()) {
                widthMm = lengthMm(it->second).value_or(0.0);
            }
            if (const auto it = tag.attributes.find("height"); it != tag.attributes.end()) {
                heightMm = lengthMm(it->second).value_or(0.0);
            }
            if (viewBox) {
                unitScale = widthMm > 0.0 ? widthMm / vw : heightMm > 0.0 ? heightMm / vh : 25.4 / 96.0;
                viewX = vx;
                viewY = vy;
                viewH = vh;
                haveHeight = true;
            } else if (heightMm > 0.0) {
                unitScale = 25.4 / 96.0;
                viewH = heightMm / unitScale;
                haveHeight = true;
            }
        }

        std::vector<Geometry> shapes;
        if (!isHidden) {
            if (tag.name == "path") {
                if (const auto d = tag.attributes.find("d"); d != tag.attributes.end()) {
                    parsePath(shapes, d->second);
                }
            } else if (tag.name == "rect") {
                const double x = attribute(tag, "x"), y = attribute(tag, "y"), w = attribute(tag, "width"), h = attribute(tag, "height");
                double rx = attribute(tag, "rx", -1.0), ry = attribute(tag, "ry", -1.0);
                if (rx < 0 && ry >= 0) {
                    rx = ry;
                }
                if (ry < 0 && rx >= 0) {
                    ry = rx;
                }
                rx = std::clamp(rx, 0.0, w / 2.0);
                ry = std::clamp(ry, 0.0, h / 2.0);
                if (w > 0 && h > 0) {
                    if (rx > 0 && ry > 0) {
                        // Los lados y, en cada esquina, un cuarto de elipse (arcos de SVG con giro positivo).
                        if (w - 2 * rx > 1e-12) {
                            shapes.push_back(Line{{x + rx, y}, {x + w - rx, y}});
                        }
                        svgArc(shapes, {x + w - rx, y}, {x + w, y + ry}, rx, ry, 0, false, true);
                        if (h - 2 * ry > 1e-12) {
                            shapes.push_back(Line{{x + w, y + ry}, {x + w, y + h - ry}});
                        }
                        svgArc(shapes, {x + w, y + h - ry}, {x + w - rx, y + h}, rx, ry, 0, false, true);
                        if (w - 2 * rx > 1e-12) {
                            shapes.push_back(Line{{x + w - rx, y + h}, {x + rx, y + h}});
                        }
                        svgArc(shapes, {x + rx, y + h}, {x, y + h - ry}, rx, ry, 0, false, true);
                        if (h - 2 * ry > 1e-12) {
                            shapes.push_back(Line{{x, y + h - ry}, {x, y + ry}});
                        }
                        svgArc(shapes, {x, y + ry}, {x + rx, y}, rx, ry, 0, false, true);
                    } else {
                        shapes.push_back(Line{{x, y}, {x + w, y}});
                        shapes.push_back(Line{{x + w, y}, {x + w, y + h}});
                        shapes.push_back(Line{{x + w, y + h}, {x, y + h}});
                        shapes.push_back(Line{{x, y + h}, {x, y}});
                    }
                }
            } else if (tag.name == "circle") {
                const double r = attribute(tag, "r");
                if (r > 0) {
                    shapes.push_back(Circle{{attribute(tag, "cx"), attribute(tag, "cy")}, r});
                }
            } else if (tag.name == "ellipse") {
                const double rx = attribute(tag, "rx"), ry = attribute(tag, "ry");
                if (rx > 0 && ry > 0) {
                    const Vec2 c{attribute(tag, "cx"), attribute(tag, "cy")};
                    if (std::abs(rx - ry) < 1e-12) {
                        shapes.push_back(Circle{c, rx});
                    } else if (rx > ry) {
                        shapes.push_back(Ellipse{c, rx, ry, 0.0});
                    } else {
                        shapes.push_back(Ellipse{c, ry, rx, kPi / 2.0});
                    }
                }
            } else if (tag.name == "line") {
                const Vec2 a{attribute(tag, "x1"), attribute(tag, "y1")}, b{attribute(tag, "x2"), attribute(tag, "y2")};
                if (!nearly(a, b)) {
                    shapes.push_back(Line{a, b});
                }
            } else if (tag.name == "polyline" || tag.name == "polygon") {
                if (const auto it = tag.attributes.find("points"); it != tag.attributes.end()) {
                    const auto v = allNumbers(it->second);
                    std::vector<Vec2> points;
                    for (std::size_t i = 0; i + 1 < v.size(); i += 2) {
                        points.push_back({v[i], v[i + 1]});
                    }
                    for (std::size_t i = 0; i + 1 < points.size(); ++i) {
                        if (!nearly(points[i], points[i + 1])) {
                            shapes.push_back(Line{points[i], points[i + 1]});
                        }
                    }
                    if (tag.name == "polygon" && points.size() > 2 && !nearly(points.back(), points.front())) {
                        shapes.push_back(Line{points.back(), points.front()});
                    }
                }
            } else if (tag.name == "text" || tag.name == "image" || tag.name == "use" || tag.name == "foreignObject") {
                skipped.insert(tag.name);
            }
        }
        for (const Geometry& g : shapes) {
            pending.emplace_back(g, current);
        }
        if (!tag.selfClosing) {
            names.push_back(tag.name);
            counted.push_back(true);
            inIgnored.push_back(false);
            stack.push_back(current);
        }
    }

    // La altura del documento en unidades de usuario fija dónde queda el eje Y invertido: la base del viewBox es y = 0.
    // Matriz final = (escala de unidades) · (voltear Y respecto a la base del viewBox) · (matriz del elemento sin el volteo).
    Matrix finish;
    finish.a = unitScale;
    finish.d = unitScale;
    for (const auto& [g, m] : pending) {
        // «m» ya lleva el volteo de la raíz (d = −1) por la multiplicación; se le pone el desplazamiento del viewBox.
        Matrix full = m;
        full.f += viewH + viewY;  // y' = −y + (viewY + viewH), a la base del viewBox
        full.e -= viewX;
        // Si el elemento vino con transformaciones, el volteo se aplicó al final: ya está incluido por la raíz.
        full = finish * full;
        addCurve(drawing.geometry, g, full);
    }
    // Sin alto en el documento no se sabe dónde está la base: el dibujo se sube hasta que su parte más baja quede en Y = 0.
    if (!haveHeight && !drawing.geometry.empty()) {
        double low = 1e300;
        for (const Geometry& g : drawing.geometry) {
            for (const Vec2& p : polyline(g)) {
                low = std::min(low, p.y);
            }
        }
        if (low < 1e299 && std::abs(low) > 1e-12) {
            for (Geometry& g : drawing.geometry) {
                const auto move = [&](Vec2& p) { p.y -= low; };
                if (auto* p = std::get_if<Point>(&g)) move(p->position);
                else if (auto* l = std::get_if<Line>(&g)) { move(l->start); move(l->end); }
                else if (auto* c = std::get_if<Circle>(&g)) move(c->center);
                else if (auto* a = std::get_if<Arc>(&g)) move(a->center);
                else if (auto* e = std::get_if<Ellipse>(&g)) move(e->center);
                else if (auto* ea = std::get_if<EllipseArc>(&g)) move(ea->ellipse.center);
                else if (auto* b = std::get_if<BSpline>(&g)) for (Vec2& pole : b->poles) move(pole);
            }
        }
    }
    drawing.skipped.assign(skipped.begin(), skipped.end());
    return drawing;
}

} // namespace nuf::sketch
