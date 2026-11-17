#pragma once

// Utilidades internas compartidas por las herramientas de dibujo.

#include <nuf/sketch/Sketch.hpp>
#include <nuf/sketch/Snap.hpp>

#include <optional>
#include <vector>

namespace nuf::sketch::detail {

inline constexpr double kMinLength = 1e-6;

double distance(Vec2 a, Vec2 b);

// Une un punto recién creado con aquello a lo que se ajustó el cursor
// (coincidente, punto medio o punto sobre curva).
void attach(Sketch& sketch, Ref created, const Snap& snapped);

// Figura «abierta»: tras el último clic la figura ya existe en el sketch pero
// la herramienta sigue enseñando sus campos (lados, ancho, diámetro…) para
// cambiarlos escribiendo; se cierra con un clic nuevo, Esc o Enter sin valores.
// Guarda lo creado (para rehacerlo con otros valores) y los dos clics.
struct OpenFigure {
    std::vector<EntityId> ids;
    Snap first;
    Snap second;
    Snap third;        // tercer clic, en las figuras de tres (rectángulo por 3 puntos)
    double value = 0.0;  // medida auxiliar (radio de la ranura)
    std::vector<std::optional<double>> typed;  // medidas ya fijadas con cotas
};

// Entidades añadidas al sketch desde que tenía «mark» entidades.
std::vector<EntityId> entitiesSince(const Sketch& sketch, std::size_t mark);

// Borra esas entidades (con sus restricciones). false, sin tocar nada, si
// alguna ya no existe (por ejemplo tras deshacer).
bool discardEntities(Sketch& sketch, const std::vector<EntityId>& ids);

// Si una curva nueva pasa por un punto existente al que se ajustó el cursor
// (no es uno de sus extremos), pone ese punto sobre la curva.
void attachOnCurve(Sketch& sketch, EntityId curve, const Snap& snapped);

// Si el segmento start→end está a menos de kStraightenAngle de la horizontal
// o la vertical, lo endereza. No si el extremo se apoya en algo (moverlo
// rompería esa restricción) ni con Ctrl (snap.free).
enum class Straight { None, Horizontal, Vertical };
Straight straighten(Vec2 start, Snap& end);

// Relación inferida de un segmento nuevo con una línea de referencia: si el
// segmento start→end queda a menos de kStraightenAngle de la paralela o de la
// perpendicular a «reference», lo endereza (conserva el largo proyectado) y la
// devuelve. No si el extremo se apoya en algo, está alineado o es libre.
enum class Relation { None, Parallel, Perpendicular, Tangent };
Relation relate(Vec2 start, const Line& reference, Snap& end);

// Lo mismo con un arco o un círculo lejano: Tangent si el segmento queda a menos de kStraightenAngle de una de las
// dos tangentes que salen de «start»; Perpendicular si apunta al centro (o se aleja de él, en la misma recta).
// Con «start» dentro del círculo no hay tangente.
Relation relateRound(Vec2 start, const Circle& round, Snap& end);

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

// Arco que sale de «start» en la dirección «direction» (tangente) y llega a
// «end». nullopt si end está sobre esa tangente (sería una recta).
std::optional<ArcFit> tangentArc(Vec2 start, Vec2 direction, Vec2 end);

// Dirección (unitaria) con la que se sale de una curva por uno de sus
// extremos al continuarla: para el final de una línea, de inicio a fin; para
// su inicio, al revés; en un arco, su tangente. nullopt si ref no es el
// extremo de una línea o un arco.
std::optional<Vec2> exitDirection(const Sketch& sketch, Ref endpoint);

} // namespace nuf::sketch::detail
