#pragma once

#include <nuf/geometry/Shape.hpp>
#include <nuf/sketch/Plane.hpp>
#include <nuf/sketch/Sketch.hpp>
#include <nuf/sketch/Snap.hpp>
#include <nuf/sketch/Solver.hpp>
#include <nuf/sketch/Tools.hpp>

#include <QObject>
#include <QString>

#include <memory>
#include <optional>
#include <vector>

namespace nuf::render {
class Viewport;
}

namespace nuf::app {

class ValueInput;

// Edición de un sketch: une el modelo, el solver, la herramienta activa y el
// viewport (clics, cursor, vista previa). Existe mientras se edita el sketch.
class SketchSession final : public QObject {
    Q_OBJECT

public:
    enum class Tool {
        None,
        Line,
        Rectangle, Rectangle3Point, RectangleCenter,
        Circle, Circle2Point, Circle3Point, CircleTangent2, CircleTangent3,
        Arc, ArcCenter, ArcTangent,
        PolygonCircumscribed, PolygonInscribed, PolygonEdge,
        SlotCenterToCenter, SlotOverall, SlotCenterPoint, SlotArc,
        Ellipse, EllipseArc,
        SplineFit, SplineControl,
        Conic,
        // Modificar (Fase 2c)
        Trim, Extend, Break,
        Fillet, Chamfer,
        Offset,
        Mirror, Move, Copy, Rotate, Scale,
        PatternRectangular, PatternCircular,
        Project,
        // Extras (Fase 2d)
        Text, Image,
    };

    // «initial»: un sketch ya hecho que se vuelve a editar.
    SketchSession(render::Viewport& viewport, const sketch::Plane& plane,
                  std::optional<sketch::Sketch> initial = std::nullopt, QObject* parent = nullptr);
    ~SketchSession() override;

    // Resuelve y muestra el sketch por primera vez (después de conectar sus
    // señales, para que lleguen el estado y los grados de libertad).
    void start();

    const sketch::Sketch& sketch() const noexcept { return sketch_; }
    const sketch::Plane& plane() const noexcept { return plane_; }

    void setTool(Tool tool);
    Tool tool() const noexcept { return toolKind_; }

    // Esc: abandona lo que la herramienta tenga a medias o, si no hay nada,
    // sale de la herramienta; sin herramienta, vacía la selección. Devuelve
    // false si no había nada que cancelar.
    bool cancel();

    // Edición de la selección (clic y Shift+clic sin herramienta activa;
    // también se seleccionan los iconos de restricciones y las cotas).
    void deleteSelection();
    void toggleConstruction();
    // Bloquear/desbloquear: el solver no mueve lo bloqueado.
    void toggleLock();
    bool hasSelection() const noexcept { return !selection_.empty() || !selectedConstraints_.empty(); }

    // Intersección de la pieza con el plano del sketch: la añade como
    // geometría fija.
    void intersectModel();
    // Importa un DXF al sketch (en sus coordenadas).
    void importDxf(const QString& path);

    // Aplica una restricción geométrica a la selección. Si no encaja con la
    // selección, entra en conflicto o ya se cumple, no cambia nada y lo
    // explica con message().
    void applyConstraint(sketch::ConstraintType type);
    // Acota la selección: pide el valor (propone el actual). Si la medida ya
    // está determinada por otras restricciones, la deja como cota de referencia.
    void addDimension();

    // Deshacer/rehacer dentro del sketch.
    bool canUndo() const noexcept { return !undo_.empty(); }
    bool canRedo() const noexcept { return !redo_.empty(); }
    void undo();
    void redo();

signals:
    void toolChanged(nuf::app::SketchSession::Tool tool);
    // Indicación de la herramienta para la barra de estado (vacía sin herramienta).
    void hintChanged(const QString& hint);
    // Estado tras resolver: "4 grados de libertad", "Totalmente restringido"…
    void statusChanged(const QString& status);
    void historyChanged();
    void selectionChanged();
    // Aviso breve para la barra de estado.
    void message(const QString& text);

private:
    void cursorMoved(bool hasPoint, double x, double y);
    void clicked(double x, double y, Qt::KeyboardModifiers modifiers);
    void doubleClicked(double x, double y);
    // Arrastrar geometría sin herramienta activa.
    void pressed(double x, double y);
    void dragged(double x, double y);
    void dragFinished();
    // Valores escritos junto al cursor.
    void typed(const QString& text);
    // Terminar la figura en curso (splines): doble clic o Enter.
    void finishShape();
    void toolDragged(double x0, double y0, double x, double y);
    void toolDragReleased(double x0, double y0, double x, double y);
    void applyTypedValues();
    // Tras un cambio hecho por la herramienta: vista previa, indicación y,
    // si ya terminó, salir de ella.
    void afterToolChange();
    // Añade geometría que viene del modelo (proyección, intersección) fija.
    void addFromModel(const std::vector<sketch::Geometry>& geometry, const geometry::ElementRef& source);
    void boxSelected(double x0, double y0, double x1, double y1);
    // Texto e imagen: piden datos con diálogos al hacer clic.
    void placeText(sketch::Vec2 at);
    void placeImage(sketch::Vec2 at);
    void updateValueInput();
    // Pide un valor nuevo para la cota; false si se canceló.
    bool askDimensionValue(sketch::Constraint& dimension);
    void editDimension(sketch::ConstraintId id);
    sketch::ConstraintId annotationAt(sketch::Vec2 point) const;

    sketch::Snap snapAt(sketch::Vec2 point) const;
    void solveAndShow();
    void showSketch();
    void showPreview();
    // Guarda el estado actual para deshacer (antes de cambiar el sketch).
    void remember(sketch::Sketch before);
    // Quita de la selección lo que ya no existe.
    void pruneSelection();
    std::vector<sketch::EntityId> selectedEntities() const;

    render::Viewport& viewport_;
    sketch::Sketch sketch_;
    sketch::Plane plane_;
    std::unique_ptr<sketch::ISolver> solver_;
    std::unique_ptr<sketch::SketchTool> tool_;
    Tool toolKind_ = Tool::None;
    std::optional<sketch::Snap> cursor_;
    std::vector<sketch::EntityId> underconstrained_;
    std::vector<sketch::Ref> selection_;
    sketch::Ref hovered_;
    std::vector<sketch::ConstraintId> selectedConstraints_;
    sketch::ConstraintId hoveredConstraint_ = 0;
    std::vector<sketch::Sketch> undo_;
    std::vector<sketch::Sketch> redo_;

    // Arrastre en curso: qué se agarró, dónde y cómo estaba el sketch.
    struct Drag {
        sketch::Ref what;
        sketch::Vec2 grabbedAt;
        sketch::Sketch before;
        bool moved = false;
    };
    std::optional<Drag> drag_;
    ValueInput* valueInput_ = nullptr;  // hijo del viewport
    double textHeight_ = 10.0;
};

} // namespace nuf::app
