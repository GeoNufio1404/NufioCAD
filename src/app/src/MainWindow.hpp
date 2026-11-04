#pragma once

#include <nuf/commands/CommandStack.hpp>
#include <nuf/document/Document.hpp>
#include <nuf/document/Features.hpp>
#include <nuf/sketch/Plane.hpp>

#include "SketchSession.hpp"

#include <QByteArray>
#include <QMainWindow>

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

class QAction;
class QActionGroup;
class QDockWidget;
class QLabel;
class QMenu;

namespace nuf::render {
class Viewport;
}

namespace nuf::app {

class FeatureTool;
class Ribbon;
class NavigatorWidget;
class TimelineWidget;

// Ventana principal: reproduce la distribución del mockup (navegador a la
// izquierda, propiedades a la derecha, timeline abajo). Cada panel se
// implementa en su fase.
class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void createMenus();
    void createDocks();
    void updateUndoActions();
    void updateTitle();

    void createSketch();
    void startSketch(const sketch::Plane& plane, const QString& description,
                     const document::SketchAttachment& attachment);
    // Reabre un sketch del historial (doble clic en la vista 3D o el timeline).
    void editSketch(document::FeatureId id);
    void openSession(const sketch::Plane& plane, const QString& description, std::optional<sketch::Sketch> initial);
    void finishSketch();
    void cancel();
    void clearNavigation();
    void updateSketchActions();
    void updateExportAction();
    void exportBodies();

    // Operaciones sólidas.
    void startTool(std::unique_ptr<FeatureTool> tool);
    void finishTool(bool accepted);
    // Editar una operación (doble clic en el timeline).
    void activateFeature(document::FeatureId id);
    void showFeatureMenu(document::FeatureId id, const QPoint& globalPos);

    // Muestra el modelo: cuerpos, sketches visibles, navegador y timeline.
    void refreshModel();
    // Hasta dónde se muestra el historial (al editar, lo anterior a esa operación).
    std::size_t shownUpTo() const;
    bool sketchVisible(document::FeatureId id, std::size_t upTo) const;

    document::Document document_;
    commands::CommandStack commands_{document_};

    render::Viewport* viewport_ = nullptr;
    QDockWidget* navigatorDock_ = nullptr;
    QDockWidget* propertiesDock_ = nullptr;
    QDockWidget* timelineDock_ = nullptr;
    QByteArray defaultLayout_; // distribución original, para "Restablecer paneles"
    QAction* undoAction_ = nullptr;
    QAction* redoAction_ = nullptr;
    QAction* exportAction_ = nullptr;

    // Sketch en edición (nuevo o una operación del historial).
    std::unique_ptr<SketchSession> sketch_;
    std::optional<document::FeatureId> editingSketch_;  // el que se está reeditando
    document::SketchAttachment newSketchAttachment_;   // dónde va el sketch nuevo
    // Herramienta de operación en curso (Extruir, Revolución).
    std::unique_ptr<FeatureTool> tool_;
    TimelineWidget* timeline_ = nullptr;
    NavigatorWidget* navigator_ = nullptr;
    QWidget* propertiesPlaceholder_ = nullptr;
    // Visibilidad (de la interfaz; el .nuf la guardará en la Fase 4).
    std::set<std::string> hiddenBodies_;
    std::map<document::FeatureId, bool> sketchVisibility_;  // elegida a mano
    bool originVisible_ = false;
    bool groundVisible_ = true;
    // Sketches a la vista, para rehacer solo los que cambian.
    struct ShownSketch {
        const sketch::Sketch* sketch = nullptr;
        sketch::Plane plane;
        std::string name;
    };
    std::map<document::FeatureId, ShownSketch> shownSketches_;
    std::map<SketchSession::Tool, QAction*> toolActions_;
    // Familias de herramientas (rectángulo, círculo…) para la barra.
    struct ToolFamily {
        QMenu* menu = nullptr;  // nullptr si la familia tiene una sola herramienta
        std::vector<QAction*> actions;
    };
    std::vector<ToolFamily> toolFamilies_;
    QLabel* sketchStatusLabel_ = nullptr;
    Ribbon* ribbon_ = nullptr;
    QActionGroup* navGroup_ = nullptr;
    int solidTab_ = 0;
    int sketchTab_ = 0;
    bool ribbonSketching_ = false;
    QAction* createSketchAction_ = nullptr;
    QAction* extrudeAction_ = nullptr;
    QAction* revolveAction_ = nullptr;
    QAction* finishSketchAction_ = nullptr;
    QAction* lookAtSketchAction_ = nullptr;
    QAction* deleteAction_ = nullptr;
    QAction* constructionAction_ = nullptr;
    QAction* lockAction_ = nullptr;
    QAction* dimensionAction_ = nullptr;
    QAction* intersectAction_ = nullptr;
    QAction* importDxfAction_ = nullptr;
    std::vector<QAction*> constraintActions_;
    QLabel* cursorLabel_ = nullptr;
    QLabel* modelStatusLabel_ = nullptr;  // primera operación con error o aviso
};

} // namespace nuf::app
