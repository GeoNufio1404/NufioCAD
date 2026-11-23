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
        Point,
        // Modificar (Fase 2c)
        Trim, Extend, Break,
        Fillet, Chamfer,
        Offset,
        Mirror, Move, Copy, Rotate, Scale,
        PatternRectangular, PatternCircular,
        Project,
        // Extras (Fase 2d)
        Text, Image,
        // Cota de un clic: clic en un círculo, arco o línea (o en dos elementos) y el valor se escribe sobre el lienzo.
        Dimension,
    };

    // «initial»: un sketch ya hecho que se vuelve a editar.
    SketchSession(render::Viewport& viewport, const sketch::Plane& plane,
                  std::optional<sketch::Sketch> initial = std::nullopt, QObject* parent = nullptr);
    ~SketchSession() override;

    // Resuelve y muestra el sketch por primera vez (después de conectar sus
    // señales, para que lleguen el estado y los grados de libertad).
    void start();
    // Vuelve a calcular y dibujar (por ejemplo, tras cambiar la unidad de las cotas).
    void refreshView() { solveAndShow(); }

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
    // Sin selección entra en el modo Cota (clic en lo que se quiere acotar). Devuelve true si creó una cota.
    bool addDimension();

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
    // Pone un texto nuevo en «at»; con «editing» (el id de la primera curva de un texto) vuelve a abrir ese texto con su
    // contenido, fuente y altura, y lo sustituye (U9).
    void placeText(sketch::Vec2 at, sketch::EntityId editing = 0);
public:
    // Edita el texto del que forma parte lo seleccionado; false si no hay un texto elegido.
    bool editSelectedText();
private:
    void placeImage(sketch::Vec2 at);
    void updateValueInput();
    // Pide un valor nuevo para la cota en un campo sobre el lienzo, junto al cursor; false si se canceló.
    bool askDimensionValue(sketch::Constraint& dimension);
    // Modo Cota: un clic en una cota existente la edita; en un círculo, arco o línea, la crea; dos clics para
    // distancias y ángulos.
    void dimensionClicked(sketch::Vec2 point);
    void editDimension(sketch::ConstraintId id);
    sketch::ConstraintId annotationAt(sketch::Vec2 point) const;

    sketch::Snap snapAt(sketch::Vec2 point) const;
    void solveAndShow();
    static bool isLengthField(const QString& label);
    std::vector<QString> valueLabels_;
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
    // Los dos últimos puntos sobre los que pasó el cursor con una herramienta
    // de dibujo: «despiertos», sirven de referencia para las alineaciones.
    std::vector<sketch::Ref> awake_;
    std::vector<sketch::EntityId> awakeLines_;  // líneas sobre las que el cursor se detuvo (referencias lejanas)
    sketch::EntityId dwellLine_ = 0;
    sketch::Ref dwellRef_;  // punto sobre el que espera el cursor para despertarlo
    std::vector<sketch::EntityId> underconstrained_;
    std::vector<sketch::Ref> selection_;
    sketch::Ref hovered_;
    std::vector<sketch::ConstraintId> selectedConstraints_;
    std::vector<sketch::ConstraintId> pinnedConstraints_;  // aplicadas por el usuario: su icono se queda visible
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
    QString textFamily_;  // fuente elegida la última vez
};

} // namespace nuf::app
