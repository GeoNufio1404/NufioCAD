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
    // Inferencias de la herramienta (perpendicular, paralela): la recta de
    // referencia a resaltar y el símbolo («⊥», «∥») junto a la esquina.
    std::vector<Guide> guides{};
    std::string badge{};
    Vec2 badgeAt{};
};

// Una línea «despierta» (el cursor se detuvo sobre ella): las herramientas de línea la usan como referencia
// lejana para inferir perpendicular o paralela, aunque el nuevo tramo no salga de ella.
// Si es un arco o un círculo, «round» lleva su círculo y «line» no se usa: las herramientas de línea infieren con él
// la tangente (T) o la perpendicular (⊥: el tramo apunta al centro).
struct AwakeLine {
    EntityId id = 0;
    Line line;
    std::optional<Circle> round{};
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
    // Doble clic o Enter en herramientas de varios puntos (splines): termina
    // la figura con lo marcado. Devuelve true si cambió el sketch.
    virtual bool finish(Sketch& /*sketch*/) { return false; }

    // Arrastre con el botón pulsado, desde «from» (donde se pulsó) hasta el
    // cursor. dragMove devuelve true si la herramienta lo usa (vista previa).
    virtual bool dragMove(const Snap& /*from*/, const Snap& /*cursor*/) { return false; }
    virtual bool dragRelease(Sketch& /*sketch*/, const Snap& /*from*/, const Snap& /*cursor*/) { return false; }

    // true cuando la herramienta ya hizo su trabajo y la UI debe salir de
    // ella (las que actúan una vez sobre la selección).
    virtual bool done() const { return false; }

    virtual Preview preview() const = 0;
    // Indicación para la barra de estado ("Clic para el segundo extremo").
    virtual std::string hint() const = 0;

    // true si el último applyValues solo cambió la figura abierta que acababa
    // de crear la herramienta (no hace falta otro paso en Deshacer).
    virtual bool amendedLast() const { return false; }

    // Las líneas despiertas actuales (con su geometría de ahora). Por defecto se ignoran.
    virtual void setAwakeLines(std::vector<AwakeLine> /*lines*/) {}

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
// Ranura en arco: arco central por 3 puntos y el ancho.
std::unique_ptr<SketchTool> makeArcSlotTool();

// Elipse por centro, extremo del eje mayor y un punto del menor; el arco de
// elipse añade dos clics para el inicio y el fin.
std::unique_ptr<SketchTool> makeEllipseTool();
std::unique_ptr<SketchTool> makeEllipseArcTool();

// Spline por puntos de paso (la curva pasa por ellos) o por polos (puntos de
// control). Se termina con doble clic o Enter.
enum class SplineMode { FitPoints, ControlPoints };
std::unique_ptr<SketchTool> makeSplineTool(SplineMode mode);

// Curva cónica: inicio, fin y el vértice del triángulo de control; rho
// (0 < rho < 1) da la forma: < 0.5 elipse, 0.5 parábola, > 0.5 hipérbola.
std::unique_ptr<SketchTool> makeConicTool();

// --- Edición (Fase 2c) --------------------------------------------------------

// Clic en una curva: recortar, extender o partir.
enum class CurveEdit { Trim, Extend, Break };
std::unique_ptr<SketchTool> makeCurveEditTool(CurveEdit edit);

// Clic en la esquina entre dos líneas: empalme (radio) o chaflán
// (distancia); el valor se escribe junto al cursor y se recuerda.
std::unique_ptr<SketchTool> makeFilletTool(bool chamfer);

// Clic en una curva (se desfasa toda su cadena) y otro clic, o un valor
// escrito, para la distancia; el lado lo da el cursor.
std::unique_ptr<SketchTool> makeOffsetTool();

// Herramientas sobre la selección (las entidades se toman al activarlas):
// simetría (clic en el eje), mover/copiar (punto base y destino), girar
// (centro y ángulo), escalar (punto base y factor) y patrones.
std::unique_ptr<SketchTool> makeMirrorTool(const Sketch& sketch, std::vector<EntityId> selection);
std::unique_ptr<SketchTool> makeMoveTool(const Sketch& sketch, std::vector<EntityId> selection, bool copy);
std::unique_ptr<SketchTool> makeRotateTool(const Sketch& sketch, std::vector<EntityId> selection);
std::unique_ptr<SketchTool> makeScaleTool(const Sketch& sketch, std::vector<EntityId> selection);
std::unique_ptr<SketchTool> makeRectangularPatternTool(const Sketch& sketch, std::vector<EntityId> selection);
std::unique_ptr<SketchTool> makeCircularPatternTool(const Sketch& sketch, std::vector<EntityId> selection);

// Interpolación: B-spline sujeta (grado ≤ 3, nudos uniformes) que pasa por
// los puntos dados (al menos 2).
BSpline interpolatingSpline(const std::vector<Vec2>& points);

} // namespace nuf::sketch
