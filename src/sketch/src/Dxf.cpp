#include <nuf/sketch/Dxf.hpp>

#include <nuf/sketch/Tools.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <optional>
#include <utility>

namespace nuf::sketch {

namespace {

constexpr double kPi = std::numbers::pi;

std::string trim(std::string s)
{
    const auto first = s.find_first_not_of(" \t\r\n");
    const auto last = s.find_last_not_of(" \t\r\n");
    return first == std::string::npos ? std::string{} : s.substr(first, last - first + 1);
}

// Entidad leída: su tipo y sus pares (código de grupo, valor) en orden.
struct Record {
    std::string type;
    std::vector<std::pair<int, std::string>> values;

    std::optional<double> number(int code) const
    {
        for (const auto& [c, v] : values) {
            if (c == code) {
                try {
                    return std::stod(v);
                } catch (...) {
                    return std::nullopt;
                }
            }
        }
        return std::nullopt;
    }
    double number(int code, double fallback) const { return number(code).value_or(fallback); }

    std::vector<double> numbers(int code) const
    {
        std::vector<double> out;
        for (const auto& [c, v] : values) {
            if (c == code) {
                try {
                    out.push_back(std::stod(v));
                } catch (...) {
                }
            }
        }
        return out;
    }
};

struct Vertex {
    Vec2 p;
    double bulge = 0.0;
};

// Tramo de polilínea: recto o arco por «bulge» (tan(ángulo/4), positivo =
// antihorario de p1 a p2).
std::optional<Geometry> segment(const Vertex& a, Vec2 b)
{
    const double dx = b.x - a.p.x;
    const double dy = b.y - a.p.y;
    const double chord = std::hypot(dx, dy);
    if (chord < 1e-9) {
        return std::nullopt;
    }
    if (std::abs(a.bulge) < 1e-12) {
        return Line{a.p, b};
    }
    const double theta = 4.0 * std::atan(a.bulge);
    const double radius = std::abs(chord / (2.0 * std::sin(theta / 2.0)));
    const double d = (chord / 2.0) / std::tan(theta / 2.0);
    const Vec2 mid{(a.p.x + b.x) / 2.0, (a.p.y + b.y) / 2.0};
    const Vec2 left{-dy / chord, dx / chord};
    const Vec2 center{mid.x + left.x * d, mid.y + left.y * d};
    const double angleA = std::atan2(a.p.y - center.y, a.p.x - center.x);
    const double angleB = std::atan2(b.y - center.y, b.x - center.x);
    const auto ccw = [](double from, double to) {
        const double r = std::fmod(to - from, 2.0 * kPi);
        return r < 0.0 ? r + 2.0 * kPi : r;
    };
    if (theta > 0.0) {
        return Arc{center, radius, angleA, angleA + ccw(angleA, angleB)};
    }
    return Arc{center, radius, angleB, angleB + ccw(angleB, angleA)};
}

void polyline(const std::vector<Vertex>& vertices, bool closed, std::vector<Geometry>& out)
{
    for (std::size_t i = 0; i + 1 < vertices.size(); ++i) {
        if (auto g = segment(vertices[i], vertices[i + 1].p)) {
            out.push_back(*g);
        }
    }
    if (closed && vertices.size() > 2) {
        if (auto g = segment(vertices.back(), vertices.front().p)) {
            out.push_back(*g);
        }
    }
}

// Spline DXF (nudos y pesos cualesquiera): se evalúa con De Boor y se
// reinterpola por puntos con la B-spline uniforme del sketch.
std::optional<Geometry> spline(const Record& r)
{
    const int degree = int(r.number(71, 3));
    const std::vector<double> knots = r.numbers(40);
    const std::vector<double> xs = r.numbers(10);
    const std::vector<double> ys = r.numbers(20);
    std::vector<double> weights = r.numbers(41);
    const std::size_t n = std::min(xs.size(), ys.size());
    if (n < 2 || degree < 1 || knots.size() != n + std::size_t(degree) + 1) {
        // Spline por puntos de paso (códigos 11/21) sin polos: se interpola directamente.
        const std::vector<double> fx = r.numbers(11);
        const std::vector<double> fy = r.numbers(21);
        std::vector<Vec2> fit;
        for (std::size_t i = 0; i < std::min(fx.size(), fy.size()); ++i) {
            fit.push_back({fx[i], fy[i]});
        }
        if (fit.size() >= 2) {
            return interpolatingSpline(fit);
        }
        return std::nullopt;
    }
    if (weights.size() != n) {
        weights.assign(n, 1.0);
    }
    const auto at = [&](double t) {
        const int p = degree;
        int k = p;
        while (k < int(n) - 1 && t >= knots[std::size_t(k) + 1]) {
            ++k;
        }
        struct H { double x, y, w; };
        std::vector<H> d(std::size_t(p) + 1);
        for (int j = 0; j <= p; ++j) {
            const std::size_t i = std::size_t(j + k - p);
            d[std::size_t(j)] = {xs[i] * weights[i], ys[i] * weights[i], weights[i]};
        }
        for (int s = 1; s <= p; ++s) {
            for (int j = p; j >= s; --j) {
                const double left = knots[std::size_t(j + k - p)];
                const double right = knots[std::size_t(j + 1 + k - s)];
                const double alpha = right > left ? (t - left) / (right - left) : 0.0;
                H& a = d[std::size_t(j)];
                const H& b = d[std::size_t(j - 1)];
                a = {(1 - alpha) * b.x + alpha * a.x, (1 - alpha) * b.y + alpha * a.y, (1 - alpha) * b.w + alpha * a.w};
            }
        }
        return Vec2{d.back().x / d.back().w, d.back().y / d.back().w};
    };
    const double t0 = knots[std::size_t(degree)];
    const double t1 = knots[n];
    const int samples = std::max(16, int(n) * 6);
    std::vector<Vec2> points;
    for (int i = 0; i <= samples; ++i) {
        points.push_back(at(t0 + (t1 - t0) * i / samples));
    }
    return interpolatingSpline(points);
}

} // namespace

DxfDrawing readDxf(std::istream& in)
{
    DxfDrawing out;
    std::vector<Record> records;
    bool inEntities = false;
    std::string codeLine;
    std::string value;
    while (std::getline(in, codeLine) && std::getline(in, value)) {
        int code = 0;
        try {
            code = std::stoi(trim(codeLine));
        } catch (...) {
            continue;
        }
        value = trim(value);
        if (code == 2 && value == "ENTITIES") {
            inEntities = true;
            continue;
        }
        if (code == 0) {
            if (value == "ENDSEC") {
                inEntities = false;
            }
            if (inEntities) {
                records.push_back({value, {}});
            }
            continue;
        }
        if (inEntities && !records.empty()) {
            records.back().values.push_back({code, value});
        }
    }

    const auto skip = [&](const std::string& type) {
        if (std::ranges::find(out.skipped, type) == out.skipped.end()) {
            out.skipped.push_back(type);
        }
    };
    for (std::size_t i = 0; i < records.size(); ++i) {
        const Record& r = records[i];
        const auto xy = [&](int cx, int cy) { return Vec2{r.number(cx, 0.0), r.number(cy, 0.0)}; };
        if (r.type == "LINE") {
            const Vec2 a = xy(10, 20);
            const Vec2 b = xy(11, 21);
            if (std::hypot(b.x - a.x, b.y - a.y) > 1e-9) {
                out.geometry.push_back(Line{a, b});
            }
        } else if (r.type == "CIRCLE") {
            out.geometry.push_back(Circle{xy(10, 20), r.number(40, 0.0)});
        } else if (r.type == "ARC") {
            const double a0 = r.number(50, 0.0) * kPi / 180.0;
            double a1 = r.number(51, 0.0) * kPi / 180.0;
            while (a1 <= a0) {
                a1 += 2.0 * kPi;
            }
            out.geometry.push_back(Arc{xy(10, 20), r.number(40, 0.0), a0, a1});
        } else if (r.type == "POINT") {
            out.geometry.push_back(Point{xy(10, 20)});
        } else if (r.type == "LWPOLYLINE") {
            std::vector<Vertex> vertices;
            for (const auto& [c, v] : r.values) {
                try {
                    if (c == 10) vertices.push_back({{std::stod(v), 0.0}, 0.0});
                    else if (c == 20 && !vertices.empty()) vertices.back().p.y = std::stod(v);
                    else if (c == 42 && !vertices.empty()) vertices.back().bulge = std::stod(v);
                } catch (...) {
                }
            }
            polyline(vertices, (int(r.number(70, 0.0)) & 1) != 0, out.geometry);
        } else if (r.type == "POLYLINE") {
            std::vector<Vertex> vertices;
            std::size_t j = i + 1;
            for (; j < records.size() && records[j].type == "VERTEX"; ++j) {
                vertices.push_back({{records[j].number(10, 0.0), records[j].number(20, 0.0)}, records[j].number(42, 0.0)});
            }
            polyline(vertices, (int(r.number(70, 0.0)) & 1) != 0, out.geometry);
            i = j;  // salta los VERTEX y el SEQEND
        } else if (r.type == "ELLIPSE") {
            const Vec2 c = xy(10, 20);
            const Vec2 axis = xy(11, 21);
            const double major = std::hypot(axis.x, axis.y);
            const double minor = major * r.number(40, 1.0);
            const Ellipse e{c, std::max(major, minor), std::min(major, minor),
                            std::atan2(axis.y, axis.x) + (minor > major ? kPi / 2.0 : 0.0)};
            const double s = r.number(41, 0.0);
            double t = r.number(42, 2.0 * kPi);
            if (std::abs(t - s - 2.0 * kPi) < 1e-9 || std::abs(t - s) < 1e-12) {
                out.geometry.push_back(e);
            } else {
                while (t <= s) {
                    t += 2.0 * kPi;
                }
                out.geometry.push_back(EllipseArc{e, s, t});
            }
        } else if (r.type == "SPLINE") {
            if (auto g = spline(r)) {
                out.geometry.push_back(*g);
            } else {
                skip(r.type);
            }
        } else if (r.type != "SEQEND" && r.type != "VERTEX") {
            skip(r.type);
        }
    }
    return out;
}

std::vector<EntityId> importDrawing(Sketch& sketch, const DxfDrawing& drawing, Vec2 offset)
{
    std::vector<EntityId> created;
    for (Geometry g : drawing.geometry) {
        const auto move = [&](Vec2& p) {
            p.x += offset.x;
            p.y += offset.y;
        };
        if (auto* p = std::get_if<Point>(&g)) move(p->position);
        else if (auto* l = std::get_if<Line>(&g)) { move(l->start); move(l->end); }
        else if (auto* c = std::get_if<Circle>(&g)) move(c->center);
        else if (auto* a = std::get_if<Arc>(&g)) move(a->center);
        else if (auto* e = std::get_if<Ellipse>(&g)) move(e->center);
        else if (auto* ea = std::get_if<EllipseArc>(&g)) move(ea->ellipse.center);
        else if (auto* b = std::get_if<BSpline>(&g)) for (Vec2& pole : b->poles) move(pole);
        try {
            created.push_back(sketch.addGeometry(g));
        } catch (const std::invalid_argument&) {
            // Geometría degenerada (radio cero…): se omite.
        }
    }
    // Extremos que coinciden: se unen (así se cierran los perfiles).
    std::vector<Ref> ends;
    for (EntityId id : created) {
        for (Part part : {Part::Start, Part::End}) {
            if (sketch.point({id, part})) {
                ends.push_back({id, part});
            }
        }
    }
    std::vector<bool> joined(ends.size(), false);
    for (std::size_t i = 0; i < ends.size(); ++i) {
        for (std::size_t j = i + 1; j < ends.size(); ++j) {
            if (joined[j] || ends[i].entity == ends[j].entity) {
                continue;
            }
            const Vec2 a = *sketch.point(ends[i]);
            const Vec2 b = *sketch.point(ends[j]);
            if (std::hypot(b.x - a.x, b.y - a.y) < 1e-6) {
                sketch.addConstraint(Constraint::coincident(ends[i], ends[j]));
                joined[j] = true;
            }
        }
    }
    return created;
}

} // namespace nuf::sketch
