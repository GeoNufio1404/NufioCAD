#pragma once

// Herramientas de dibujo del sketch como máquinas de estados sin Qt: la UI
// les pasa el cursor ya ajustado (Snap) y los clics, y muestra preview() y
// hint(). Al confirmar, crean la geometría y sus restricciones automáticas:
// coincidente / punto medio al apoyarse en un punto existente, y horizontal
// o vertical si se dibuja casi recto.

#include <nuf/sketch/Sketch.hpp>
#include <nuf/sketch/Snap.hpp>

#include <memory>
#include <optional>
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

    // Valores que se pueden escribir junto al cursor en el paso actual
    // ("Longitud", "Ángulo"…; vacío si ninguno) y su valor según el cursor.
    // Los ángulos van en grados.
    virtual std::vector<std::string> valueFields() const { return {}; }
    virtual std::vector<double> currentValues() const { return {}; }
    // Termina el paso con los valores escritos (nullopt = tomar el del
    // cursor) y los fija con cotas. Devuelve true si cambió el sketch.
    virtual bool applyValues(Sketch& /*sketch*/, const std::vector<std::optional<double>>& /*values*/)
    {
        return false;
    }
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

// --- Variantes (Fase 2b) ---------------------------------------------------

// Rectángulo por 3 puntos: un lado (girado como se quiera) y la altura.
std::unique_ptr<SketchTool> makeThreePointRectangleTool();
// Rectángulo por el centro y una esquina.
std::unique_ptr<SketchTool> makeCenterRectangleTool();

// Círculo por los dos extremos de un diámetro.
std::unique_ptr<SketchTool> makeTwoPointCircleTool();
// Círculo por 3 puntos.
std::unique_ptr<SketchTool> makeThreePointCircleTool();
// Círculo tangente a 2 o 3 curvas: se eligen las curvas y un clic indica
// por dónde va (el solver lo ajusta).
std::unique_ptr<SketchTool> makeTangentCircleTool(int curves);

// Arco por centro, inicio y fin (el sentido lo da el movimiento del cursor).
std::unique_ptr<SketchTool> makeCenterArcTool();
// Arco tangente desde el extremo de una línea o arco.
std::unique_ptr<SketchTool> makeTangentArcTool();

// Polígono regular: inscrito (centro y vértice), circunscrito (centro y
// punto medio de un lado) o por un lado (sus dos vértices).
enum class PolygonMode { Inscribed, Circumscribed, Edge };
std::unique_ptr<SketchTool> makePolygonTool(PolygonMode mode);

// Ranura: centro a centro, por su longitud total o desde el punto central;
// el tercer clic da el ancho.
enum class SlotMode { CenterToCenter, Overall, CenterPoint };
std::unique_ptr<SketchTool> makeSlotTool(SlotMode mode);

} // namespace nuf::sketch
