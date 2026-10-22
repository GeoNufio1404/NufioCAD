#include "MainWindow.hpp"
#include "Theme.hpp"

#include <nuf/commands/RenameDocument.hpp>
#include <nuf/geometry/Kernel.hpp>
#include <nuf/io/NufFile.hpp>
#include <nuf/render/Viewport.hpp>

#include <QAction>
#include <QActionGroup>
#include <QCloseEvent>
#include <QDockWidget>
#include <QInputDialog>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMenuBar>
#include <QSettings>
#include <QStatusBar>
#include <QToolBar>
#include <QToolButton>

#include <memory>
#include <tuple>
#include <utility>

namespace nuf::app {

namespace {

// Versión de la distribución de barras y paneles (ver restoreState).
constexpr int kLayoutVersion = 2;

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
    cursorLabel_ = new QLabel(this);
    cursorLabel_->setMinimumWidth(190);
    statusBar()->addPermanentWidget(cursorLabel_);
    statusBar()->addPermanentWidget(versions);
    connect(viewport_, &render::Viewport::selectionChanged, this,
            [this](const QString& description) { statusBar()->showMessage(description); });
    connect(viewport_, &render::Viewport::sketchPlanePicked, this, &MainWindow::startSketch);
    connect(viewport_, &render::Viewport::sketchCursorMoved, this, [this](bool hasPoint, double x, double y) {
        cursorLabel_->setText(hasPoint ? QStringLiteral("X %1   Y %2 mm")
                                             .arg(QLocale().toString(x, 'f', 2), QLocale().toString(y, 'f', 2))
                                       : QString());
    });

    document_.changed.connect([this] { updateTitle(); });
    commands_.changed.connect([this] { updateUndoActions(); });
    updateTitle();
    updateUndoActions();
    resize(1440, 900);

    // Recuerda tamaño de la ventana y distribución de paneles entre sesiones.
    defaultLayout_ = saveState(kLayoutVersion);
    const QSettings settings;
    restoreGeometry(settings.value(QStringLiteral("ui/geometry")).toByteArray());
    // Si cambian las barras o los paneles, kLayoutVersion sube y la
    // distribución guardada (que ya no encaja) se descarta.
    restoreState(settings.value(QStringLiteral("ui/layout")).toByteArray(), kLayoutVersion);
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    QSettings settings;
    settings.setValue(QStringLiteral("ui/geometry"), saveGeometry());
    settings.setValue(QStringLiteral("ui/layout"), saveState(kLayoutVersion));
    QMainWindow::closeEvent(event);
}

void MainWindow::createMenus()
{
    QMenu* file = menuBar()->addMenu(QStringLiteral("&Archivo"));
    // Fase 4: estas acciones se conectan al formato .nuf y a la exportación.
    for (const auto& [text, shortcut] : {
             std::pair{QStringLiteral("&Nuevo diseño"), QKeySequence(QKeySequence::New)},
             std::pair{QStringLiteral("&Abrir…"), QKeySequence(QKeySequence::Open)},
             std::pair{QStringLiteral("&Guardar"), QKeySequence(QKeySequence::Save)},
             std::pair{QStringLiteral("&Exportar…"), QKeySequence()}}) {
        QAction* action = file->addAction(text);
        action->setShortcut(shortcut);
        action->setEnabled(false);
    }
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
    redoAction_ = edit->addAction(QStringLiteral("Rehacer"), QKeySequence(QKeySequence::Redo), this, [this] {
        if (sketch_) {
            sketch_->redo();
        } else {
            commands_.redo();
        }
    });
    edit->addSeparator();
    edit->addAction(QStringLiteral("Renombrar diseño…"), this, [this] {
        bool ok = false;
        const QString name = QInputDialog::getText(
            this, QStringLiteral("Renombrar diseño"), QStringLiteral("Nombre:"), QLineEdit::Normal,
            QString::fromStdString(document_.name()), &ok);
        if (ok && !name.trimmed().isEmpty()) {
            commands_.push(std::make_unique<commands::RenameDocument>(name.trimmed().toStdString()));
        }
    });

    // Esc cancela lo que esté en curso (por ahora, elegir el plano del sketch).
    QAction* cancelAction = new QAction(QStringLiteral("Cancelar"), this);
    cancelAction->setShortcut(QKeySequence(Qt::Key_Escape));
    connect(cancelAction, &QAction::triggered, this, &MainWindow::cancel);
    addAction(cancelAction);

    QMenu* sketchMenu = menuBar()->addMenu(QStringLiteral("&Sketch"));
    createSketchAction_ = sketchMenu->addAction(QStringLiteral("&Crear sketch"), this, &MainWindow::createSketch);
    createSketchAction_->setToolTip(QStringLiteral("Crear un sketch en un plano de origen o en una cara plana"));
    finishSketchAction_ = sketchMenu->addAction(QStringLiteral("&Terminar sketch"), this, &MainWindow::finishSketch);
    sketchMenu->addSeparator();

    // Herramientas de dibujo por familias (como en Fusion 360); la primera
    // de cada familia lleva su tecla.
    using Tool = SketchSession::Tool;
    struct ToolEntry {
        Tool tool;
        QString text;
        QKeySequence key;
    };
    const std::vector<std::pair<QString, std::vector<ToolEntry>>> families{
        {QStringLiteral("Línea"), {{Tool::Line, QStringLiteral("Línea"), QKeySequence(Qt::Key_L)}}},
        {QStringLiteral("Rectángulo"),
         {{Tool::Rectangle, QStringLiteral("Rectángulo por 2 esquinas"), QKeySequence(Qt::Key_R)},
          {Tool::Rectangle3Point, QStringLiteral("Rectángulo por 3 puntos"), {}},
          {Tool::RectangleCenter, QStringLiteral("Rectángulo por centro"), {}}}},
        {QStringLiteral("Círculo"),
         {{Tool::Circle, QStringLiteral("Círculo por centro y diámetro"), QKeySequence(Qt::Key_C)},
          {Tool::Circle2Point, QStringLiteral("Círculo por 2 puntos"), {}},
          {Tool::Circle3Point, QStringLiteral("Círculo por 3 puntos"), {}},
          {Tool::CircleTangent2, QStringLiteral("Círculo tangente a 2 curvas"), {}},
          {Tool::CircleTangent3, QStringLiteral("Círculo tangente a 3 curvas"), {}}}},
        {QStringLiteral("Arco"),
         {{Tool::Arc, QStringLiteral("Arco por 3 puntos"), QKeySequence(Qt::Key_A)},
          {Tool::ArcCenter, QStringLiteral("Arco por centro"), {}},
          {Tool::ArcTangent, QStringLiteral("Arco tangente"), {}}}},
        {QStringLiteral("Polígono"),
         {{Tool::PolygonCircumscribed, QStringLiteral("Polígono circunscrito"), QKeySequence(Qt::Key_P)},
          {Tool::PolygonInscribed, QStringLiteral("Polígono inscrito"), {}},
          {Tool::PolygonEdge, QStringLiteral("Polígono por lado"), {}}}},
        {QStringLiteral("Ranura"),
         {{Tool::SlotCenterToCenter, QStringLiteral("Ranura centro a centro"), {}},
          {Tool::SlotOverall, QStringLiteral("Ranura total"), {}},
          {Tool::SlotCenterPoint, QStringLiteral("Ranura por punto central"), {}},
          {Tool::SlotArc, QStringLiteral("Ranura en arco"), {}}}},
        {QStringLiteral("Elipse"),
         {{Tool::Ellipse, QStringLiteral("Elipse"), {}},
          {Tool::EllipseArc, QStringLiteral("Arco de elipse"), {}}}},
        {QStringLiteral("Spline"),
         {{Tool::SplineFit, QStringLiteral("Spline por puntos de paso"), {}},
          {Tool::SplineControl, QStringLiteral("Spline por polos"), {}}}},
        {QStringLiteral("Cónica"), {{Tool::Conic, QStringLiteral("Curva cónica"), {}}}},
        // Modificar
        {QStringLiteral("Recortar"),
         {{Tool::Trim, QStringLiteral("Recortar"), QKeySequence(Qt::Key_T)},
          {Tool::Extend, QStringLiteral("Extender"), {}},
          {Tool::Break, QStringLiteral("Partir"), {}}}},
        {QStringLiteral("Empalme"),
         {{Tool::Fillet, QStringLiteral("Empalme"), {}},
          {Tool::Chamfer, QStringLiteral("Chaflán"), {}}}},
        {QStringLiteral("Desfase"), {{Tool::Offset, QStringLiteral("Desfase"), QKeySequence(Qt::Key_O)}}},
        {QStringLiteral("Simetría"), {{Tool::Mirror, QStringLiteral("Simetría"), {}}}},
        {QStringLiteral("Mover"),
         {{Tool::Move, QStringLiteral("Mover"), QKeySequence(Qt::Key_M)},
          {Tool::Copy, QStringLiteral("Copiar"), {}},
          {Tool::Rotate, QStringLiteral("Girar"), {}},
          {Tool::Scale, QStringLiteral("Escalar"), {}}}},
        {QStringLiteral("Patrón"),
         {{Tool::PatternRectangular, QStringLiteral("Patrón rectangular"), {}},
          {Tool::PatternCircular, QStringLiteral("Patrón circular"), {}}}},
        {QStringLiteral("Proyectar"), {{Tool::Project, QStringLiteral("Proyectar aristas"), QKeySequence(Qt::Key_P | Qt::ShiftModifier)}}},
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
            toolGroup->addAction(action);
            connect(action, &QAction::triggered, this, [this, tool = entry.tool](bool checked) {
                if (sketch_) {
                    sketch_->setTool(checked ? tool : Tool::None);
                }
            });
            toolActions_[entry.tool] = action;
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
    constructionAction_ = sketchMenu->addAction(QStringLiteral("Alternar &construcción"), QKeySequence(Qt::Key_X),
                                                this, [this] { sketch_->toggleConstruction(); });

    // Restricciones y cotas: se seleccionan las entidades y se aplica.
    QMenu* constraintMenu = sketchMenu->addMenu(QStringLiteral("Res&tricciones"));
    using T = sketch::ConstraintType;
    for (const auto& [type, text, icon] :
         {std::tuple{T::Coincident, QStringLiteral("Coincidente"), QStringLiteral("⊙")},
          std::tuple{T::Horizontal, QStringLiteral("Horizontal"), QStringLiteral("H")},
          std::tuple{T::Vertical, QStringLiteral("Vertical"), QStringLiteral("V")},
          std::tuple{T::Parallel, QStringLiteral("Paralela"), QStringLiteral("∥")},
          std::tuple{T::Perpendicular, QStringLiteral("Perpendicular"), QStringLiteral("⊥")},
          std::tuple{T::Tangent, QStringLiteral("Tangente"), QStringLiteral("T")},
          std::tuple{T::Equal, QStringLiteral("Igual"), QStringLiteral("=")},
          std::tuple{T::Collinear, QStringLiteral("Colineal"), QStringLiteral("≡")},
          std::tuple{T::Concentric, QStringLiteral("Concéntrica"), QStringLiteral("◎")},
          std::tuple{T::Midpoint, QStringLiteral("Punto medio"), QStringLiteral("M")},
          std::tuple{T::Symmetric, QStringLiteral("Simetría"), QStringLiteral("S")},
          std::tuple{T::Fix, QStringLiteral("Fijar"), QStringLiteral("F")},
          std::tuple{T::Curvature, QStringLiteral("Curvatura (G2)"), QStringLiteral("G2")}}) {
        QAction* action = constraintMenu->addAction(text, this, [this, type = type] { sketch_->applyConstraint(type); });
        action->setIconText(icon);
        action->setToolTip(QStringLiteral("%1 (selecciona y aplica)").arg(text));
        constraintActions_.push_back(action);
    }
    dimensionAction_ = sketchMenu->addAction(QStringLiteral("C&ota"), QKeySequence(Qt::Key_D), this,
                                             [this] { sketch_->addDimension(); });
    intersectAction_ = sketchMenu->addAction(QStringLiteral("&Intersección con la pieza"), this,
                                             [this] { sketch_->intersectModel(); });
    dimensionAction_->setToolTip(QStringLiteral("Acotar la selección (D)"));

    QToolBar* toolbar = addToolBar(QStringLiteral("Sketch"));
    toolbar->setObjectName(QStringLiteral("sketchToolbar"));
    toolbar->setMovable(false);
    toolbar->addAction(createSketchAction_);
    toolbar->addAction(finishSketchAction_);
    toolbar->addSeparator();
    // Un botón por familia; la flecha despliega sus variantes y el botón se
    // queda con la última usada. Las de modificar, en una segunda barra.
    addToolBarBreak();
    QToolBar* modifyBar = addToolBar(QStringLiteral("Modificar"));
    modifyBar->setObjectName(QStringLiteral("modifyToolbar"));
    modifyBar->setMovable(false);
    for (std::size_t i = 0; i < toolFamilies_.size(); ++i) {
        const ToolFamily& family = toolFamilies_[i];
        QToolBar* bar = i < firstModifyFamily ? toolbar : modifyBar;
        if (!family.menu) {
            bar->addAction(family.actions.front());
            continue;
        }
        auto* button = new QToolButton(toolbar);
        button->setPopupMode(QToolButton::MenuButtonPopup);
        button->setMenu(family.menu);
        button->setDefaultAction(family.actions.front());
        for (QAction* action : family.actions) {
            connect(action, &QAction::triggered, button, [button, action] { button->setDefaultAction(action); });
        }
        bar->addWidget(button);
    }
    modifyBar->addAction(intersectAction_);
    modifyBar->addSeparator();
    modifyBar->addAction(dimensionAction_);
    for (QAction* action : constraintActions_) {
        modifyBar->addAction(action);
    }

    QMenu* view = menuBar()->addMenu(QStringLiteral("&Ver"));
    view->addAction(QStringLiteral("Vista &inicial"), QKeySequence(Qt::Key_Home), this,
                    [this] { viewport_->resetView(); });
    view->addAction(QStringLiteral("&Ajustar a la ventana"), QKeySequence(Qt::Key_F6), this,
                    [this] { viewport_->fitAll(); });
    lookAtSketchAction_ = view->addAction(QStringLiteral("&Mirar al sketch"), this,
                                          [this] { viewport_->lookAtSketch(); });
    updateSketchActions();
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
    navigatorDock_->setWidget(placeholder(QStringLiteral("Navegador\n(Fase 3)")));
    navigatorDock_->setMinimumWidth(240);
    addDockWidget(Qt::LeftDockWidgetArea, navigatorDock_);

    propertiesDock_ = new QDockWidget(QStringLiteral("Propiedades"), this);
    propertiesDock_->setObjectName(QStringLiteral("properties"));
    propertiesDock_->setWidget(placeholder(QStringLiteral("Panel de propiedades\n(Fase 3)")));
    propertiesDock_->setMinimumWidth(300);
    addDockWidget(Qt::RightDockWidgetArea, propertiesDock_);

    timelineDock_ = new QDockWidget(QStringLiteral("Timeline"), this);
    timelineDock_->setObjectName(QStringLiteral("timeline"));
    timelineDock_->setWidget(placeholder(QStringLiteral("Timeline (Fase 3)")));
    timelineDock_->setFeatures(QDockWidget::NoDockWidgetFeatures);
    addDockWidget(Qt::BottomDockWidgetArea, timelineDock_);
}

void MainWindow::updateUndoActions()
{
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
    setWindowTitle(QString::fromStdString(document_.name()));
}

void MainWindow::createSketch()
{
    if (sketch_ || viewport_->isPickingSketchPlane()) {
        return;
    }
    viewport_->pickSketchPlane();
    statusBar()->showMessage(QStringLiteral("Elige un plano de origen o una cara plana · Esc para cancelar"));
    updateSketchActions();
}

void MainWindow::startSketch(const sketch::Plane& plane, const QString& description)
{
    sketch_ = std::make_unique<SketchSession>(*viewport_, plane);
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
    sketchStatusLabel_->setText(QStringLiteral("Sin geometría"));
    updateSketchActions();
    updateUndoActions();
}

void MainWindow::finishSketch()
{
    if (!sketch_) {
        return;
    }
    // Hasta la Fase 3 (historial) el sketch no se guarda, pero queda a la vista.
    const sketch::Sketch finished = sketch_->sketch();
    const sketch::Plane plane = sketch_->plane();
    sketch_.reset();
    viewport_->showFinishedSketch(finished, plane);
    sketchStatusLabel_->clear();
    statusBar()->clearMessage();
    updateSketchActions();
    updateUndoActions();
}

void MainWindow::cancel()
{
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
    const bool picking = viewport_->isPickingSketchPlane();
    createSketchAction_->setEnabled(!sketch_ && !picking);
    finishSketchAction_->setEnabled(sketch_ != nullptr);
    lookAtSketchAction_->setEnabled(sketch_ != nullptr);
    for (const auto& [tool, action] : toolActions_) {
        action->setEnabled(sketch_ != nullptr);
        if (!sketch_) {
            action->setChecked(false);
        }
    }
    deleteAction_->setEnabled(sketch_ && sketch_->hasSelection());
    constructionAction_->setEnabled(sketch_ && sketch_->hasSelection());
    dimensionAction_->setEnabled(sketch_ != nullptr);
    intersectAction_->setEnabled(sketch_ != nullptr);
    for (QAction* action : constraintActions_) {
        action->setEnabled(sketch_ != nullptr);
    }
}

} // namespace nuf::app
