#include <nuf/sketch/Profiles.hpp>

#include <algorithm>
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

// Línea o arco del grafo.
struct Edge {
    EntityId entity = 0;
    int from = 0;  // vértice del inicio
    int to = 0;    // vértice del fin
    // Dirección con la que la curva sale de su inicio y de su fin.
    double angleAtStart = 0.0;
    double angleAtEnd = 0.0;
    std::vector<Vec2> points;  // de inicio a fin
    bool alive = true;
};

// Arista orientada: la curva recorrida en un sentido.
struct HalfEdge {
    int edge = 0;
    bool reversed = false;
    int from = 0;
    int to = 0;
    double angle = 0.0;  // dirección de salida desde «from»
};

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
        loop.edges.push_back({edges[halfEdges[h].edge].entity, halfEdges[h].reversed});
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

    std::vector<Edge> edges;
    std::vector<const Entity*> closed;  // círculos y elipses: contornos por sí solos
    for (const Entity& e : sketch.entities()) {
        if (e.reference || e.construction || std::holds_alternative<Point>(e.geometry)) {
            continue;
        }
        if (isClosedCurve(e.geometry)) {
            closed.push_back(&e);
            continue;
        }
        // Curva abierta (línea, arco, arco de elipse, spline): su polilínea
        // da los extremos y la dirección con que sale de cada uno.
        std::vector<Vec2> points = polyline(e.geometry);
        if (points.size() < 2 || distance(points.front(), points.back()) <= tolerance) {
            continue;  // degenerada o cerrada sobre sí misma
        }
        Edge edge;
        edge.entity = e.id;
        edge.from = vertexAt(points.front());
        edge.to = vertexAt(points.back());
        if (const auto* a = std::get_if<Arc>(&e.geometry)) {
            // En arcos, la tangente exacta (los primeros tramos son cortos).
            edge.angleAtStart = a->startAngle + std::numbers::pi / 2.0;
            edge.angleAtEnd = a->endAngle - std::numbers::pi / 2.0;
        } else {
            const Vec2 s1 = points[1];
            const Vec2 e1 = points[points.size() - 2];
            edge.angleAtStart = std::atan2(s1.y - points.front().y, s1.x - points.front().x);
            edge.angleAtEnd = std::atan2(e1.y - points.back().y, e1.x - points.back().x);
        }
        edge.points = std::move(points);
        edges.push_back(std::move(edge));
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
        halfEdges.push_back({int(i), false, e.from, e.to, e.angleAtStart});
        halfEdges.push_back({int(i), true, e.to, e.from, e.angleAtEnd});
    }
    std::map<int, std::vector<int>> outgoing;  // vértice → medias aristas, antihorario
    for (std::size_t h = 0; h < halfEdges.size(); ++h) {
        outgoing[halfEdges[h].from].push_back(int(h));
    }
    for (auto& [vertex, list] : outgoing) {
        std::ranges::sort(list, [&](int a, int b) { return halfEdges[a].angle < halfEdges[b].angle; });
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
        loop.edges = {{e->id, false}};
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

} // namespace nuf::sketch
