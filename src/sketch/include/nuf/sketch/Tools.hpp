#pragma once

// Herramientas de dibujo del sketch como máquinas de estados sin Qt: la UI
// les pasa el cursor ya ajustado (Snap) y los clics, y muestra preview() y
// hint(). Al confirmar, crean la geometría y sus restricciones automáticas:
// coincidente / punto medio al apoyarse en un punto existente, y horizontal
// o vertical si se dibuja casi recto.

#include <nuf/sketch/Sketch.hpp>
#include <nuf/sketch/Snap.hpp>

#include <memory>
#include <string>
#include <vector>

namespace nuf::sketch {

// Geometría provisional que la UI dibuja mientras se usa la herramienta.
struct Preview {
    std::vector<Geometry> geometry;
};

class SketchTool {
public:
    virtual ~SketchTool() = default;

    // El cursor se movió.
    virtual void move(const Snap& cursor) = 0;
    // Clic: puede crear geometría. Devuelve true si cambió el sketch.
    virtual bool click(Sketch& sketch, const Snap& cursor) = 0;
    // Esc o doble clic: abandona lo que está a medias. Devuelve false si no
    // había nada a medias (la UI entonces sale de la herramienta).
    virtual bool cancel() = 0;

    virtual Preview preview() const = 0;
    // Indicación para la barra de estado ("Clic para el segundo extremo").
    virtual std::string hint() const = 0;
};

// Ángulo (radianes) por debajo del cual una línea se endereza a horizontal o
// vertical y recibe esa restricción.
inline constexpr double kStraightenAngle = 3.0 * 3.14159265358979323846 / 180.0;

// Línea: encadena segmentos (polilínea) hasta Esc, doble clic o cerrar en el
// primer punto.
std::unique_ptr<SketchTool> makeLineTool();
// Rectángulo por dos esquinas opuestas.
std::unique_ptr<SketchTool> makeRectangleTool();
// Círculo por centro y un punto del contorno.
std::unique_ptr<SketchTool> makeCircleTool();
// Arco por 3 puntos: inicio, fin y un punto por el que pasa.
std::unique_ptr<SketchTool> makeArcTool();

} // namespace nuf::sketch
