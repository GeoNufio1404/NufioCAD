#include "MainWindow.hpp"

#include "ExportDialog.hpp"
#include "BodiesDialog.hpp"
#include "ExpressionSpinBox.hpp"
#include "GeneratorTool.hpp"
#include "SettingsDialog.hpp"
#include "Units.hpp"
#include "ParametersDialog.hpp"
#include "FeatureTools.hpp"
#include "Icons.hpp"
#include "ModelPanels.hpp"
#include "NavBar.hpp"
#include "Recovery.hpp"
#include "StartScreen.hpp"
#include "Ribbon.hpp"
#include "Theme.hpp"

#include <nuf/commands/FeatureCommands.hpp>
#include <nuf/commands/RenameDocument.hpp>
#include <nuf/commands/SetBodyInfo.hpp>
#include <nuf/commands/SetParameters.hpp>
#include <nuf/geometry/Exchange.hpp>
#include <nuf/geometry/Kernel.hpp>
#include <nuf/commands/AddRecipe.hpp>
#include <nuf/commands/ReplaceRecipe.hpp>
#include <nuf/document/Generators.hpp>
#include <nuf/sketch/Annotations.hpp>
#include <nuf/geometry/Measure.hpp>
#include <nuf/io/NufFile.hpp>
#include <nuf/render/Viewport.hpp>

#include <QAction>
#include <QFileInfo>
#include <QDir>
#include <QApplication>
#include <QActionGroup>
#include <QCloseEvent>
#include <QDockWidget>
#include <QEvent>
#include <QFileDialog>
#include <QInputDialog>
#include <QKeySequence>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QLabel>
#include <QSignalBlocker>
#include <QLineEdit>
#include <QLocale>
#include <QMenuBar>
#include <QMessageBox>
#include <QProcess>
#include <QStandardPaths>
#include <QScrollArea>
#include <QSettings>
#include <QTimer>
#include <QStatusBar>
#include <QToolBar>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <tuple>
#include <utility>

#include "MainWindowCommon.hpp"

namespace nuf::app {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    viewport_ = new render::Viewport(this);
    setCentralWidget(viewport_);
    // «Calculando…» sobre la vista, abajo a la izquierda (se recoloca al cambiar el tamaño de la vista).
    busyBadge_ = new QLabel(QStringLiteral("⏳ Calculando…"), viewport_);
    busyBadge_->setStyleSheet(QStringLiteral(
        "QLabel { background: rgba(20, 24, 31, 215); color: #E6EAF0; border: 1px solid #3A4250; border-radius: 12px;"
        " padding: 5px 14px; }"));
    busyBadge_->adjustSize();
    busyBadge_->hide();
    viewport_->installEventFilter(this);
    createDocks();
    createMenus();

    auto* versions = new QLabel(QStringLiteral("OCCT %1 · SQLite %2 · Qt %3")
                                    .arg(QString::fromStdString(geometry::kernelVersion()),
                                         QString::fromStdString(io::NufFile::sqliteVersion()),
                                         QString::fromLatin1(qVersion())));
    sketchStatusLabel_ = new QLabel(this);
    statusBar()->addPermanentWidget(sketchStatusLabel_);
    modelStatusLabel_ = new QLabel(this);
    statusBar()->addPermanentWidget(modelStatusLabel_);
    cursorLabel_ = new QLabel(this);
    cursorLabel_->setMinimumWidth(190);
    statusBar()->addPermanentWidget(cursorLabel_);
    statusBar()->addPermanentWidget(versions);
    connect(viewport_, &render::Viewport::selectionChanged, this,
            [this](const QString& description) { statusBar()->showMessage(description); });
    connect(viewport_, &render::Viewport::busyChanged, this, [this](bool busy) {
        if (busy) {
            statusBar()->showMessage(QStringLiteral("Preparando el dibujo de la pieza…"));
        } else {
            statusBar()->clearMessage();
        }
    });
    connect(viewport_, &render::Viewport::sketchPlanePicked, this, &MainWindow::startSketch);
    connect(viewport_, &render::Viewport::finishedSketchActivated, this, &MainWindow::editSketch);
    connect(timeline_, &TimelineWidget::featureActivated, this, &MainWindow::activateFeature);
    connect(timeline_, &TimelineWidget::featureMenuRequested, this, &MainWindow::showFeatureMenu);
    connect(timeline_, &TimelineWidget::markerMoved, this, [this](std::size_t marker) {
        if (!sketch_ && !tool_) {
            commands_.push(std::make_unique<commands::SetMarker>(marker));
        }
    });
    connect(timeline_, &TimelineWidget::featureMoved, this, [this](document::FeatureId id, std::size_t index) {
        if (sketch_ || tool_) {
            return;
        }
        if (!document_.canMoveFeature(id, index)) {
            statusBar()->showMessage(QStringLiteral("No se puede mover ahí: quedaría antes de lo que necesita o "
                                                    "después de lo que la usa"),
                                     6000);
            return;
        }
        commands_.push(std::make_unique<commands::MoveFeature>(id, index));
    });
    ExpressionSpinBox::setDocument(&document_);
    // Los generadores con texto piden el contorno de las letras a la vista (que es la que conoce las fuentes).
    document::setTextOutliner([this](const std::string& text, double height, const std::string& family) {
        return viewport_->textOutline(QString::fromStdString(text), height, QString::fromStdString(family));
    });
    // Las cotas del sketch se escriben en la unidad elegida.
    const auto applyUnits = [this] {
        sketch::setDimensionLengthScale(Units::instance().factor(), 3 + Units::instance().extraDecimals());
        if (sketch_) {
            sketch_->refreshView();
        }
    };
    connect(&Units::instance(), &Units::changed, this, applyUnits);
    applyUnits();
    connect(navigator_, &NavigatorWidget::bodyVisibilityChanged, this, [this](const std::string& key, bool visible) {
        if (visible) {
            hiddenBodies_.erase(key);
        } else {
            hiddenBodies_.insert(key);
        }
        setViewDirty(true);
        refreshModel();
    });
    connect(navigator_, &NavigatorWidget::planeVisibilityChanged, this, [this](document::FeatureId id, bool visible) {
        if (visible) {
            hiddenPlanes_.erase(id);
        } else {
            hiddenPlanes_.insert(id);
        }
        refreshModel();
    });
    connect(navigator_, &NavigatorWidget::componentVisibilityChanged, this, [this](const std::string& name, bool visible) {
        for (const document::Body& body : document_.result().bodies) {
            if (document_.componentOf(body.key) == name) {
                if (visible) {
                    hiddenBodies_.erase(body.key);
                } else {
                    hiddenBodies_.insert(body.key);
                }
            }
        }
        setViewDirty(true);
        refreshModel();
    });
    connect(navigator_, &NavigatorWidget::sketchVisibilityChanged, this, [this](document::FeatureId id, bool visible) {
        sketchVisibility_[id] = visible;
        setViewDirty(true);
        refreshModel();
    });
    connect(navigator_, &NavigatorWidget::originVisibilityChanged, this, [this](bool visible) {
        originVisible_ = visible;
        setViewDirty(true);
        viewport_->setOriginVisible(visible);
    });
    connect(navigator_, &NavigatorWidget::sketchActivated, this, &MainWindow::editSketch);
    connect(navigator_, &NavigatorWidget::groundVisibilityChanged, this, [this](bool visible) {
        groundVisible_ = visible;
        setViewDirty(true);
        viewport_->setGroundVisible(visible);
    });
    connect(viewport_, &render::Viewport::sketchCursorMoved, this, [this](bool hasPoint, double x, double y) {
        cursorLabel_->setText(hasPoint ? QStringLiteral("X %1   Y %2 %3")
                                             .arg(Units::instance().format(x), Units::instance().format(y), Units::instance().unit())
                                       : QString());
    });

    // El historial se recalcula en un hilo aparte: una rosca o una pieza grande no deben colgar la interfaz.
    document_.enableBackgroundCompute([this](std::function<void()> done) {
        QMetaObject::invokeMethod(this, std::move(done), Qt::QueuedConnection);
    });
    document_.computingChanged.connect([this](bool computing) {
        if (computing) {
            statusBar()->showMessage(QStringLiteral("Calculando el modelo… puedes seguir mirando; un cambio nuevo cancela el cálculo"));
        } else {
            statusBar()->clearMessage();
            auto pending = std::move(afterCompute_);
            afterCompute_.clear();
            for (const auto& what : pending) {
                what();
            }
        }
        updateSketchActions();
        updateExportAction();
        updateBusyBadge();
    });
    document_.changed.connect([this] {
        updateTitle();
        refreshModel();
    });
    commands_.changed.connect([this] {
        updateUndoActions();
        updateTitle();
    });
    setAcceptDrops(true);
    // Copia de recuperación cada minuto si hay cambios sin guardar.
    recovery_ = std::make_unique<Recovery>();
    bool secondsOk = false;
    const int seconds = qEnvironmentVariableIntValue("NUF_RECOVERY_SECONDS", &secondsOk);
    auto* recoveryTimer = new QTimer(this);
    recoveryTimer->setInterval((secondsOk && seconds > 0 ? seconds : 60) * 1000);
    connect(recoveryTimer, &QTimer::timeout, this, &MainWindow::saveRecovery);
    recoveryTimer->start();
    updateTitle();
    updateUndoActions();
    refreshModel();
    resize(1440, 900);

    resizeDocks({navigatorDock_, propertiesDock_}, {240, 300}, Qt::Horizontal);
    // Recuerda tamaño de la ventana y distribución de paneles entre sesiones.
    defaultLayout_ = saveState(kLayoutVersion);
    const QSettings settings;
    restoreGeometry(settings.value(QStringLiteral("ui/geometry")).toByteArray());
    // Si cambian las barras o los paneles, kLayoutVersion sube y la
    // distribución guardada (que ya no encaja) se descarta.
    restoreState(settings.value(QStringLiteral("ui/layout")).toByteArray(), kLayoutVersion);
    propertiesDock_->hide();  // aparece al usar una herramienta
}

MainWindow::~MainWindow()
{
    // Los hilos de cálculo avisan a esta ventana: se paran antes de que empiece a destruirse.
    document_.stopBackground();
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == viewport_ && busyBadge_ != nullptr && event->type() == QEvent::Resize) {
        busyBadge_->move(16, viewport_->height() - busyBadge_->height() - 16);
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::updateBusyBadge()
{
    const bool busy = toolComputing_ || document_.computing();
    if (busyBadge_ == nullptr || busyBadge_->isVisible() == busy) {
        return;
    }
    busyBadge_->move(16, viewport_->height() - busyBadge_->height() - 16);
    busyBadge_->setVisible(busy);
    if (busy) {
        busyBadge_->raise();
    }
}

bool MainWindow::modelBusy()
{
    if (!document_.computing()) {
        return false;
    }
    statusBar()->showMessage(QStringLiteral("El modelo todavía se está calculando; espera a que termine"), 4000);
    return true;
}

void MainWindow::whenComputed(std::function<void()> what)
{
    if (document_.computing()) {
        afterCompute_.push_back(std::move(what));
    } else {
        what();
    }
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (!maybeSave()) {
        event->ignore();
        return;
    }
    recovery_->discard();  // cierre normal: no hace falta la copia
    QSettings settings;
    settings.setValue(QStringLiteral("ui/geometry"), saveGeometry());
    settings.setValue(QStringLiteral("ui/layout"), saveState(kLayoutVersion));
    QMainWindow::closeEvent(event);
}

void MainWindow::createDocks()
{
    // objectName: lo usa saveState()/restoreState() para recordar cada panel.
    navigatorDock_ = new QDockWidget(QStringLiteral("Navegador"), this);
    navigatorDock_->setObjectName(QStringLiteral("navigator"));
    navigator_ = new NavigatorWidget();
    navigatorDock_->setWidget(navigator_);
    navigatorDock_->setMinimumWidth(240);
    addDockWidget(Qt::LeftDockWidgetArea, navigatorDock_);

    propertiesDock_ = new QDockWidget(QStringLiteral("Propiedades"), this);
    propertiesDock_->setObjectName(QStringLiteral("properties"));
    propertiesPlaceholder_ = placeholder(QStringLiteral("Elige una herramienta (Extruir, Revolución…) para ver "
                                                         "sus propiedades aquí"));
    propertiesDock_->setWidget(propertiesPlaceholder_);
    propertiesDock_->setMinimumWidth(300);
    addDockWidget(Qt::RightDockWidgetArea, propertiesDock_);

    timelineDock_ = new QDockWidget(QStringLiteral("Timeline"), this);
    timelineDock_->setObjectName(QStringLiteral("timeline"));
    timeline_ = new TimelineWidget(document_);
    auto* scroll = new QScrollArea();
    scroll->setWidget(timeline_);
    scroll->setWidgetResizable(true);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setFixedHeight(timeline_->sizeHint().height() + 16);
    timelineDock_->setWidget(scroll);
    timelineDock_->setFeatures(QDockWidget::NoDockWidgetFeatures);
    addDockWidget(Qt::BottomDockWidgetArea, timelineDock_);
}

void MainWindow::updateUndoActions()
{
    if (tool_) {
        undoAction_->setEnabled(false);
        redoAction_->setEnabled(false);
        return;
    }
    if (sketch_) {
        undoAction_->setEnabled(sketch_->canUndo());
        redoAction_->setEnabled(sketch_->canRedo());
        undoAction_->setText(QStringLiteral("Deshacer en el sketch"));
        redoAction_->setText(QStringLiteral("Rehacer en el sketch"));
        return;
    }
    undoAction_->setEnabled(commands_.canUndo());
    redoAction_->setEnabled(commands_.canRedo());
    undoAction_->setText(commands_.canUndo()
                             ? QStringLiteral("Deshacer %1").arg(QString::fromStdString(commands_.undoLabel()))
                             : QStringLiteral("Deshacer"));
    redoAction_->setText(commands_.canRedo()
                             ? QStringLiteral("Rehacer %1").arg(QString::fromStdString(commands_.redoLabel()))
                             : QStringLiteral("Rehacer"));
}

void MainWindow::updateTitle()
{
    // Con archivo, el título es su nombre (el que se ve al abrir y guardar);
    // sin él, el nombre interno del documento.
    const QString name = path_.empty() ? QString::fromStdString(document_.name())
                                       : QString::fromStdU16String(path_.stem().u16string());
    setWindowTitle(name + QStringLiteral("[*]"));
    setWindowModified(hasUnsavedWork());
    updateStartScreen();
}

// Como Fusion 360, la aplicación abre directo en un lienzo vacío. Los recientes
// están en un panel que se abre a petición (Archivo › Recientes…) y se cierra
// con cualquier acción o con Esc.
void MainWindow::updateStartScreen()
{
    if (startScreen_ == nullptr) {
        return;
    }
    if (isEditing() || viewport_->isPickingSketchPlane()) {
        recentsOpen_ = false;
    }
    startScreen_->setVisible(recentsOpen_);
}

void MainWindow::showRecents()
{
    if (isEditing() || viewport_->isPickingSketchPlane()) {
        return;
    }
    recentsOpen_ = true;
    updateStartScreen();
}

void MainWindow::createSketch()
{
    if (sketch_ || tool_ || viewport_->isPickingSketchPlane() || modelBusy()) {
        return;
    }
    viewport_->pickSketchPlane();
    statusBar()->showMessage(QStringLiteral("Elige un plano de origen o una cara plana · Esc para cancelar"));
    updateSketchActions();
}

void MainWindow::startSketch(const sketch::Plane& plane, const QString& description,
                             const document::SketchAttachment& attachment)
{
    editingSketch_.reset();
    newSketchAttachment_ = attachment;
    openSession(plane, description, std::nullopt);
}

void MainWindow::editSketch(document::FeatureId id)
{
    if (sketch_ || tool_ || viewport_->isPickingSketchPlane() || modelBusy()) {
        return;
    }
    const auto index = document_.indexOf(id);
    const auto* feature = dynamic_cast<const document::SketchFeature*>(document_.findFeature(id));
    if (!index || feature == nullptr) {
        return;
    }
    // Su plano tal como quedó al calcularlo (sigue a su cara).
    const auto& after = document_.stateBefore(*index + 1).sketches;
    const auto placed = after.find(id);
    const sketch::Plane plane = placed != after.end() ? placed->second.plane : feature->attachment().lastPlane;
    // Con lo proyectado ya actualizado.
    // (y con las expresiones de las cotas, para poder editarlas)
    const sketch::Sketch sketch =
        feature->forEditing(placed != after.end() ? *placed->second.sketch : feature->sketch());
    editingSketch_ = id;
    refreshModel();  // el modelo como estaba antes de este sketch
    openSession(plane, QString::fromStdString(feature->name()), sketch);
}

void MainWindow::openSession(const sketch::Plane& plane, const QString& description,
                             std::optional<sketch::Sketch> initial)
{
    viewport_->clearModelSelection();
    sketch_ = std::make_unique<SketchSession>(*viewport_, plane, std::move(initial));
    connect(sketch_.get(), &SketchSession::toolChanged, this, [this](SketchSession::Tool tool) {
        for (const auto& [kind, action] : toolActions_) {
            action->setChecked(kind == tool);
        }
    });
    connect(sketch_.get(), &SketchSession::hintChanged, this, [this, description](const QString& hint) {
        statusBar()->showMessage(hint.isEmpty() ? QStringLiteral("Sketch en %1").arg(description) : hint);
    });
    connect(sketch_.get(), &SketchSession::statusChanged, sketchStatusLabel_, &QLabel::setText);
    connect(sketch_.get(), &SketchSession::historyChanged, this, &MainWindow::updateUndoActions);
    connect(sketch_.get(), &SketchSession::selectionChanged, this, &MainWindow::updateSketchActions);
    connect(sketch_.get(), &SketchSession::message, this,
            [this](const QString& text) { statusBar()->showMessage(text, 6000); });
    statusBar()->showMessage(QStringLiteral("Sketch en %1 · L línea, R rectángulo, C círculo, A arco").arg(description));
    sketch_->start();
    timeline_->setEditing(editingSketch_);
    updateSketchActions();
    updateUndoActions();
}

void MainWindow::finishSketch()
{
    if (!sketch_) {
        return;
    }
    sketch::Sketch finished = sketch_->sketch();
    const sketch::Plane plane = sketch_->plane();
    sketch_.reset();
    const std::optional<document::FeatureId> editing = std::exchange(editingSketch_, std::nullopt);
    timeline_->setEditing(std::nullopt);
    sketchStatusLabel_->clear();
    statusBar()->clearMessage();
    if (editing) {
        // Un solo paso de deshacer para toda la edición del sketch.
        if (const auto* feature = dynamic_cast<const document::SketchFeature*>(document_.findFeature(*editing))) {
            auto edited = std::unique_ptr<document::SketchFeature>(
                static_cast<document::SketchFeature*>(feature->clone().release()));
            edited->setSketchFromEditing(std::move(finished));
            const std::string label = "Editar " + feature->name();
            commands_.push(std::make_unique<commands::EditFeature>(*editing, std::move(edited), label));
        }
    } else {
        const bool empty =
            std::ranges::none_of(finished.entities(), [](const sketch::Entity& e) { return !e.reference; });
        if (!empty) {
            document::SketchAttachment attachment = newSketchAttachment_;
            attachment.lastPlane = plane;
            auto created = std::make_unique<document::SketchFeature>(sketch::Sketch(), attachment);
            created->setSketchFromEditing(std::move(finished));
            commands_.push(std::make_unique<commands::AddFeature>(std::move(created)));
        }
    }
    refreshModel();
    updateSketchActions();
    updateUndoActions();
}

// --- Operaciones sólidas ---------------------------------------------------------

void MainWindow::startTool(std::unique_ptr<FeatureTool> tool)
{
    if (sketch_ || tool_ || viewport_->isPickingSketchPlane() || modelBusy()) {
        return;
    }
    tool_ = std::move(tool);
    connect(tool_.get(), &FeatureTool::accepted, this, [this] { finishTool(true); });
    connect(tool_.get(), &FeatureTool::cancelled, this, [this] { finishTool(false); });
    connect(tool_.get(), &FeatureTool::message, this, [this](const QString& text) { statusBar()->showMessage(text, 6000); });
    connect(tool_.get(), &FeatureTool::computingChanged, this, [this](bool computing) {
        toolComputing_ = computing;
        updateBusyBadge();
    });
    refreshModel();  // al editar: el modelo hasta esa operación
    tool_->start();
    propertiesDock_->setWidget(tool_->panel());
    propertiesDock_->show();
    timeline_->setEditing(tool_->editing());
    statusBar()->showMessage(QStringLiteral("Elige las entradas en la vista · Enter acepta · Esc cancela"));
    updateSketchActions();
    updateUndoActions();
}

void MainWindow::startGenerator(const document::Generator& generator)
{
    if (sketch_ || tool_ || viewport_->isPickingSketchPlane() || modelBusy()) {
        return;
    }
    startTool(std::make_unique<GeneratorTool>(document_, *viewport_, generator));
}

void MainWindow::reopenGenerator(document::FeatureId id)
{
    if (sketch_ || tool_ || viewport_->isPickingSketchPlane() || modelBusy()) {
        return;
    }
    const document::Feature* feature = document_.findFeature(id);
    if (feature == nullptr || feature->origin.lead == 0) {
        return;
    }
    const document::FeatureId lead = feature->origin.lead;
    const std::vector<document::FeatureId> group = document::generatedGroup(document_, lead);
    const document::Generator* generator = group.empty() ? nullptr : document::findGenerator(document_.findFeature(group.front())->origin.generator);
    if (generator == nullptr) {
        statusBar()->showMessage(QStringLiteral("Este generador ya no existe en esta versión"), 8000);
        return;
    }
    const document::GeneratorOrigin& origin = document_.findFeature(group.front())->origin;
    startTool(std::make_unique<GeneratorTool>(document_, *viewport_, *generator, lead, document::decodeValues(origin.values),
                                              origin.suffix));
}

void MainWindow::finishTool(bool accepted)
{
    if (!tool_) {
        return;
    }
    // Un generador no da una operación sino una receta (parámetros, operaciones y nombres) que se aplica de una vez.
    std::unique_ptr<commands::Command> recipe;
    if (const auto* generator = dynamic_cast<const GeneratorTool*>(tool_.get()); generator != nullptr && accepted) {
        try {
            document::Recipe made = generator->recipe();
            if (generator->lead() != 0) {
                if (const std::string problem = commands::ReplaceRecipe::problem(document_, generator->lead(), made); !problem.empty()) {
                    statusBar()->showMessage(QString::fromStdString(problem), 12000);
                    return;  // la herramienta sigue abierta
                }
                recipe = std::make_unique<commands::ReplaceRecipe>(generator->lead(), std::move(made), generator->generator().title());
            } else {
                recipe = std::make_unique<commands::AddRecipe>(std::move(made), generator->generator().title());
            }
        } catch (const std::exception& e) {
            statusBar()->showMessage(QString::fromUtf8(e.what()), 8000);  // p. ej. un texto sin letras
            return;                                                       // la herramienta sigue abierta
        }
    }
    std::unique_ptr<document::Feature> feature = accepted && !recipe ? tool_->make() : nullptr;
    const std::optional<document::FeatureId> editing = tool_->editing();
    const document::FeatureId id = tool_->featureId();
    tool_->stopPreview();
    toolComputing_ = false;
    updateBusyBadge();
    // Se destruye después: esta llamada viene de una señal de la herramienta.
    propertiesDock_->setWidget(propertiesPlaceholder_);
    propertiesDock_->hide();  // como los diálogos de Fusion: solo mientras hay un comando
    tool_.release()->deleteLater();
    timeline_->setEditing(std::nullopt);
    statusBar()->clearMessage();
    viewport_->clearModelSelection();
    viewport_->clearToolPreview();
    viewport_->hideDistanceHandle();
    viewport_->setAdditiveSelection(false);
    if (recipe) {
        commands_.push(std::move(recipe));
    } else if (feature) {
        if (editing) {
            commands_.push(std::make_unique<commands::EditFeature>(*editing, std::move(feature)));
        } else {
            feature->setId(id);
            commands_.push(std::make_unique<commands::AddFeature>(std::move(feature)));
        }
    }
    refreshModel();
    updateSketchActions();
    updateUndoActions();
}

void MainWindow::activateFeature(document::FeatureId id)
{
    const document::Feature* feature = document_.findFeature(id);
    if (feature == nullptr || sketch_ || tool_) {
        return;
    }
    switch (feature->kind()) {
    case document::FeatureKind::Sketch:
        editSketch(id);
        break;
    case document::FeatureKind::Extrude:
        startTool(std::make_unique<ExtrudeTool>(document_, *viewport_, id));
        break;
    case document::FeatureKind::Revolve:
        startTool(std::make_unique<RevolveTool>(document_, *viewport_, id));
        break;
    case document::FeatureKind::Fillet:
    case document::FeatureKind::Chamfer:
        startTool(std::make_unique<EdgeTool>(document_, *viewport_, id));
        break;
    case document::FeatureKind::Shell:
        startTool(std::make_unique<ShellTool>(document_, *viewport_, id));
        break;
    case document::FeatureKind::Hole:
        startTool(std::make_unique<HoleTool>(document_, *viewport_, id));
        break;
    case document::FeatureKind::Mirror:
        startTool(std::make_unique<MirrorTool>(document_, *viewport_, id));
        break;
    case document::FeatureKind::Pattern:
        startTool(std::make_unique<PatternTool>(document_, *viewport_, id));
        break;
    case document::FeatureKind::ConstructionPlane:
        startTool(std::make_unique<PlaneTool>(document_, *viewport_, id));
        break;
    case document::FeatureKind::ConstructionAxis:
        startTool(std::make_unique<AxisTool>(document_, *viewport_, id));
        break;
    case document::FeatureKind::Box:
        startTool(std::make_unique<BoxTool>(document_, *viewport_, id));
        break;
    case document::FeatureKind::Cylinder:
        startTool(std::make_unique<CylinderTool>(document_, *viewport_, id));
        break;
    case document::FeatureKind::Gear:
        startTool(std::make_unique<GearTool>(document_, *viewport_, id));
        break;
    case document::FeatureKind::Thread:
        startTool(std::make_unique<ThreadTool>(document_, *viewport_, id));
        break;
    case document::FeatureKind::Sweep:
        startTool(std::make_unique<SweepTool>(document_, *viewport_, id));
        break;
    case document::FeatureKind::Loft:
        startTool(std::make_unique<LoftTool>(document_, *viewport_, id));
        break;
    case document::FeatureKind::Move:
        startTool(std::make_unique<MoveTool>(document_, *viewport_, id));
        break;
    case document::FeatureKind::Combine:
        startTool(std::make_unique<CombineTool>(document_, *viewport_, id));
        break;
    case document::FeatureKind::Joint:
        startTool(std::make_unique<JointTool>(document_, *viewport_, id));
        break;
    case document::FeatureKind::Import:
        break;  // no tiene parámetros que editar
    }
}

void MainWindow::showFeatureMenu(document::FeatureId id, const QPoint& globalPos)
{
    const document::Feature* feature = document_.findFeature(id);
    if (feature == nullptr || sketch_ || tool_) {
        return;
    }
    const QString name = QString::fromStdString(feature->name());
    QMenu menu(this);
    menu.addAction(QStringLiteral("Editar"), this, [this, id] { activateFeature(id); });
    if (feature->origin.lead != 0) {
        if (const document::Generator* generator = document::findGenerator(feature->origin.generator)) {
            menu.addAction(QStringLiteral("Reabrir «%1»…").arg(QString::fromStdString(generator->title())), this,
                           [this, id] { reopenGenerator(id); });
        }
    }
    menu.addAction(QStringLiteral("Renombrar…"), this, [this, id, name] {
        bool ok = false;
        const QString text = QInputDialog::getText(this, QStringLiteral("Renombrar"), QStringLiteral("Nombre:"),
                                                   QLineEdit::Normal, name, &ok);
        const document::Feature* f = document_.findFeature(id);
        if (ok && f && !text.trimmed().isEmpty() && text.trimmed() != name) {
            auto renamed = f->clone();
            renamed->setName(text.trimmed().toStdString());
            commands_.push(std::make_unique<commands::EditFeature>(
                id, std::move(renamed), "Renombrar " + name.toStdString()));
        }
    });
    const bool suppressed = feature->suppressed();
    menu.addAction(suppressed ? QStringLiteral("Reactivar") : QStringLiteral("Suprimir"), this, [this, id, suppressed] {
        commands_.push(std::make_unique<commands::SuppressFeature>(id, !suppressed));
    });
    menu.addAction(QStringLiteral("Mover el marcador aquí"), this, [this, id] {
        if (const auto index = document_.indexOf(id)) {
            commands_.push(std::make_unique<commands::SetMarker>(*index + 1));
        }
    });
    menu.addSeparator();
    menu.addAction(QStringLiteral("Borrar"), this, [this, id, name] {
        const std::vector<document::FeatureId> users = document_.dependents(id);
        if (!users.empty()) {
            QStringList names;
            for (document::FeatureId user : users) {
                names << QString::fromStdString(document_.findFeature(user)->name());
            }
            const auto answer = QMessageBox::question(
                this, QStringLiteral("Borrar %1").arg(name),
                QStringLiteral("Estas operaciones dependen de %1 y quedarán con error:\n\n%2\n\n¿Borrarla igualmente?")
                    .arg(name, names.join(QStringLiteral(", "))));
            if (answer != QMessageBox::Yes) {
                return;
            }
        }
        commands_.push(std::make_unique<commands::DeleteFeature>(id));
    });
    menu.exec(globalPos);
}

} // namespace nuf::app
