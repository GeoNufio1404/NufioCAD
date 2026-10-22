#pragma once

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

// Edición de un sketch: une el modelo, el solver, la herramienta activa y el
// viewport (clics, cursor, vista previa). Existe mientras se edita el sketch.
class SketchSession final : public QObject {
    Q_OBJECT

public:
    enum class Tool { None, Line, Rectangle, Circle, Arc };

    SketchSession(render::Viewport& viewport, const sketch::Plane& plane, QObject* parent = nullptr);
    ~SketchSession() override;

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
    bool hasSelection() const noexcept { return !selection_.empty() || !selectedConstraints_.empty(); }

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
};

} // namespace nuf::app
