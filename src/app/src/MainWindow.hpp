#pragma once

#include <nuf/commands/CommandStack.hpp>
#include <nuf/document/Document.hpp>
#include <nuf/sketch/Plane.hpp>
#include <nuf/sketch/Sketch.hpp>

#include <QByteArray>
#include <QMainWindow>

#include <optional>

class QAction;
class QDockWidget;
class QLabel;

namespace nuf::render {
class Viewport;
}

namespace nuf::app {

// Ventana principal: reproduce la distribución del mockup (navegador a la
// izquierda, propiedades a la derecha, timeline abajo). Cada panel se
// implementa en su fase.
class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void createMenus();
    void createDocks();
    void updateUndoActions();
    void updateTitle();

    void createSketch();
    void startSketch(const sketch::Plane& plane, const QString& description);
    void finishSketch();
    void cancel();
    void updateSketchActions();

    document::Document document_;
    commands::CommandStack commands_{document_};

    render::Viewport* viewport_ = nullptr;
    QDockWidget* navigatorDock_ = nullptr;
    QDockWidget* propertiesDock_ = nullptr;
    QDockWidget* timelineDock_ = nullptr;
    QByteArray defaultLayout_; // distribución original, para "Restablecer paneles"
    QAction* undoAction_ = nullptr;
    QAction* redoAction_ = nullptr;

    // Sketch en edición. En la Fase 3 pasa a ser una operación del documento.
    std::optional<sketch::Sketch> sketch_;
    sketch::Plane sketchPlane_;
    QString sketchPlaneName_;
    QAction* createSketchAction_ = nullptr;
    QAction* finishSketchAction_ = nullptr;
    QAction* lookAtSketchAction_ = nullptr;
    QLabel* cursorLabel_ = nullptr;
};

} // namespace nuf::app
