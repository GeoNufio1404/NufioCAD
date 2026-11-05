#include <nuf/sketch/Profiles.hpp>

#include "Curves2d.hpp"

#include <algorithm>
#include <functional>
#include <type_traits>
#include <variant>
#include <cmath>
#include <map>
#include <numbers>
#include <optional>

namespace nuf::sketch {

namespace {

constexpr double kMinArea = 1e-9;

double distance(Vec2 a, Vec2 b) { return std::hypot(b.x - a.x, b.y - a.y); }

double signedArea(const std::vector<Vec2>& polygon)
{
    double twice = 0.0;
    for (std::size_t i = 0; i < polygon.size(); ++i) {
        const Vec2& a = polygon[i];
        const Vec2& b = polygon[(i + 1) % polygon.size()];
        twice += a.x * b.y - b.x * a.y;
    }
    return 0.5 * twice;
}

bool inside(const std::vector<Vec2>& polygon, Vec2 p)
{
    bool in = false;
    for (std::size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++) {
        const Vec2& a = polygon[i];
        const Vec2& b = polygon[j];
        if ((a.y > p.y) != (b.y > p.y) && p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x) {
            in = !in;
        }
    }
    return in;
}

// Posición de p a lo largo de la polilínea: índice del tramo + fracción.
double positionOn(const std::vector<Vec2>& points, Vec2 p)
{
    double best = 0.0;
    double bestDistance = 1e300;
    for (std::size_t i = 0; i + 1 < points.size(); ++i) {
        const Vec2 a = points[i];
        const Vec2 b = points[i + 1];
        const double len2 = (b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y);
        double t = len2 > 0.0 ? ((p.x - a.x) * (b.x - a.x) + (p.y - a.y) * (b.y - a.y)) / len2 : 0.0;
        t = std::clamp(t, 0.0, 1.0);
        const double d = distance({a.x + t * (b.x - a.x), a.y + t * (b.y - a.y)}, p);
        if (d < bestDistance) {
            bestDistance = d;
            best = double(i) + t;
        }
    }
    return best;
}

// Cruces entre dos polilíneas (segmento a segmento).
std::vector<Vec2> polylineCrossings(const std::vector<Vec2>& p, const std::vector<Vec2>& q, double tolerance)
{
    std::vector<Vec2> result;
    for (std::size_t i = 0; i + 1 < p.size(); ++i) {
        for (std::size_t j = 0; j + 1 < q.size(); ++j) {
            const Vec2 d1{p[i + 1].x - p[i].x, p[i + 1].y - p[i].y};
            const Vec2 d2{q[j + 1].x - q[j].x, q[j + 1].y - q[j].y};
            const double denom = d1.x * d2.y - d1.y * d2.x;
            if (std::abs(denom) < 1e-14) {
                continue;
            }
            const double s = ((q[j].x - p[i].x) * d2.y - (q[j].y - p[i].y) * d2.x) / denom;
            const double t = ((q[j].x - p[i].x) * d1.y - (q[j].y - p[i].y) * d1.x) / denom;
            if (s < -1e-9 || s > 1.0 + 1e-9 || t < -1e-9 || t > 1.0 + 1e-9) {
                continue;
            }
            const Vec2 x{p[i].x + s * d1.x, p[i].y + s * d1.y};
            if (std::ranges::none_of(result, [&](Vec2 r) { return distance(r, x) <= tolerance; })) {
                result.push_back(x);
            }
        }
    }
    return result;
}

// Dirección con la que el tramo sale de su inicio (atStart) o de su fin
// (hacia dentro del tramo). En arcos y círculos, la tangente exacta.
double tangentAngle(const Geometry& geometry, const std::vector<Vec2>& points, bool atStart)
{
    const Vec2 p = atStart ? points.front() : points.back();
    std::optional<Vec2> center;
    if (const auto* a = std::get_if<Arc>(&geometry)) {
        center = a->center;
    } else if (const auto* c = std::get_if<Circle>(&geometry)) {
        center = c->center;
    }
    if (center) {
        // Las curvas van en sentido antihorario.
        const double radial = std::atan2(p.y - center->y, p.x - center->x);
        return atStart ? radial + std::numbers::pi / 2.0 : radial - std::numbers::pi / 2.0;
    }
    const Vec2 q = atStart ? points[1] : points[points.size() - 2];
    return std::atan2(q.y - p.y, q.x - p.x);
}

// Línea o arco del grafo.
struct Edge {
    EntityId entity = 0;
    int from = 0;  // vértice del inicio
    int to = 0;    // vértice del fin
    // Dirección con la que la curva sale de su inicio y de su fin.
    double angleAtStart = 0.0;
    double angleAtEnd = 0.0;
    std::vector<Vec2> points;  // de inicio a fin
    std::optional<Trim> trim;  // si es solo un tramo de la curva
    bool alive = true;
};

// Arista orientada: la curva recorrida en un sentido.
struct HalfEdge {
    int edge = 0;
    bool reversed = false;
    int from = 0;
    int to = 0;
    double angle = 0.0;  // dirección de salida desde «from», en [0, 2π)
    // Dirección hacia un punto más adentro de la curva: desempata curvas
    // tangentes en el vértice (un círculo tangente a una recta).
    double farAngle = 0.0;
};

double normalized(double angle)
{
    constexpr double kTwoPi = 2.0 * std::numbers::pi;
    angle = std::fmod(angle, kTwoPi);
    return angle < 0.0 ? angle + kTwoPi : angle;
}

// Diferencia a - b en (-π, π].
double angleDifference(double a, double b)
{
    const double d = normalized(a - b);
    return d > std::numbers::pi ? d - 2.0 * std::numbers::pi : d;
}

struct Cycle {
    std::vector<int> halfEdges;
    std::vector<Vec2> polygon;
    double area = 0.0;  // con signo: positiva = antihoraria = cara acotada
    int component = 0;
};

Loop toLoop(const Cycle& cycle, const std::vector<HalfEdge>& halfEdges, const std::vector<Edge>& edges,
            bool reverse)
{
    Loop loop;
    loop.polygon = cycle.polygon;
    loop.area = std::abs(cycle.area);
    for (int h : cycle.halfEdges) {
        const Edge& e = edges[halfEdges[h].edge];
        loop.edges.push_back({e.entity, halfEdges[h].reversed, e.trim});
    }
    if (reverse) {
        std::ranges::reverse(loop.edges);
        for (ProfileEdge& e : loop.edges) {
            e.reversed = !e.reversed;
        }
        std::ranges::reverse(loop.polygon);
    }
    return loop;
}

} // namespace

std::vector<Profile> findProfiles(const Sketch& sketch, double tolerance)
{
    // --- Vértices y aristas -------------------------------------------------
    std::vector<Vec2> vertices;
    const auto vertexAt = [&](Vec2 p) {
        for (std::size_t i = 0; i < vertices.size(); ++i) {
            if (distance(vertices[i], p) <= tolerance) {
                return int(i);
            }
        }
        vertices.push_back(p);
        return int(vertices.size() - 1);
    };

    // Curvas que cuentan (sin construcción, referencias ni puntos).
    struct Source {
        const Entity* entity;
        std::vector<Vec2> points;  // polilínea; cerrada: el último repite el primero
        bool closed;
        std::vector<double> cuts;  // posiciones (en la polilínea) donde se parte
    };
    std::vector<Source> sources;
    // Extremo de una curva abierta junto a p (a menos del 2 % de su largo).
    const auto nearEndpoint = [](const Source& src, Vec2 p, double tol) -> std::optional<Vec2> {
        if (src.closed) {
            return std::nullopt;
        }
        double length = 0.0;
        for (std::size_t k = 0; k + 1 < src.points.size(); ++k) {
            length += distance(src.points[k], src.points[k + 1]);
        }
        const double near = std::max(tol, 0.02 * length);
        for (const Vec2& end : {src.points.front(), src.points.back()}) {
            if (distance(end, p) <= near) {
                return end;
            }
        }
        return std::nullopt;
    };
    for (const Entity& e : sketch.entities()) {
        if (e.reference || e.construction || std::holds_alternative<Point>(e.geometry)) {
            continue;
        }
        std::vector<Vec2> points = polyline(e.geometry);
        const bool closed = isClosedCurve(e.geometry);
        if (points.size() < 2 || (!closed && distance(points.front(), points.back()) <= tolerance)) {
            continue;  // degenerada o cerrada sobre sí misma
        }
        sources.push_back({&e, std::move(points), closed, {}});
    }

    // Cruces entre curvas: exactos entre rectas y circunferencias; con la
    // polilínea en el resto. Cada cruce parte las dos curvas en el mismo punto.
    std::vector<std::vector<std::pair<double, Vec2>>> splits(sources.size());
    for (std::size_t i = 0; i < sources.size(); ++i) {
        for (std::size_t j = i + 1; j < sources.size(); ++j) {
            std::vector<Vec2> crossings;
            if (detail::isSimpleCurve(*sources[i].entity) && detail::isSimpleCurve(*sources[j].entity)) {
                crossings = detail::intersections(detail::curveOf(*sources[i].entity),
                                                  detail::curveOf(*sources[j].entity));
            } else {
                // Aproximados: junto a una unión de las dos curvas no son
                // cruces (dos tramos vecinos de una letra se «tocan» ahí), y
                // junto a un extremo de una se ajustan a él.
                for (Vec2 p : polylineCrossings(sources[i].points, sources[j].points, tolerance)) {
                    const auto endA = nearEndpoint(sources[i], p, tolerance);
                    const auto endB = nearEndpoint(sources[j], p, tolerance);
                    if (endA && endB) {
                        continue;
                    }
                    crossings.push_back(endA ? *endA : endB ? *endB : p);
                }
            }
            for (const Vec2& p : crossings) {
                splits[i].emplace_back(positionOn(sources[i].points, p), p);
                splits[j].emplace_back(positionOn(sources[j].points, p), p);
            }
        }
    }

    std::vector<Edge> edges;
    std::vector<const Entity*> closed;  // círculos y elipses sin cruces: contornos por sí solos
    for (std::size_t i = 0; i < sources.size(); ++i) {
        const Source& src = sources[i];
        const Entity& e = *src.entity;
        auto& cuts = splits[i];
        std::ranges::sort(cuts, [](const auto& a, const auto& b) { return a.first < b.first; });
        // Sin repetir (un cruce en un extremo o dos curvas por el mismo punto).
        std::vector<std::pair<double, Vec2>> unique;
        for (const auto& c : cuts) {
            if (unique.empty() || distance(unique.back().second, c.second) > tolerance) {
                unique.push_back(c);
            }
        }
        if (src.closed && unique.size() >= 2 && distance(unique.front().second, unique.back().second) <= tolerance) {
            unique.pop_back();
        }
        if (src.closed && unique.size() < 2) {
            closed.push_back(&e);  // un solo contacto no la divide
            continue;
        }

        // Tramos: entre cortes consecutivos (y los extremos, si es abierta).
        std::vector<std::pair<double, Vec2>> marks;
        const double last = double(src.points.size() - 1);
        if (!src.closed) {
            marks.emplace_back(0.0, src.points.front());
        }
        for (const auto& c : unique) {
            if (src.closed || (distance(c.second, src.points.front()) > tolerance
                               && distance(c.second, src.points.back()) > tolerance)) {
                marks.push_back(c);
            }
        }
        if (src.closed) {
            marks.emplace_back(marks.front().first + last, marks.front().second);  // vuelta completa
        } else {
            marks.emplace_back(last, src.points.back());
        }
        const bool whole = marks.size() == 2 && !src.closed;
        for (std::size_t k = 0; k + 1 < marks.size(); ++k) {
            const auto [t0, p0] = marks[k];
            const auto [t1, p1] = marks[k + 1];
            if (distance(p0, p1) <= tolerance && t1 - t0 < 1.0) {
                continue;  // tramo nulo
            }
            Edge edge;
            edge.entity = e.id;
            edge.from = vertexAt(p0);
            edge.to = vertexAt(p1);
            edge.points.push_back(p0);
            for (double t = std::floor(t0) + 1.0; t < t1 - 1e-9; t += 1.0) {
                edge.points.push_back(src.points[std::size_t(std::fmod(t, last))]);
            }
            edge.points.push_back(p1);
            edge.angleAtStart = tangentAngle(e.geometry, edge.points, true);
            edge.angleAtEnd = tangentAngle(e.geometry, edge.points, false);
            if (!whole) {
                edge.trim = Trim{p0, p1};
            }
            edges.push_back(std::move(edge));
        }
    }

    // Quita las aristas sueltas (con un extremo que no se une a nada): no
    // pueden cerrar ninguna región.
    std::vector<int> degree(vertices.size(), 0);
    for (const Edge& e : edges) {
        ++degree[e.from];
        ++degree[e.to];
    }
    for (bool changed = true; changed;) {
        changed = false;
        for (Edge& e : edges) {
            if (e.alive && (degree[e.from] < 2 || degree[e.to] < 2)) {
                e.alive = false;
                --degree[e.from];
                --degree[e.to];
                changed = true;
            }
        }
    }

    // --- Componentes conexas (union-find sobre vértices) ---------------------
    std::vector<int> parent(vertices.size());
    for (std::size_t i = 0; i < parent.size(); ++i) {
        parent[i] = int(i);
    }
    const auto find = [&](int v) {
        while (parent[v] != v) {
            v = parent[v] = parent[parent[v]];
        }
        return v;
    };
    for (const Edge& e : edges) {
        if (e.alive) {
            parent[find(e.from)] = find(e.to);
        }
    }

    // --- Medias aristas ordenadas por ángulo en cada vértice ------------------
    std::vector<HalfEdge> halfEdges;
    for (std::size_t i = 0; i < edges.size(); ++i) {
        const Edge& e = edges[i];
        if (!e.alive) {
            continue;
        }
        const auto towards = [](Vec2 from, Vec2 to) { return std::atan2(to.y - from.y, to.x - from.x); };
        // Un punto a un cuarto del recorrido desde cada extremo.
        const Vec2 nearStart = e.points[std::max<std::size_t>(1, e.points.size() / 4)];
        const Vec2 nearEnd = e.points[std::min(e.points.size() - 2, e.points.size() - 1 - e.points.size() / 4)];
        halfEdges.push_back({int(i), false, e.from, e.to, normalized(e.angleAtStart),
                             towards(e.points.front(), nearStart)});
        halfEdges.push_back({int(i), true, e.to, e.from, normalized(e.angleAtEnd), towards(e.points.back(), nearEnd)});
    }
    std::map<int, std::vector<int>> outgoing;  // vértice → medias aristas, antihorario
    for (std::size_t h = 0; h < halfEdges.size(); ++h) {
        outgoing[halfEdges[h].from].push_back(int(h));
    }
    for (auto& [vertex, list] : outgoing) {
        std::ranges::sort(list, [&](int a, int b) {
            const HalfEdge& p = halfEdges[a];
            const HalfEdge& q = halfEdges[b];
            if (std::abs(angleDifference(p.angle, q.angle)) > 1e-6) {
                return p.angle < q.angle;
            }
            // Tangentes: primero la que gira hacia la derecha (horario).
            return angleDifference(p.farAngle, q.farAngle) < 0.0;
        });
    }
    // La gemela de h es la otra media arista de la misma curva (índice par/impar).
    const auto twin = [](int h) { return h ^ 1; };
    // Siguiente media arista de la cara que queda a la izquierda de h: en el
    // vértice de llegada, la anterior a la gemela en orden antihorario.
    const auto next = [&](int h) {
        const std::vector<int>& list = outgoing[halfEdges[h].to];
        const auto it = std::ranges::find(list, twin(h));
        const std::size_t i = std::size_t(it - list.begin());
        return list[(i + list.size() - 1) % list.size()];
    };

    // --- Ciclos (caras del grafo) ----------------------------------------------
    std::vector<Cycle> cycles;
    std::vector<bool> used(halfEdges.size(), false);
    for (std::size_t start = 0; start < halfEdges.size(); ++start) {
        if (used[start]) {
            continue;
        }
        Cycle cycle;
        int h = int(start);
        while (!used[h]) {
            used[h] = true;
            cycle.halfEdges.push_back(h);
            const Edge& e = edges[halfEdges[h].edge];
            std::vector<Vec2> pts = e.points;
            if (halfEdges[h].reversed) {
                std::ranges::reverse(pts);
            }
            cycle.polygon.insert(cycle.polygon.end(), pts.begin(), pts.end() - 1);
            h = next(h);
        }
        cycle.area = signedArea(cycle.polygon);
        cycle.component = find(halfEdges[start].from);
        if (std::abs(cycle.area) > kMinArea) {
            cycles.push_back(std::move(cycle));
        }
    }

    // Caras acotadas (antihorarias) y contorno exterior de cada componente
    // (el ciclo horario), que es lo que hace de agujero si queda dentro de otra.
    struct Region {
        Loop loop;
        int component;
    };
    std::vector<Region> faces;
    std::map<int, Loop> boundaries;
    for (const Cycle& cycle : cycles) {
        if (cycle.area > 0.0) {
            faces.push_back({toLoop(cycle, halfEdges, edges, false), cycle.component});
        } else {
            boundaries[cycle.component] = toLoop(cycle, halfEdges, edges, true);
        }
    }
    // Cada círculo o elipse es una componente propia: cara y contorno a la vez.
    int nextComponent = int(vertices.size());
    for (const Entity* e : closed) {
        Loop loop;
        loop.edges = {{e->id, false, std::nullopt}};
        loop.polygon = polyline(e->geometry);
        loop.polygon.pop_back();
        loop.area = std::abs(signedArea(loop.polygon));
        if (const auto* c = std::get_if<Circle>(&e->geometry)) {
            loop.area = std::numbers::pi * c->radius * c->radius;
        } else if (const auto* el = std::get_if<Ellipse>(&e->geometry)) {
            loop.area = std::numbers::pi * el->majorRadius * el->minorRadius;
        }
        boundaries[nextComponent] = loop;
        faces.push_back({std::move(loop), nextComponent});
        ++nextComponent;
    }

    // --- Agujeros: componentes dentro de una cara de otra componente ----------
    std::vector<Profile> profiles;
    for (const Region& face : faces) {
        Profile profile;
        profile.outer = face.loop;
        profile.area = face.loop.area;
        profiles.push_back(std::move(profile));
    }
    for (const auto& [component, boundary] : boundaries) {
        // La cara más pequeña (de otra componente) que contiene este contorno.
        std::optional<std::size_t> container;
        for (std::size_t i = 0; i < faces.size(); ++i) {
            if (faces[i].component == component || faces[i].loop.area <= boundary.area
                || !inside(faces[i].loop.polygon, boundary.polygon.front())) {
                continue;
            }
            if (!container || faces[i].loop.area < faces[*container].loop.area) {
                container = i;
            }
        }
        if (container) {
            profiles[*container].holes.push_back(boundary);
            profiles[*container].area -= boundary.area;
        }
    }
    return profiles;
}

std::size_t geometryFingerprint(const Sketch& sketch)
{
    std::size_t seed = sketch.entities().size();
    const auto mix = [&](std::size_t value) {
        seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
    };
    const auto number = [&](double v) { mix(std::hash<double>{}(v)); };
    const auto point = [&](const Vec2& p) {
        number(p.x);
        number(p.y);
    };
    const auto ellipse = [&](const Ellipse& e) {
        point(e.center);
        number(e.majorRadius);
        number(e.minorRadius);
        number(e.rotation);
    };
    for (const Entity& e : sketch.entities()) {
        mix(e.id);
        mix(e.construction ? 1 : 0);
        mix(e.geometry.index());
        std::visit(
            [&](const auto& g) {
                using T = std::decay_t<decltype(g)>;
                if constexpr (std::is_same_v<T, Point>) {
                    point(g.position);
                } else if constexpr (std::is_same_v<T, Line>) {
                    point(g.start);
                    point(g.end);
                } else if constexpr (std::is_same_v<T, Circle>) {
                    point(g.center);
                    number(g.radius);
                } else if constexpr (std::is_same_v<T, Arc>) {
                    point(g.center);
                    number(g.radius);
                    number(g.startAngle);
                    number(g.endAngle);
                } else if constexpr (std::is_same_v<T, Ellipse>) {
                    ellipse(g);
                } else if constexpr (std::is_same_v<T, EllipseArc>) {
                    ellipse(g.ellipse);
                    number(g.startParam);
                    number(g.endParam);
                } else if constexpr (std::is_same_v<T, BSpline>) {
                    for (const Vec2& pole : g.poles) {
                        point(pole);
                    }
                    for (double w : g.weights) {
                        number(w);
                    }
                    mix(std::size_t(g.degree));
                }
            },
            e.geometry);
    }
    return seed;
}

} // namespace nuf::sketch
