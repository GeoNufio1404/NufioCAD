#include "MainWindow.hpp"
#include "Theme.hpp"

#include <nuf/commands/RenameDocument.hpp>
#include <nuf/geometry/Kernel.hpp>
#include <nuf/io/NufFile.hpp>
#include <nuf/render/Viewport.hpp>

#include <QAction>
#include <QActionGroup>
#include <QDockWidget>
#include <QInputDialog>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
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
    createMenus();
    createDocks();

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
    auto* navigator = new QDockWidget(QStringLiteral("Navegador"), this);
    navigator->setWidget(placeholder(QStringLiteral("Navegador\n(Fase 3)")));
    navigator->setMinimumWidth(240);
    addDockWidget(Qt::LeftDockWidgetArea, navigator);

    auto* properties = new QDockWidget(QStringLiteral("Propiedades"), this);
    properties->setWidget(placeholder(QStringLiteral("Panel de propiedades\n(Fase 3)")));
    properties->setMinimumWidth(300);
    addDockWidget(Qt::RightDockWidgetArea, properties);

    auto* timeline = new QDockWidget(QStringLiteral("Timeline"), this);
    timeline->setWidget(placeholder(QStringLiteral("Timeline (Fase 3)")));
    timeline->setFeatures(QDockWidget::NoDockWidgetFeatures);
    addDockWidget(Qt::BottomDockWidgetArea, timeline);
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
