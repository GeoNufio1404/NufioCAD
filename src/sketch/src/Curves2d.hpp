#pragma once

// Geometría analítica de rectas y circunferencias (con o sin límites) para
// el snap y la edición del sketch: intersecciones exactas y proyecciones.

#include <nuf/sketch/Sketch.hpp>

#include <optional>
#include <vector>

namespace nuf::sketch::detail {

// Las intersecciones exactas solo se calculan entre rectas y circunferencias.
bool isSimpleCurve(const Entity& e);

// Curva como recta (con o sin límites) o circunferencia (con o sin arco).
struct Curve {
    bool isLine = false;
    Vec2 a, b;           // línea
    bool infinite = false;
    Vec2 center;         // círculo / arco
    double radius = 0.0;
    bool isArc = false;
    double startAngle = 0.0;
    double sweep = 0.0;
};

Curve curveOf(const Entity& e);

// ¿Está p (que ya está sobre la recta o circunferencia) dentro de la curva?
bool contains(const Curve& c, Vec2 p);

std::vector<Vec2> intersections(const Curve& p, const Curve& q);

// Punto de la curva más cercano a p (nullopt si cae fuera de un arco).
std::optional<Vec2> nearestOn(const Curve& c, Vec2 p);

} // namespace nuf::sketch::detail
