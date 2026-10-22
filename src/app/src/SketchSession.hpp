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
    // sale de la herramienta. Devuelve false si no había herramienta activa.
    bool cancel();

signals:
    void toolChanged(nuf::app::SketchSession::Tool tool);
    // Indicación de la herramienta para la barra de estado (vacía sin herramienta).
    void hintChanged(const QString& hint);
    // Estado tras resolver: "4 grados de libertad", "Totalmente restringido"…
    void statusChanged(const QString& status);

private:
    void cursorMoved(bool hasPoint, double x, double y);
    void clicked(double x, double y);
    void doubleClicked();

    sketch::Snap snapAt(sketch::Vec2 point) const;
    void solveAndShow();
    void showPreview();

    render::Viewport& viewport_;
    sketch::Sketch sketch_;
    sketch::Plane plane_;
    std::unique_ptr<sketch::ISolver> solver_;
    std::unique_ptr<sketch::SketchTool> tool_;
    Tool toolKind_ = Tool::None;
    std::optional<sketch::Snap> cursor_;
};

} // namespace nuf::app
