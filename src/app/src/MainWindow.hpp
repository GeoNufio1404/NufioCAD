#pragma once

#include <nuf/commands/CommandStack.hpp>
#include <nuf/document/Document.hpp>
#include <nuf/document/Features.hpp>
#include <nuf/geometry/Exchange.hpp>
#include <nuf/io/DocumentFile.hpp>
#include <nuf/sketch/Plane.hpp>

#include "BackgroundRun.hpp"
#include "ExportDialog.hpp"
#include "SketchSession.hpp"

#include <QByteArray>
#include <QMainWindow>

#include <cstdint>
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
class QColor;
class QVariantAnimation;
class QComboBox;
class QFrame;
class QDockWidget;
class QToolButton;
class QTimer;
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

class BusySpinner;
class FeatureTool;
class StatusBanner;
class InterfaceCheck;
class Recovery;
class Ribbon;
class StartScreen;
class RadialMenu;
class ValueInput;
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
    // Pliega solo el navegador y la línea de tiempo, y compacta la cinta, según el tamaño de la ventana (Fase 20, R4).
    // Es pública para poder llamarla antes del primer show(): así la ventana no crece por lo que se va a plegar.
    void adaptToSize();

protected:
    void closeEvent(QCloseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void createMenus();
    void createDocks();
    // Fase 20, R4: en ventanas angostas o bajas el navegador y la línea de tiempo se pliegan solos y la cinta se compacta;
    // al agrandar la ventana vuelven (solo los que se plegaron solos, no los que el usuario cerró).
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
    // Un archivo por trabajo (ruta y cuerpos). Mallar y escribir va en un hilo aparte: la ventana sigue viva.
    struct ExportJob {
        QString path;
        std::vector<geometry::Shape> shapes;
        std::vector<std::string> names;  // el nombre de cada cuerpo (el 3MF los guarda)
    };
    void startExport(std::vector<ExportJob> jobs, ExportDialog::Format format, geometry::MeshQuality quality);
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
    // ¿Hay algo cargando? El modelo, la vista previa de una herramienta, una exportación, una importación, una
    // medición o la malla que prepara el visor.
    bool anythingBusy() const;
    // «Calculando…» con su spinner sobre la vista y la barra de estado en ámbar con otro spinner, mientras haya algo
    // cargando: así se ve a la vista que el programa sigue trabajando aunque no cambie nada en pantalla.
    void updateBusyBadge();
    void paintBanner(const QColor& background);
    void setBannerBusy(bool busy);
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
    // Trabajos largos de archivo y medición, en hilos de trabajo (la interfaz no espera).
    BackgroundRun exportRun_;   // exportar o preparar el STL para el slicer
    BackgroundRun importRun_;   // leer y reparar un STEP
    BackgroundRun measureRun_;  // medir (la distancia entre superficies complejas tarda)
    std::uint64_t documentGeneration_ = 0;  // sube al crear o abrir otro diseño: lo que se calculó para el anterior se descarta

    render::Viewport* viewport_ = nullptr;
    bool navigatorAutoHidden_ = false;
    bool timelineAutoHidden_ = false;
    QDockWidget* navigatorDock_ = nullptr;
    QDockWidget* propertiesDock_ = nullptr;
    QDockWidget* timelineDock_ = nullptr;
    QByteArray defaultLayout_; // distribución original, para "Restablecer paneles"
    QAction* saveAction_ = nullptr;
    QLabel* docTitle_ = nullptr;
    QToolButton* timelinePlay_ = nullptr;
    QTimer* timelinePlayer_ = nullptr;
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
    std::vector<QString> generatorGroups_;  // la familia de cada uno (Cajas, Letreros, Engranajes), en el mismo orden
    int toolsTab_ = -1;
    QAction* sectionAction_ = nullptr;
    QFrame* sectionPanel_ = nullptr;
    QComboBox* sectionPlane_ = nullptr;
    QDoubleSpinBox* sectionOffset_ = nullptr;
    QCheckBox* sectionFlip_ = nullptr;
    // Sección por un plano de construcción o por una cara plana (O5): el plano elegido y la lista de planos de construcción.
    QString lastMeasureText_;  // el resultado de Medir en texto plano, para Ctrl+C (O5)
    sketch::Plane sectionCustom_;
    bool hasSectionCustom_ = false;
    std::vector<document::FeatureId> sectionPlaneIds_;
    QLabel* busyBadge_ = nullptr;     // «Calculando…», sobre la vista
    // Menú radial con clic derecho sobre la vista (U6) y atajos de una tecla para todo.
    void openRadialMenu(const QPoint& at);
    void assignShortcuts();
    QPoint radialPress_;
    bool radialPending_ = false;
    ValueInput* toolValueInput_ = nullptr;  // valor del campo principal de la herramienta, junto al cursor (U7)
    void startToolTyping(const QString& text);
    void applyToolValue();
    // Paleta flotante del sketch (U4): casillas para lo que se ve sobre el lienzo; refleja las acciones del menú Ver.
    void createSketchPalette();
    void updateSketchPalette();
    QAction* sketchMeasuresAction_ = nullptr;
    QAction* sketchPointsAction_ = nullptr;
    QAction* allMeasuresAction_ = nullptr;
    QAction* allConstraintsAction_ = nullptr;
    QFrame* sketchPalette_ = nullptr;
    QFrame* recoveryBar_ = nullptr;   // aviso de la copia de recuperación (U2), sobre la vista, arriba y al centro
    void placeRecoveryBar();
    // Tarjeta «Empieza un diseño» sobre el lienzo vacío (G4): los comandos para arrancar, a un clic. Sale con el diseño sin
    // operaciones y sin herramienta ni sketch abiertos; se cierra con «×» (esa sesión) o con «No mostrar más».
    QFrame* startCard_ = nullptr;
    bool startCardDismissed_ = false;
    void updateStartCard();
    bool placeStartCard();  // la coloca; false si no cabe en la vista
    BusySpinner* badgeSpinner_ = nullptr;   // el spinner del distintivo
    StatusBanner* statusBanner_ = nullptr;  // la barra de estado, con el spinner a la izquierda
    BusySpinner* statusSpinner_ = nullptr;  // el de la barra de estado (lo tiene el banner)
    QVariantAnimation* bannerBlink_ = nullptr;  // el parpadeo del banner al cambiar el mensaje
    bool bannerBusy_ = false;         // el banner está en ámbar (hay algo cargando)
    bool viewBusy_ = false;           // el visor prepara la malla de algún cuerpo
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
    QAction* editTextAction_ = nullptr;
    std::vector<QAction*> constraintActions_;
    QLabel* cursorLabel_ = nullptr;
    QComboBox* unitBox_ = nullptr;
    QLabel* modelStatusLabel_ = nullptr;  // primera operación con error o aviso
};

} // namespace nuf::app
