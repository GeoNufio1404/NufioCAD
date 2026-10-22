#pragma once

#include <nuf/commands/CommandStack.hpp>
#include <nuf/document/Document.hpp>
#include <nuf/sketch/Plane.hpp>

#include "SketchSession.hpp"

#include <QByteArray>
#include <QMainWindow>

#include <map>
#include <memory>
#include <vector>

class QAction;
class QDockWidget;
class QLabel;
class QMenu;

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
    std::unique_ptr<SketchSession> sketch_;
    std::map<SketchSession::Tool, QAction*> toolActions_;
    // Familias de herramientas (rectángulo, círculo…) para la barra.
    struct ToolFamily {
        QMenu* menu = nullptr;  // nullptr si la familia tiene una sola herramienta
        std::vector<QAction*> actions;
    };
    std::vector<ToolFamily> toolFamilies_;
    QLabel* sketchStatusLabel_ = nullptr;
    QAction* createSketchAction_ = nullptr;
    QAction* finishSketchAction_ = nullptr;
    QAction* lookAtSketchAction_ = nullptr;
    QAction* deleteAction_ = nullptr;
    QAction* constructionAction_ = nullptr;
    QAction* dimensionAction_ = nullptr;
    std::vector<QAction*> constraintActions_;
    QLabel* cursorLabel_ = nullptr;
};

} // namespace nuf::app
