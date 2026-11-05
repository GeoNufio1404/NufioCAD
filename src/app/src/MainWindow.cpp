#include "MainWindow.hpp"

#include "ExportDialog.hpp"
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
#include <nuf/geometry/Exchange.hpp>
#include <nuf/geometry/Kernel.hpp>
#include <nuf/io/NufFile.hpp>
#include <nuf/render/Viewport.hpp>

#include <QAction>
#include <QFileInfo>
#include <QDir>
#include <QApplication>
#include <QActionGroup>
#include <QCloseEvent>
#include <QDockWidget>
#include <QFileDialog>
#include <QInputDialog>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMenuBar>
#include <QMessageBox>
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

namespace nuf::app {

namespace {

// Versión de la distribución de barras y paneles (ver restoreState).
constexpr int kLayoutVersion = 5;

// Ícono y descripción de una acción. El tooltip lleva el nombre completo, la
// tecla y, si se da, una línea de ayuda; el botón de la barra muestra el nombre
// corto (`shortName`, si no el mismo texto de la acción).
void decorate(QAction* action, const QString& icon, const QString& hint = {}, const QString& shortName = {})
{
    action->setIcon(uiIcon(icon));
    QString name = action->text();
    name.remove(QLatin1Char('&'));
    action->setIconText(shortName.isEmpty() ? name : shortName);
    QString tip = QStringLiteral("<b>%1</b>").arg(name.toHtmlEscaped());
    if (!action->shortcut().isEmpty()) {
        tip += QStringLiteral(" (%1)").arg(action->shortcut().toString(QKeySequence::NativeText));
    }
    if (!hint.isEmpty()) {
        tip += QStringLiteral("<br>") + hint.toHtmlEscaped();
    }
    action->setToolTip(tip);
}

QWidget* placeholder(const QString& text)
{
    auto* label = new QLabel(text);
    label->setAlignment(Qt::AlignCenter);
    label->setWordWrap(true);
    label->setMargin(12);
    return label;
}

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    viewport_ = new render::Viewport(this);
    setCentralWidget(viewport_);
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
    connect(navigator_, &NavigatorWidget::bodyVisibilityChanged, this, [this](const std::string& key, bool visible) {
        if (visible) {
            hiddenBodies_.erase(key);
        } else {
            hiddenBodies_.insert(key);
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
        cursorLabel_->setText(hasPoint ? QStringLiteral("X %1   Y %2 mm")
                                             .arg(QLocale().toString(x, 'f', 2), QLocale().toString(y, 'f', 2))
                                       : QString());
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

MainWindow::~MainWindow() = default;

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

void MainWindow::createMenus()
{
    // Barras con icono y el nombre debajo; el tooltip da el detalle.
    setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    setIconSize(QSize(24, 24));
    QMenu* file = menuBar()->addMenu(QStringLiteral("&Archivo"));
    const auto addFile = [&](const QString& text, const QKeySequence& shortcut, const QString& icon, auto slot) {
        QAction* action = file->addAction(text);
        action->setShortcut(shortcut);
        decorate(action, icon);
        connect(action, &QAction::triggered, this, slot);
        return action;
    };
    addFile(QStringLiteral("&Nuevo diseño"), QKeySequence(QKeySequence::New), QStringLiteral("file-new"),
            &MainWindow::newDocument);
    addFile(QStringLiteral("&Abrir…"), QKeySequence(QKeySequence::Open), QStringLiteral("file-open"),
            &MainWindow::openDocument);
    addFile(QStringLiteral("&Recientes…"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O), QStringLiteral("file-open"),
            &MainWindow::showRecents);
    recentMenu_ = file->addMenu(QStringLiteral("Abrir &reciente"));
    connect(recentMenu_, &QMenu::aboutToShow, this, &MainWindow::rebuildRecentMenu);
    file->addSeparator();
    addFile(QStringLiteral("&Guardar"), QKeySequence(QKeySequence::Save), QStringLiteral("file-save"),
            [this] { saveDocument(); });
    addFile(QStringLiteral("Guardar &como…"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S),
            QStringLiteral("file-save"), [this] { saveDocumentAs(); });
    file->addSeparator();
    importAction_ = addFile(QStringLiteral("&Importar STEP…"), QKeySequence(Qt::CTRL | Qt::Key_I),
                            QStringLiteral("file-open"), &MainWindow::importStep);
    exportAction_ = addFile(QStringLiteral("&Exportar…"), QKeySequence(), QStringLiteral("file-export"),
                            &MainWindow::exportBodies);
    exportAction_->setEnabled(false);
    file->addSeparator();
    file->addAction(QStringLiteral("&Salir"), QKeySequence(QKeySequence::Quit), this, &QWidget::close);

    QMenu* edit = menuBar()->addMenu(QStringLiteral("&Editar"));
    // Mientras se edita un sketch, deshacer/rehacer actúan dentro del sketch.
    undoAction_ = edit->addAction(QStringLiteral("Deshacer"), QKeySequence(QKeySequence::Undo), this, [this] {
        if (sketch_) {
            sketch_->undo();
        } else {
            commands_.undo();
        }
    });
    decorate(undoAction_, QStringLiteral("undo"));
    redoAction_ = edit->addAction(QStringLiteral("Rehacer"), QKeySequence(QKeySequence::Redo), this, [this] {
        if (sketch_) {
            sketch_->redo();
        } else {
            commands_.redo();
        }
    });
    decorate(redoAction_, QStringLiteral("redo"));
    edit->addSeparator();
    QAction* renameAction = edit->addAction(QStringLiteral("Renombrar diseño…"), this, [this] {
        bool ok = false;
        const QString name = QInputDialog::getText(
            this, QStringLiteral("Renombrar diseño"), QStringLiteral("Nombre:"), QLineEdit::Normal,
            QString::fromStdString(document_.name()), &ok);
        if (ok && !name.trimmed().isEmpty()) {
            commands_.push(std::make_unique<commands::RenameDocument>(name.trimmed().toStdString()));
        }
    });
    decorate(renameAction, QStringLiteral("rename"));

    // Esc cancela lo que esté en curso (por ahora, elegir el plano del sketch).
    QAction* cancelAction = new QAction(QStringLiteral("Cancelar"), this);
    cancelAction->setShortcut(QKeySequence(Qt::Key_Escape));
    connect(cancelAction, &QAction::triggered, this, &MainWindow::cancel);
    addAction(cancelAction);

    QMenu* sketchMenu = menuBar()->addMenu(QStringLiteral("&Sketch"));
    createSketchAction_ = sketchMenu->addAction(QStringLiteral("&Crear sketch"), this, &MainWindow::createSketch);
    decorate(createSketchAction_, QStringLiteral("create-sketch"),
             QStringLiteral("Crear un sketch en un plano de origen o en una cara plana"));
    finishSketchAction_ = sketchMenu->addAction(QStringLiteral("&Terminar sketch"), this, &MainWindow::finishSketch);
    decorate(finishSketchAction_, QStringLiteral("finish-sketch"), QStringLiteral("Salir del sketch y guardarlo"),
             QStringLiteral("Terminar sketch"));
    sketchMenu->addSeparator();

    QMenu* solidMenu = menuBar()->addMenu(QStringLiteral("S&ólido"));
    extrudeAction_ = solidMenu->addAction(QStringLiteral("&Extruir"), QKeySequence(Qt::Key_E), this, [this] {
        startTool(std::make_unique<ExtrudeTool>(document_, *viewport_, std::nullopt));
    });
    decorate(extrudeAction_, QStringLiteral("extrude"),
             QStringLiteral("Extruir perfiles de un sketch o una cara plana"));
    revolveAction_ = solidMenu->addAction(QStringLiteral("&Revolución"), this, [this] {
        startTool(std::make_unique<RevolveTool>(document_, *viewport_, std::nullopt));
    });
    decorate(revolveAction_, QStringLiteral("revolve"),
             QStringLiteral("Girar perfiles de un sketch alrededor de un eje"));
    solidMenu->addSeparator();
    filletAction_ = solidMenu->addAction(QStringLiteral("Re&dondeo"), QKeySequence(Qt::Key_F), this, [this] {
        startTool(std::make_unique<EdgeTool>(document_, *viewport_, std::nullopt, EdgeTool::Style::Fillet));
    });
    decorate(filletAction_, QStringLiteral("fillet"), QStringLiteral("Redondear aristas de un cuerpo"));
    chamferAction_ = solidMenu->addAction(QStringLiteral("&Chaflán"), this, [this] {
        startTool(std::make_unique<EdgeTool>(document_, *viewport_, std::nullopt, EdgeTool::Style::Chamfer));
    });
    decorate(chamferAction_, QStringLiteral("chamfer"), QStringLiteral("Achaflanar aristas de un cuerpo"));
    shellAction_ = solidMenu->addAction(QStringLiteral("&Vaciado"), this, [this] {
        startTool(std::make_unique<ShellTool>(document_, *viewport_, std::nullopt));
    });
    holeAction_ = solidMenu->addAction(QStringLiteral("&Agujero"), QKeySequence(Qt::Key_H), this, [this] {
        startTool(std::make_unique<HoleTool>(document_, *viewport_, std::nullopt));
    });
    decorate(holeAction_, QStringLiteral("hole"),
             QStringLiteral("Hacer agujeros en los círculos de un sketch (simple, abocardado o avellanado)"));
    mirrorAction_ = solidMenu->addAction(QStringLiteral("&Espejo"), this, [this] {
        startTool(std::make_unique<MirrorTool>(document_, *viewport_, std::nullopt));
    });
    decorate(mirrorAction_, QStringLiteral("mirror-solid"),
             QStringLiteral("Reflejar un cuerpo sobre un plano de origen o una cara plana"));
    decorate(shellAction_, QStringLiteral("shell"), QStringLiteral("Vaciar un cuerpo dejando abiertas las caras elegidas"));

    // Herramientas de dibujo por familias (como en Fusion 360); la primera
    // de cada familia lleva su tecla.
    using Tool = SketchSession::Tool;
    struct ToolEntry {
        Tool tool;
        QString text;
        QKeySequence key;
        QString icon;
    };
    const std::vector<std::pair<QString, std::vector<ToolEntry>>> families{
        {QStringLiteral("Línea"), {{Tool::Line, QStringLiteral("Línea"), QKeySequence(Qt::Key_L), QStringLiteral("line")}}},
        {QStringLiteral("Rectángulo"),
         {{Tool::Rectangle, QStringLiteral("Rectángulo por 2 esquinas"), QKeySequence(Qt::Key_R), QStringLiteral("rectangle")},
          {Tool::Rectangle3Point, QStringLiteral("Rectángulo por 3 puntos"), {}, QStringLiteral("rectangle-3point")},
          {Tool::RectangleCenter, QStringLiteral("Rectángulo por centro"), {}, QStringLiteral("rectangle-center")}}},
        {QStringLiteral("Círculo"),
         {{Tool::Circle, QStringLiteral("Círculo por centro y diámetro"), QKeySequence(Qt::Key_C), QStringLiteral("circle")},
          {Tool::Circle2Point, QStringLiteral("Círculo por 2 puntos"), {}, QStringLiteral("circle-2point")},
          {Tool::Circle3Point, QStringLiteral("Círculo por 3 puntos"), {}, QStringLiteral("circle-3point")},
          {Tool::CircleTangent2, QStringLiteral("Círculo tangente a 2 curvas"), {}, QStringLiteral("circle-tangent2")},
          {Tool::CircleTangent3, QStringLiteral("Círculo tangente a 3 curvas"), {}, QStringLiteral("circle-tangent3")}}},
        {QStringLiteral("Arco"),
         {{Tool::Arc, QStringLiteral("Arco por 3 puntos"), QKeySequence(Qt::Key_A), QStringLiteral("arc")},
          {Tool::ArcCenter, QStringLiteral("Arco por centro"), {}, QStringLiteral("arc-center")},
          {Tool::ArcTangent, QStringLiteral("Arco tangente"), {}, QStringLiteral("arc-tangent")}}},
        {QStringLiteral("Polígono"),
         {{Tool::PolygonCircumscribed, QStringLiteral("Polígono circunscrito"), QKeySequence(Qt::Key_P), QStringLiteral("polygon-circumscribed")},
          {Tool::PolygonInscribed, QStringLiteral("Polígono inscrito"), {}, QStringLiteral("polygon-inscribed")},
          {Tool::PolygonEdge, QStringLiteral("Polígono por lado"), {}, QStringLiteral("polygon-edge")}}},
        {QStringLiteral("Ranura"),
         {{Tool::SlotCenterToCenter, QStringLiteral("Ranura centro a centro"), {}, QStringLiteral("slot-center-to-center")},
          {Tool::SlotOverall, QStringLiteral("Ranura total"), {}, QStringLiteral("slot-overall")},
          {Tool::SlotCenterPoint, QStringLiteral("Ranura por punto central"), {}, QStringLiteral("slot-center-point")},
          {Tool::SlotArc, QStringLiteral("Ranura en arco"), {}, QStringLiteral("slot-arc")}}},
        {QStringLiteral("Elipse"),
         {{Tool::Ellipse, QStringLiteral("Elipse"), {}, QStringLiteral("ellipse")},
          {Tool::EllipseArc, QStringLiteral("Arco de elipse"), {}, QStringLiteral("ellipse-arc")}}},
        {QStringLiteral("Spline"),
         {{Tool::SplineFit, QStringLiteral("Spline por puntos de paso"), {}, QStringLiteral("spline-fit")},
          {Tool::SplineControl, QStringLiteral("Spline por polos"), {}, QStringLiteral("spline-control")}}},
        {QStringLiteral("Cónica"), {{Tool::Conic, QStringLiteral("Curva cónica"), {}, QStringLiteral("conic")}}},
        // Modificar
        {QStringLiteral("Recortar"),
         {{Tool::Trim, QStringLiteral("Recortar"), QKeySequence(Qt::Key_T), QStringLiteral("trim")},
          {Tool::Extend, QStringLiteral("Extender"), {}, QStringLiteral("extend")},
          {Tool::Break, QStringLiteral("Partir"), {}, QStringLiteral("break")}}},
        {QStringLiteral("Empalme"),
         {{Tool::Fillet, QStringLiteral("Empalme"), {}, QStringLiteral("fillet")},
          {Tool::Chamfer, QStringLiteral("Chaflán"), {}, QStringLiteral("chamfer")}}},
        {QStringLiteral("Desfase"), {{Tool::Offset, QStringLiteral("Desfase"), QKeySequence(Qt::Key_O), QStringLiteral("offset")}}},
        {QStringLiteral("Simetría"), {{Tool::Mirror, QStringLiteral("Simetría"), {}, QStringLiteral("mirror")}}},
        {QStringLiteral("Mover"),
         {{Tool::Move, QStringLiteral("Mover"), QKeySequence(Qt::Key_M), QStringLiteral("move")},
          {Tool::Copy, QStringLiteral("Copiar"), {}, QStringLiteral("copy")},
          {Tool::Rotate, QStringLiteral("Girar"), {}, QStringLiteral("rotate")},
          {Tool::Scale, QStringLiteral("Escalar"), {}, QStringLiteral("scale")}}},
        {QStringLiteral("Patrón"),
         {{Tool::PatternRectangular, QStringLiteral("Patrón rectangular"), {}, QStringLiteral("pattern-rectangular")},
          {Tool::PatternCircular, QStringLiteral("Patrón circular"), {}, QStringLiteral("pattern-circular")}}},
        {QStringLiteral("Proyectar"), {{Tool::Project, QStringLiteral("Proyectar aristas"), QKeySequence(Qt::Key_P | Qt::ShiftModifier), QStringLiteral("project")}}},
        {QStringLiteral("Texto"),
         {{Tool::Text, QStringLiteral("Texto"), {}, QStringLiteral("text")},
          {Tool::Image, QStringLiteral("Imagen de referencia"), {}, QStringLiteral("image")}}},
    };
    // A partir de esta familia van a la barra «Modificar».
    const std::size_t firstModifyFamily = 9;
    auto* toolGroup = new QActionGroup(this);
    toolGroup->setExclusionPolicy(QActionGroup::ExclusionPolicy::ExclusiveOptional);
    for (const auto& [family, entries] : families) {
        QMenu* menu = entries.size() > 1 ? sketchMenu->addMenu(family) : sketchMenu;
        std::vector<QAction*> actions;
        for (const ToolEntry& entry : entries) {
            QAction* action = menu->addAction(entry.text);
            action->setShortcut(entry.key);
            action->setCheckable(true);
            decorate(action, entry.icon, {}, family);
            toolGroup->addAction(action);
            connect(action, &QAction::triggered, this, [this, tool = entry.tool](bool checked) {
                if (sketch_) {
                    sketch_->setTool(checked ? tool : Tool::None);
                }
            });
            toolActions_[entry.tool] = action;
            connect(action, &QAction::triggered, this, [this] { clearNavigation(); });
            actions.push_back(action);
        }
        toolFamilies_.push_back({entries.size() > 1 ? menu : nullptr, actions});
        if (toolFamilies_.size() == firstModifyFamily) {
            sketchMenu->addSeparator();
        }
    }

    sketchMenu->addSeparator();
    deleteAction_ = sketchMenu->addAction(QStringLiteral("&Borrar"), QKeySequence(QKeySequence::Delete), this,
                                          [this] { sketch_->deleteSelection(); });
    decorate(deleteAction_, QStringLiteral("delete"));
    constructionAction_ = sketchMenu->addAction(QStringLiteral("Alternar &construcción"), QKeySequence(Qt::Key_X),
                                                this, [this] { sketch_->toggleConstruction(); });
    decorate(constructionAction_, QStringLiteral("construction"),
             QStringLiteral("Convierte la geometría en auxiliar (no forma perfiles)"),
             QStringLiteral("Construcción"));
    lockAction_ = sketchMenu->addAction(QStringLiteral("Alternar b&loqueo"), QKeySequence(Qt::Key_K), this,
                                        [this] { sketch_->toggleLock(); });
    decorate(lockAction_, QStringLiteral("lock"), QStringLiteral("Impide mover la geometría seleccionada"),
             QStringLiteral("Bloqueo"));

    // Restricciones y cotas: se seleccionan las entidades y se aplica.
    QMenu* constraintMenu = sketchMenu->addMenu(QStringLiteral("Res&tricciones"));
    using T = sketch::ConstraintType;
    for (const auto& [type, text, icon] :
         {std::tuple{T::Coincident, QStringLiteral("Coincidente"), QStringLiteral("c-coincident")},
          std::tuple{T::Horizontal, QStringLiteral("Horizontal"), QStringLiteral("c-horizontal")},
          std::tuple{T::Vertical, QStringLiteral("Vertical"), QStringLiteral("c-vertical")},
          std::tuple{T::Parallel, QStringLiteral("Paralela"), QStringLiteral("c-parallel")},
          std::tuple{T::Perpendicular, QStringLiteral("Perpendicular"), QStringLiteral("c-perpendicular")},
          std::tuple{T::Tangent, QStringLiteral("Tangente"), QStringLiteral("c-tangent")},
          std::tuple{T::Equal, QStringLiteral("Igual"), QStringLiteral("c-equal")},
          std::tuple{T::Collinear, QStringLiteral("Colineal"), QStringLiteral("c-collinear")},
          std::tuple{T::Concentric, QStringLiteral("Concéntrica"), QStringLiteral("c-concentric")},
          std::tuple{T::Midpoint, QStringLiteral("Punto medio"), QStringLiteral("c-midpoint")},
          std::tuple{T::Symmetric, QStringLiteral("Simetría"), QStringLiteral("c-symmetric")},
          std::tuple{T::Fix, QStringLiteral("Fijar"), QStringLiteral("c-fix")},
          std::tuple{T::Curvature, QStringLiteral("Curvatura (G2)"), QStringLiteral("c-curvature")}}) {
        QAction* action = constraintMenu->addAction(text, this, [this, type = type] { sketch_->applyConstraint(type); });
        decorate(action, icon, QStringLiteral("Selecciona las entidades y aplica"));
        constraintActions_.push_back(action);
    }
    dimensionAction_ = sketchMenu->addAction(QStringLiteral("C&ota"), QKeySequence(Qt::Key_D), this,
                                             [this] { sketch_->addDimension(); });
    decorate(dimensionAction_, QStringLiteral("dimension"), QStringLiteral("Acotar la selección"));
    intersectAction_ = sketchMenu->addAction(QStringLiteral("&Intersección con la pieza"), this,
                                             [this] { sketch_->intersectModel(); });
    decorate(intersectAction_, QStringLiteral("intersect"), QStringLiteral("Traza la intersección de la pieza con el plano del sketch"),
             QStringLiteral("Intersección"));
    importDxfAction_ = sketchMenu->addAction(QStringLiteral("Importar &DXF…"), this, [this] {
        const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Importar DXF"), QString(),
                                                          QStringLiteral("Dibujos DXF (*.dxf)"));
        if (!path.isEmpty()) {
            sketch_->importDxf(path);
        }
    });
    decorate(importDxfAction_, QStringLiteral("import-dxf"));

    // Cinta al estilo de Fusion 360: pestaña Sólido y, mientras se edita un
    // sketch, la pestaña Sketch con «Terminar sketch» al final.
    ribbon_ = new Ribbon(this);
    solidTab_ = ribbon_->addTab(QStringLiteral("SÓLIDO"));
    sketchTab_ = ribbon_->addTab(QStringLiteral("SKETCH"));
    ribbon_->addGroup(solidTab_, QStringLiteral("CREAR"));
    ribbon_->addAction(solidTab_, createSketchAction_);
    ribbon_->addAction(solidTab_, extrudeAction_);
    ribbon_->addAction(solidTab_, revolveAction_);
    ribbon_->addGroup(solidTab_, QStringLiteral("MODIFICAR"));
    ribbon_->addAction(solidTab_, filletAction_);
    ribbon_->addAction(solidTab_, chamferAction_);
    ribbon_->addAction(solidTab_, shellAction_);
    ribbon_->addAction(solidTab_, holeAction_);
    ribbon_->addAction(solidTab_, mirrorAction_);

    auto family = [this, &families](const QString& name) {
        const std::size_t i = static_cast<std::size_t>(
            std::find_if(families.begin(), families.end(), [&](const auto& f) { return f.first == name; }) -
            families.begin());
        ribbon_->addFamily(sketchTab_, toolFamilies_[i].menu, toolFamilies_[i].actions);
    };
    ribbon_->addGroup(sketchTab_, QStringLiteral("CREAR"));
    for (const char* name : {"Línea", "Rectángulo", "Círculo", "Arco", "Polígono", "Elipse", "Ranura", "Spline",
                             "Cónica", "Simetría", "Patrón", "Proyectar"}) {
        family(QString::fromUtf8(name));
    }
    ribbon_->addAction(sketchTab_, intersectAction_);
    ribbon_->addAction(sketchTab_, dimensionAction_);
    ribbon_->addGroup(sketchTab_, QStringLiteral("MODIFICAR"));
    for (const char* name : {"Recortar", "Empalme", "Desfase", "Mover"}) {
        family(QString::fromUtf8(name));
    }
    ribbon_->addGroup(sketchTab_, QStringLiteral("RESTRICCIONES"));
    ribbon_->addMenu(sketchTab_, QStringLiteral("Restricciones"), uiIcon(QStringLiteral("c-perpendicular")),
                     constraintMenu);
    ribbon_->addGroup(sketchTab_, QStringLiteral("INSERTAR"));
    family(QStringLiteral("Texto"));
    ribbon_->addAction(sketchTab_, importDxfAction_);
    ribbon_->addGroup(sketchTab_, QStringLiteral("OPCIONES"));
    ribbon_->addAction(sketchTab_, constructionAction_);
    ribbon_->addAction(sketchTab_, lockAction_);
    ribbon_->addTrailing(sketchTab_, finishSketchAction_);
    ribbon_->setTabVisible(sketchTab_, false);

    QToolBar* ribbonBar = addToolBar(QStringLiteral("Cinta"));
    ribbonBar->setObjectName(QStringLiteral("ribbonToolbar"));
    ribbonBar->setMovable(false);
    ribbonBar->setContentsMargins(0, 0, 0, 0);
    ribbonBar->addWidget(ribbon_);


    QMenu* view = menuBar()->addMenu(QStringLiteral("&Ver"));
    QAction* homeAction = view->addAction(QStringLiteral("Vista &inicial"), QKeySequence(Qt::Key_Home), this,
                                          [this] { viewport_->resetView(); });
    decorate(homeAction, QStringLiteral("view-home"), QStringLiteral("Vista isométrica de todo el modelo"));
    QAction* fitAction = view->addAction(QStringLiteral("&Ajustar a la ventana"), QKeySequence(Qt::Key_F6), this,
                                         [this] { viewport_->fitAll(); });
    decorate(fitAction, QStringLiteral("view-fit"), QStringLiteral("Encuadrar todo el modelo en la ventana"));
    lookAtSketchAction_ = view->addAction(QStringLiteral("&Mirar al sketch"), this,
                                          [this] { viewport_->lookAtSketch(); });
    decorate(lookAtSketchAction_, QStringLiteral("look-at-sketch"),
             QStringLiteral("Orientar la cámara de frente al plano del sketch"));
    // Barra flotante sobre el visor, como la de navegación de Fusion 360.
    // Orbitar / desplazar / zoom como herramientas: activas, el arrastre
    // izquierdo hace eso. Esc o volver a pulsar las desactiva.
    navGroup_ = new QActionGroup(this);
    navGroup_->setExclusionPolicy(QActionGroup::ExclusionPolicy::ExclusiveOptional);
    std::vector<QAction*> navActions;
    using Mode = render::Viewport::NavigationMode;
    for (const auto& [text, icon, hint, mode] :
         {std::tuple{QStringLiteral("Orbitar"), QStringLiteral("nav-orbit"),
                     QStringLiteral("Arrastra con el botón izquierdo para girar la vista (Shift + botón central también)"),
                     Mode::Orbit},
          std::tuple{QStringLiteral("Desplazar"), QStringLiteral("nav-pan"),
                     QStringLiteral("Arrastra para mover la vista (botón derecho o central también)"), Mode::Pan},
          std::tuple{QStringLiteral("Zoom"), QStringLiteral("nav-zoom"),
                     QStringLiteral("Arrastra arriba o abajo para acercar o alejar (la rueda también)"), Mode::Zoom}}) {
        QAction* action = new QAction(text, this);
        action->setCheckable(true);
        decorate(action, icon, hint);
        navGroup_->addAction(action);
        connect(action, &QAction::toggled, this, [this, mode = mode](bool on) {
            viewport_->setNavigationMode(on ? mode : Mode::Default);
            if (on) {
                // Una herramienta de navegación reemplaza a la de dibujo.
                if (sketch_) {
                    sketch_->setTool(SketchSession::Tool::None);
                }
                for (const auto& [tool, toolAction] : toolActions_) {
                    toolAction->setChecked(false);
                }
            }
        });
        navActions.push_back(action);
    }
    navActions.push_back(homeAction);
    navActions.push_back(fitAction);
    navActions.push_back(lookAtSketchAction_);
    new NavBar(viewport_, navActions);
    // Encima de todo lo que flota sobre el visor.
    startScreen_ = new StartScreen(viewport_);
    startScreen_->hide();
    // Cualquier cosa que se elija en el panel lo cierra antes de actuar.
    const auto closeRecents = [this] {
        recentsOpen_ = false;
        updateStartScreen();
    };
    connect(startScreen_, &StartScreen::closeRequested, this, closeRecents);
    connect(startScreen_, &StartScreen::newRequested, this, [this, closeRecents] {
        closeRecents();
        newDocument();
    });
    connect(startScreen_, &StartScreen::openRequested, this, [this, closeRecents] {
        closeRecents();
        openDocument();
    });
    connect(startScreen_, &StartScreen::importRequested, this, [this, closeRecents] {
        closeRecents();
        importStep();
    });
    connect(startScreen_, &StartScreen::recentRequested, this, [this, closeRecents](const QString& path) {
        closeRecents();
        openRecent(path);
    });
    updateSketchActions();
    view->addSeparator();
    auto* measures = view->addAction(QStringLiteral("Mostrar &medidas del sketch"));
    measures->setCheckable(true);
    measures->setChecked(QSettings().value(QStringLiteral("view/sketchMeasures"), true).toBool());
    viewport_->setSketchMeasuresVisible(measures->isChecked());
    connect(measures, &QAction::toggled, this, [this](bool on) {
        QSettings().setValue(QStringLiteral("view/sketchMeasures"), on);
        viewport_->setSketchMeasuresVisible(on);
    });
    // Medidas y restricciones: por defecto solo en lo que se mira (cursor o selección).
    auto* allMeasures = view->addAction(QStringLiteral("Medidas de &todo el sketch a la vez"));
    auto* allConstraints = view->addAction(QStringLiteral("Restricciones de t&odo el sketch a la vez"));
    for (QAction* action : {allMeasures, allConstraints}) {
        action->setCheckable(true);
    }
    allMeasures->setChecked(QSettings().value(QStringLiteral("view/sketchAllMeasures"), false).toBool());
    allConstraints->setChecked(QSettings().value(QStringLiteral("view/sketchAllConstraints"), false).toBool());
    const auto applyAll = [this, allMeasures, allConstraints] {
        QSettings().setValue(QStringLiteral("view/sketchAllMeasures"), allMeasures->isChecked());
        QSettings().setValue(QStringLiteral("view/sketchAllConstraints"), allConstraints->isChecked());
        viewport_->setSketchShowAll(allMeasures->isChecked(), allConstraints->isChecked());
    };
    connect(allMeasures, &QAction::toggled, this, applyAll);
    connect(allConstraints, &QAction::toggled, this, applyAll);
    applyAll();
    view->addSeparator();
    QMenu* panels = view->addMenu(QStringLiteral("&Paneles"));
    panels->addAction(navigatorDock_->toggleViewAction());
    panels->addAction(propertiesDock_->toggleViewAction());
    view->addAction(QStringLiteral("&Restablecer paneles"), this, [this] {
        restoreState(defaultLayout_, kLayoutVersion);
        for (QDockWidget* dock : {navigatorDock_, propertiesDock_, timelineDock_}) {
            dock->setFloating(false);
            dock->show();
        }
    });
    view->addSeparator();
    QMenu* themeMenu = view->addMenu(QStringLiteral("&Tema"));
    auto* themeGroup = new QActionGroup(this);
    for (const auto& [text, theme] : {std::pair{QStringLiteral("&Sistema"), Theme::System},
                                      std::pair{QStringLiteral("&Claro"), Theme::Light},
                                      std::pair{QStringLiteral("&Oscuro"), Theme::Dark}}) {
        QAction* action = themeMenu->addAction(text, this, [theme] { setTheme(theme); });
        action->setCheckable(true);
        action->setChecked(theme == currentTheme());
        themeGroup->addAction(action);
    }
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
    setWindowTitle(QString::fromStdString(document_.name()) + QStringLiteral("[*]"));
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
    if (sketch_ || tool_ || viewport_->isPickingSketchPlane()) {
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
    if (sketch_ || tool_ || viewport_->isPickingSketchPlane()) {
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
    const sketch::Sketch sketch = placed != after.end() ? *placed->second.sketch : feature->sketch();
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
            edited->setSketch(std::move(finished));
            const std::string label = "Editar " + feature->name();
            commands_.push(std::make_unique<commands::EditFeature>(*editing, std::move(edited), label));
        }
    } else {
        const bool empty =
            std::ranges::none_of(finished.entities(), [](const sketch::Entity& e) { return !e.reference; });
        if (!empty) {
            document::SketchAttachment attachment = newSketchAttachment_;
            attachment.lastPlane = plane;
            commands_.push(std::make_unique<commands::AddFeature>(
                std::make_unique<document::SketchFeature>(std::move(finished), attachment)));
        }
    }
    refreshModel();
    updateSketchActions();
    updateUndoActions();
}

// --- Operaciones sólidas ---------------------------------------------------------

void MainWindow::startTool(std::unique_ptr<FeatureTool> tool)
{
    if (sketch_ || tool_ || viewport_->isPickingSketchPlane()) {
        return;
    }
    tool_ = std::move(tool);
    connect(tool_.get(), &FeatureTool::accepted, this, [this] { finishTool(true); });
    connect(tool_.get(), &FeatureTool::cancelled, this, [this] { finishTool(false); });
    connect(tool_.get(), &FeatureTool::message, this, [this](const QString& text) { statusBar()->showMessage(text, 6000); });
    refreshModel();  // al editar: el modelo hasta esa operación
    tool_->start();
    propertiesDock_->setWidget(tool_->panel());
    propertiesDock_->show();
    timeline_->setEditing(tool_->editing());
    statusBar()->showMessage(QStringLiteral("Elige las entradas en la vista · Enter acepta · Esc cancela"));
    updateSketchActions();
    updateUndoActions();
}

void MainWindow::finishTool(bool accepted)
{
    if (!tool_) {
        return;
    }
    std::unique_ptr<document::Feature> feature = accepted ? tool_->build() : nullptr;
    const std::optional<document::FeatureId> editing = tool_->editing();
    const document::FeatureId id = tool_->featureId();
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
    if (feature) {
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

// --- Vista del modelo ----------------------------------------------------------------

std::size_t MainWindow::shownUpTo() const
{
    if (editingSketch_) {
        return document_.indexOf(*editingSketch_).value_or(document_.marker());
    }
    if (tool_) {
        return tool_->position();
    }
    return document_.marker();
}

bool MainWindow::sketchVisible(document::FeatureId id, std::size_t upTo) const
{
    if (const auto chosen = sketchVisibility_.find(id); chosen != sketchVisibility_.end()) {
        return chosen->second;
    }
    // Como en Fusion: un sketch se oculta cuando una operación ya lo usó.
    for (std::size_t i = 0; i < upTo; ++i) {
        const document::Feature& f = document_.feature(i);
        if (f.kind() == document::FeatureKind::Sketch || f.suppressed()) {
            continue;
        }
        const std::vector<document::FeatureId> deps = f.dependencies();
        if (std::ranges::find(deps, id) != deps.end()) {
            return false;
        }
    }
    return true;
}

void MainWindow::updateExportAction()
{
    updateStartScreen();
    for (QAction* action : {filletAction_, chamferAction_, shellAction_, holeAction_, mirrorAction_}) {
        if (action != nullptr) {
            action->setEnabled(!sketch_ && !tool_ && !viewport_->isPickingSketchPlane() && !document_.result().bodies.empty());
        }
    }
    if (importAction_ != nullptr) {
        importAction_->setEnabled(!sketch_ && !tool_);
    }
    if (exportAction_ != nullptr) {
        exportAction_->setEnabled(!sketch_ && !tool_ && !document_.result().bodies.empty());
    }
}

void MainWindow::exportBodies()
{
    const std::vector<document::Body>& bodies = document_.result().bodies;
    std::vector<std::string> shown;
    for (const document::Body& body : bodies) {
        if (!hiddenBodies_.contains(body.key)) {
            shown.push_back(body.key);
        }
    }
    ExportDialog dialog(bodies, shown, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const QString extension = ExportDialog::extension(dialog.format());
    const QString filter = QStringLiteral("%1 (*.%2)").arg(extension.toUpper(), extension);
    const std::vector<std::size_t> chosen = dialog.selected();
    const QString suggested = QString::fromStdString(document_.name());

    // Una lista de (archivo, cuerpos) según «un archivo por cuerpo».
    std::vector<std::pair<QString, std::vector<geometry::Shape>>> jobs;
    if (dialog.onePerBody() && chosen.size() > 1) {
        const QString folder = QFileDialog::getExistingDirectory(this, QStringLiteral("Carpeta donde exportar"));
        if (folder.isEmpty()) {
            return;
        }
        for (std::size_t index : chosen) {
            const QString name = QString::fromStdString(bodies[index].name);
            jobs.push_back({QDir(folder).filePath(name + QLatin1Char('.') + extension), {bodies[index].shape}});
        }
    } else {
        QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Exportar"), suggested + QLatin1Char('.') + extension,
                                                    filter);
        if (path.isEmpty()) {
            return;
        }
        if (QFileInfo(path).suffix().isEmpty()) {
            path += QLatin1Char('.') + extension;
        }
        std::vector<geometry::Shape> shapes;
        for (std::size_t index : chosen) {
            shapes.push_back(bodies[index].shape);
        }
        jobs.push_back({path, std::move(shapes)});
    }

    // «Un archivo por cuerpo» no pregunta cada nombre: avisa antes de pisar.
    if (jobs.size() > 1) {
        QStringList existing;
        for (const auto& job : jobs) {
            if (QFileInfo::exists(job.first)) {
                existing << QFileInfo(job.first).fileName();
            }
        }
        if (!existing.isEmpty()
            && QMessageBox::question(this, QStringLiteral("Exportar"),
                                     QStringLiteral("Ya existen y se reemplazarán:\n%1")
                                         .arg(existing.join(QLatin1Char('\n'))))
                   != QMessageBox::Yes) {
            return;
        }
    }

    QApplication::setOverrideCursor(Qt::WaitCursor);
    QString error;
    try {
        for (const auto& [path, shapes] : jobs) {
            const std::filesystem::path out = path.toStdU16String();
            switch (dialog.format()) {
            case ExportDialog::Format::Stl:
                geometry::writeStl(out, shapes, dialog.quality());
                break;
            case ExportDialog::Format::Obj:
                geometry::writeObj(out, shapes, dialog.quality());
                break;
            case ExportDialog::Format::Step:
                geometry::writeStep(out, shapes);
                break;
            }
        }
    } catch (const std::exception& e) {
        error = QString::fromUtf8(e.what());
    }
    QApplication::restoreOverrideCursor();

    if (!error.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Exportar"), error);
    } else {
        statusBar()->showMessage(jobs.size() == 1 ? QStringLiteral("Exportado a %1").arg(QDir::toNativeSeparators(jobs.front().first))
                                                  : QStringLiteral("Exportados %1 archivos").arg(jobs.size()),
                                 8000);
    }
}

void MainWindow::refreshModel()
{
    if (!viewport_ || !timeline_ || !navigator_ || !modelStatusLabel_) {
        return;
    }
    const std::size_t upTo = shownUpTo();
    const document::ModelState& state = document_.stateBefore(upTo);
    updateExportAction();

    // Con una herramienta activa, ella muestra su vista previa.
    if (!tool_) {
        std::vector<document::Body> visible;
        for (const document::Body& body : state.bodies) {
            if (!hiddenBodies_.contains(body.key)) {
                visible.push_back(body);
            }
        }
        viewport_->showBodies(visible);
    }

    std::map<document::FeatureId, ShownSketch> wanted;
    std::set<document::FeatureId> visibleIds;
    for (std::size_t i = 0; i < upTo && i < document_.featureCount(); ++i) {
        const auto* sk = dynamic_cast<const document::SketchFeature*>(&document_.feature(i));
        if (sk == nullptr || sk->id() == editingSketch_) {
            continue;
        }
        const auto placed = state.sketches.find(sk->id());
        if (placed == state.sketches.end()) {
            continue;  // suprimido
        }
        // Al usar una herramienta, se ven todos (para elegir perfiles).
        if (!tool_ && !sketchVisible(sk->id(), upTo)) {
            continue;
        }
        visibleIds.insert(sk->id());
        // El calculado: lo proyectado del modelo ya está al día.
        wanted[sk->id()] = {placed->second.sketch.get(), placed->second.plane, sk->name()};
    }
    for (auto it = shownSketches_.begin(); it != shownSketches_.end();) {
        const auto keep = wanted.find(it->first);
        const auto samePoint = [](const sketch::Vec3& a, const sketch::Vec3& b) {
            return a.x == b.x && a.y == b.y && a.z == b.z;
        };
        const auto samePlane = [&](const sketch::Plane& a, const sketch::Plane& b) {
            return samePoint(a.origin, b.origin) && samePoint(a.xAxis, b.xAxis) && samePoint(a.yAxis, b.yAxis);
        };
        const bool same = keep != wanted.end() && keep->second.sketch == it->second.sketch
                          && keep->second.name == it->second.name && samePlane(keep->second.plane, it->second.plane);
        if (!same) {
            viewport_->hideFinishedSketch(it->first);
            it = shownSketches_.erase(it);
        } else {
            ++it;
        }
    }
    for (const auto& [id, shown] : wanted) {
        if (!shownSketches_.contains(id)) {
            viewport_->showFinishedSketch(id, QString::fromStdString(shown.name), *shown.sketch, shown.plane);
            shownSketches_[id] = shown;
        }
    }
    navigator_->refresh(document_, state, upTo, hiddenBodies_, visibleIds, originVisible_, groundVisible_);

    // La primera operación con problemas, en la barra de estado.
    QString problem;
    int problems = 0;
    for (std::size_t i = 0; i < document_.featureCount(); ++i) {
        const document::Feature& f = document_.feature(i);
        const document::FeatureStatus& st = document_.status(f.id());
        if (st.health != document::Health::Error && st.health != document::Health::Warning) {
            continue;
        }
        if (problems++ == 0) {
            problem = QStringLiteral("%1 %2: %3")
                          .arg(st.health == document::Health::Error ? QStringLiteral("⛔") : QStringLiteral("⚠"),
                               QString::fromStdString(f.name()), QString::fromStdString(st.message));
        }
    }
    if (problems > 1) {
        problem += QStringLiteral(" (y %1 más)").arg(problems - 1);
    }
    modelStatusLabel_->setText(problem);
    modelStatusLabel_->setToolTip(problem);
    timeline_->updateGeometry();
    timeline_->update();
}

void MainWindow::clearNavigation()
{
    if (QAction* active = navGroup_ ? navGroup_->checkedAction() : nullptr) {
        active->setChecked(false);
    }
}

void MainWindow::cancel()
{
    if (recentsOpen_) {
        recentsOpen_ = false;
        updateStartScreen();
        return;
    }
    if (navGroup_ && navGroup_->checkedAction()) {
        clearNavigation();
        return;
    }
    if (tool_) {
        finishTool(false);
        return;
    }
    if (sketch_ && sketch_->cancel()) {
        return;
    }
    if (viewport_->isPickingSketchPlane()) {
        viewport_->cancelPickSketchPlane();
        statusBar()->clearMessage();
        updateSketchActions();
    }
}

void MainWindow::updateSketchActions()
{
    updateExportAction();
    const bool picking = viewport_->isPickingSketchPlane();
    createSketchAction_->setEnabled(!sketch_ && !picking && !tool_);
    extrudeAction_->setEnabled(!sketch_ && !picking && !tool_);
    revolveAction_->setEnabled(!sketch_ && !picking && !tool_);
    finishSketchAction_->setEnabled(sketch_ != nullptr);
    if (ribbon_ && (sketch_ != nullptr) != ribbonSketching_) {
        ribbonSketching_ = sketch_ != nullptr;
        ribbon_->setTabVisible(sketchTab_, ribbonSketching_);
        ribbon_->setCurrentTab(ribbonSketching_ ? sketchTab_ : solidTab_);
    }
    lookAtSketchAction_->setEnabled(sketch_ != nullptr);
    for (const auto& [tool, action] : toolActions_) {
        action->setEnabled(sketch_ != nullptr);
        if (!sketch_) {
            action->setChecked(false);
        }
    }
    deleteAction_->setEnabled(sketch_ && sketch_->hasSelection());
    constructionAction_->setEnabled(sketch_ && sketch_->hasSelection());
    lockAction_->setEnabled(sketch_ && sketch_->hasSelection());
    dimensionAction_->setEnabled(sketch_ != nullptr);
    intersectAction_->setEnabled(sketch_ != nullptr);
    importDxfAction_->setEnabled(sketch_ != nullptr);
    for (QAction* action : constraintActions_) {
        action->setEnabled(sketch_ != nullptr);
    }
}

} // namespace nuf::app
