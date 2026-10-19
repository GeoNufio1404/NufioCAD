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
#include <QMenuBar>
#include <QSettings>
#include <QStatusBar>

#include <memory>
#include <utility>

namespace nuf::app {

namespace {

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
    statusBar()->addPermanentWidget(versions);
    connect(viewport_, &render::Viewport::selectionChanged, this,
            [this](const QString& description) { statusBar()->showMessage(description); });

    document_.changed.connect([this] { updateTitle(); });
    commands_.changed.connect([this] { updateUndoActions(); });
    updateTitle();
    updateUndoActions();
    resize(1440, 900);

    // Recuerda tamaño de la ventana y distribución de paneles entre sesiones.
    defaultLayout_ = saveState();
    const QSettings settings;
    restoreGeometry(settings.value(QStringLiteral("ui/geometry")).toByteArray());
    restoreState(settings.value(QStringLiteral("ui/layout")).toByteArray());
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    QSettings settings;
    settings.setValue(QStringLiteral("ui/geometry"), saveGeometry());
    settings.setValue(QStringLiteral("ui/layout"), saveState());
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
    undoAction_ = edit->addAction(QStringLiteral("Deshacer"), QKeySequence(QKeySequence::Undo), this,
                                  [this] { commands_.undo(); });
    redoAction_ = edit->addAction(QStringLiteral("Rehacer"), QKeySequence(QKeySequence::Redo), this,
                                  [this] { commands_.redo(); });
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

    QMenu* view = menuBar()->addMenu(QStringLiteral("&Ver"));
    view->addAction(QStringLiteral("Vista &inicial"), QKeySequence(Qt::Key_Home), this,
                    [this] { viewport_->resetView(); });
    view->addAction(QStringLiteral("&Ajustar a la ventana"), QKeySequence(Qt::Key_F6), this,
                    [this] { viewport_->fitAll(); });
    view->addSeparator();
    QMenu* panels = view->addMenu(QStringLiteral("&Paneles"));
    panels->addAction(navigatorDock_->toggleViewAction());
    panels->addAction(propertiesDock_->toggleViewAction());
    view->addAction(QStringLiteral("&Restablecer paneles"), this, [this] {
        restoreState(defaultLayout_);
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

} // namespace nuf::app
