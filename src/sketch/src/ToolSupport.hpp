#pragma once

// Utilidades internas compartidas por las herramientas de dibujo.

#include <nuf/sketch/Sketch.hpp>
#include <nuf/sketch/Snap.hpp>

#include <optional>

namespace nuf::sketch::detail {

inline constexpr double kMinLength = 1e-6;

double distance(Vec2 a, Vec2 b);

// Une un punto recién creado con aquello a lo que se ajustó el cursor
// (coincidente, punto medio o punto sobre curva).
void attach(Sketch& sketch, Ref created, const Snap& snapped);

// Si una curva nueva pasa por un punto existente al que se ajustó el cursor
// (no es uno de sus extremos), pone ese punto sobre la curva.
void attachOnCurve(Sketch& sketch, EntityId curve, const Snap& snapped);

// Si el segmento start→end está a menos de kStraightenAngle de la horizontal
// o la vertical, lo endereza. No si el extremo se apoya en algo (moverlo
// rompería esa restricción) ni con Ctrl (snap.free).
enum class Straight { None, Horizontal, Vertical };
Straight straighten(Vec2 start, Snap& end);

// Curva (línea, círculo o arco, incluidos los ejes) sobre la que se hizo
// clic, según el snap. 0 si no hay ninguna.
EntityId curveAt(const Sketch& sketch, const Snap& snapped);

// Centro del círculo que pasa por tres puntos (nullopt si están alineados).
std::optional<Vec2> circumcenter(Vec2 a, Vec2 b, Vec2 c);

// Arco antihorario de a a b que pasa por through. Si through queda en el otro
// lado, el arco va de b a a (swapped = true). nullopt si están alineados.
struct ArcFit {
    Arc arc;
    bool swapped = false;
};
std::optional<ArcFit> fitArc(Vec2 a, Vec2 b, Vec2 through);

// Dirección (unitaria) con la que se sale de una curva por uno de sus
// extremos al continuarla: para el final de una línea, de inicio a fin; para
// su inicio, al revés; en un arco, su tangente. nullopt si ref no es el
// extremo de una línea o un arco.
std::optional<Vec2> exitDirection(const Sketch& sketch, Ref endpoint);

} // namespace nuf::sketch::detail
