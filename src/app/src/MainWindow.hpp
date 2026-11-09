#pragma once

#include <nuf/commands/CommandStack.hpp>
#include <nuf/document/Document.hpp>
#include <nuf/document/Features.hpp>
#include <nuf/io/DocumentFile.hpp>
#include <nuf/sketch/Plane.hpp>

#include "SketchSession.hpp"

#include <QByteArray>
#include <QMainWindow>

#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

class QAction;
class QDragEnterEvent;
class QDropEvent;
class QEvent;
class QActionGroup;
class QDockWidget;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFrame;
class QLabel;
class QMenu;

namespace nuf::render {
class Viewport;
}

namespace nuf::document {
class Generator;
}

namespace nuf::app {

class FeatureTool;
class InterfaceCheck;
class Recovery;
class Ribbon;
class StartScreen;
class NavigatorWidget;
class TimelineWidget;

// Ventana principal: reproduce la distribución del mockup (navegador a la
// izquierda, propiedades a la derecha, timeline abajo). Cada panel se
// implementa en su fase.
class MainWindow final : public QMainWindow {
    Q_OBJECT
    friend class InterfaceCheck;  // la prueba automática de la interfaz (--prueba-interfaz)

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    // Abre el archivo dado en la línea de comandos.
    void openFromCommandLine(const QString& file);
    // Si quedó una copia de una sesión que se cerró mal, ofrece restaurarla.
    void offerRecovery();

protected:
    void closeEvent(QCloseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void createMenus();
    void createDocks();
    void updateUndoActions();
    void updateTitle();

    // Archivo (MainWindowFiles.cpp).
    bool hasUnsavedWork() const;
    void setViewDirty(bool dirty);
    bool isEditing() const;
    io::ViewState currentView() const;
    void applyView(const io::ViewState& view);
    void abandonEditing();
    bool maybeSave();
    void newDocument();
    void openDocument();
    // `recovered`: es una copia de recuperación; `original` es de dónde venía.
    bool openPath(const std::filesystem::path& path, bool recovered = false, const std::filesystem::path& original = {});
    void saveRecovery();
    bool saveDocument();
    bool saveDocumentAs();
    bool writeTo(const std::filesystem::path& path);

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
    void printBodies();
    void toggleMeasure(bool on);
    void startGenerator(const document::Generator& generator);
    // Vuelve a abrir el formulario de lo que sacó un generador (la operación «id» es una de ellas).
    void reopenGenerator(document::FeatureId id);
    void updateMeasure();
    void toggleSection(bool on);
    void updateSection();
    void importStep();
    void openRecent(const QString& path);
    std::string thumbnailPng();
    void rebuildRecentMenu();
    void updateStartScreen();
    void showRecents();
    bool importStepFile(const std::filesystem::path& path);

    // Operaciones sólidas.
    void startTool(std::unique_ptr<FeatureTool> tool);
    // El modelo se recalcula en segundo plano: lo que necesita el resultado nuevo espera (avisa y devuelve true).
    bool modelBusy();
    // «Calculando…» sobre la vista mientras el modelo o la vista previa de una herramienta se calculan.
    void updateBusyBadge();
    // Ejecuta «what» ya, o en cuanto termine el cálculo en segundo plano.
    void whenComputed(std::function<void()> what);
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
    std::vector<std::function<void()>> afterCompute_;  // pendientes de que termine el cálculo en segundo plano

    render::Viewport* viewport_ = nullptr;
    QDockWidget* navigatorDock_ = nullptr;
    QDockWidget* propertiesDock_ = nullptr;
    QDockWidget* timelineDock_ = nullptr;
    QByteArray defaultLayout_; // distribución original, para "Restablecer paneles"
    QAction* undoAction_ = nullptr;
    QAction* redoAction_ = nullptr;
    QAction* filletAction_ = nullptr;
    QAction* chamferAction_ = nullptr;
    QAction* shellAction_ = nullptr;
    QAction* holeAction_ = nullptr;
    QAction* mirrorAction_ = nullptr;
    QAction* patternAction_ = nullptr;
    QAction* planeAction_ = nullptr;
    QAction* axisAction_ = nullptr;
    QAction* parametersAction_ = nullptr;
    QAction* gearAction_ = nullptr;
    QAction* threadAction_ = nullptr;
    QAction* sweepAction_ = nullptr;
    QAction* loftAction_ = nullptr;
    QAction* moveAction_ = nullptr;
    QAction* combineAction_ = nullptr;
    QAction* organizeAction_ = nullptr;
    QAction* jointAction_ = nullptr;
    QAction* measureAction_ = nullptr;
    QAction* boxAction_ = nullptr;
    QAction* cylinderAction_ = nullptr;
    std::vector<QAction*> generatorActions_;
    int toolsTab_ = -1;
    QAction* sectionAction_ = nullptr;
    QFrame* sectionPanel_ = nullptr;
    QComboBox* sectionPlane_ = nullptr;
    QDoubleSpinBox* sectionOffset_ = nullptr;
    QCheckBox* sectionFlip_ = nullptr;
    QLabel* busyBadge_ = nullptr;     // «Calculando…», sobre la vista
    bool toolComputing_ = false;      // la vista previa de la herramienta abierta se está calculando
    QLabel* measurePanel_ = nullptr;  // resultado de la medición, sobre la vista
    QAction* exportAction_ = nullptr;
    QAction* printAction_ = nullptr;
    QAction* importAction_ = nullptr;
    QMenu* recentMenu_ = nullptr;
    std::unique_ptr<Recovery> recovery_;
    StartScreen* startScreen_ = nullptr;
    bool recentsOpen_ = false;  // el panel de recientes (Archivo › Recientes…) está a la vista

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
    std::set<document::FeatureId> hiddenPlanes_;
    std::map<document::FeatureId, bool> sketchVisibility_;  // elegida a mano
    bool originVisible_ = false;
    bool groundVisible_ = true;
    bool viewDirty_ = false;          // visibilidad cambiada desde el último guardado
    std::filesystem::path path_;      // vacío = diseño sin guardar
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
