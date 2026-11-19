// Archivo: Nuevo, Abrir, Guardar y Guardar como, y el aviso de cambios sin guardar.

#include "MainWindow.hpp"
#include "ModelPanels.hpp"
#include "Recovery.hpp"
#include "Units.hpp"
#include "StartScreen.hpp"

#include <nuf/commands/FeatureCommands.hpp>
#include <nuf/document/Features.hpp>
#include <nuf/geometry/Exchange.hpp>
#include <nuf/io/DocumentFile.hpp>
#include <nuf/render/Viewport.hpp>

#include <QApplication>
#include <QBuffer>
#include <QImage>
#include <QLocale>
#include <QMenu>
#include <QCloseEvent>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QSettings>
#include <QInputDialog>
#include <QToolButton>
#include <QCheckBox>
#include <functional>
#include <QStatusBar>
#include <QUrl>

#include <filesystem>

namespace nuf::app {

namespace {

constexpr const char* kExtension = "nuf";

std::filesystem::path toPath(const QString& text)
{
    return std::filesystem::path(text.toStdU16String());
}

QString fromPath(const std::filesystem::path& path)
{
    return QString::fromStdU16String(path.u16string());
}

QString fileFilter()
{
    return QStringLiteral("Diseños NufioCAD (*.nuf)");
}

QString lastDirectory()
{
    return QSettings().value(QStringLiteral("files/lastDirectory"), QDir::homePath()).toString();
}

void rememberDirectory(const QString& file)
{
    QSettings().setValue(QStringLiteral("files/lastDirectory"), QFileInfo(file).absolutePath());
}

bool isDesign(const QString& file)
{
    return file.endsWith(QLatin1String(".nuf"), Qt::CaseInsensitive);
}

bool isStep(const QString& file)
{
    return file.endsWith(QLatin1String(".step"), Qt::CaseInsensitive)
        || file.endsWith(QLatin1String(".stp"), Qt::CaseInsensitive);
}

} // namespace

bool MainWindow::hasUnsavedWork() const
{
    return viewDirty_ || !commands_.isClean();
}

void MainWindow::setViewDirty(bool dirty)
{
    if (viewDirty_ != dirty) {
        viewDirty_ = dirty;
        updateTitle();
    }
}

bool MainWindow::isEditing() const
{
    return sketch_ != nullptr || tool_ != nullptr;
}

io::ViewState MainWindow::currentView() const
{
    io::ViewState view;
    view.hiddenBodies = hiddenBodies_;
    view.sketchVisibility = sketchVisibility_;
    view.originVisible = originVisible_;
    view.groundVisible = groundVisible_;
    view.displayUnit = Units::label(Units::instance().length()).toStdString();
    if (const auto camera = viewport_->cameraState(); camera.valid) {
        view.camera = {camera.eye[0], camera.eye[1], camera.eye[2], camera.center[0], camera.center[1], camera.center[2],
                       camera.up[0], camera.up[1], camera.up[2], camera.scale, camera.orthographic ? 1.0 : 0.0};
    }
    return view;
}

void MainWindow::applyView(const io::ViewState& view)
{
    // La unidad con que se guardó el diseño (U3); los anteriores no la traen y se queda la que hay.
    for (const LengthUnit unit : {LengthUnit::Mm, LengthUnit::Cm, LengthUnit::M, LengthUnit::In}) {
        if (!view.displayUnit.empty() && Units::label(unit).toStdString() == view.displayUnit) {
            Units::instance().setLength(unit);
        }
    }
    hiddenBodies_ = view.hiddenBodies;
    sketchVisibility_ = view.sketchVisibility;
    originVisible_ = view.originVisible;
    groundVisible_ = view.groundVisible;
    viewport_->setOriginVisible(originVisible_);
    viewport_->setGroundVisible(groundVisible_);
    savedCamera_ = view.camera;
}

// Deja la ventana sin sketch ni herramienta en curso (lo que no se terminó se
// descarta: el documento solo tiene lo ya confirmado).
void MainWindow::abandonEditing()
{
    clearNavigation();
    if (tool_) {
        finishTool(false);
    }
    if (sketch_) {
        sketch_.reset();
        editingSketch_.reset();
        timeline_->setEditing(std::nullopt);
        sketchStatusLabel_->clear();
        statusBar()->clearMessage();
    }
    if (viewport_->isPickingSketchPlane()) {
        viewport_->cancelPickSketchPlane();
        statusBar()->clearMessage();
    }
    refreshModel();
    updateSketchActions();
    updateUndoActions();
}

// true si se puede seguir (se guardó, se descartó o no había nada).
bool MainWindow::maybeSave()
{
    if (!hasUnsavedWork() && !isEditing()) {
        return true;
    }
    const QString name = QString::fromStdString(document_.name());
    QMessageBox box(QMessageBox::Warning, QStringLiteral("NufioCAD"),
                    QStringLiteral("¿Guardar los cambios de «%1»?").arg(name), QMessageBox::NoButton, this);
    if (isEditing()) {
        box.setInformativeText(QStringLiteral("Lo que estás editando sin terminar (sketch o herramienta) no se guardará."));
    } else {
        box.setInformativeText(QStringLiteral("Si no los guardas, se perderán."));
    }
    QPushButton* save = box.addButton(QStringLiteral("Guardar"), QMessageBox::AcceptRole);
    QPushButton* discard = box.addButton(QStringLiteral("No guardar"), QMessageBox::DestructiveRole);
    box.addButton(QStringLiteral("Cancelar"), QMessageBox::RejectRole);
    box.setDefaultButton(save);
    box.exec();
    if (box.clickedButton() == static_cast<QAbstractButton*>(save)) {
        return saveDocument();
    }
    return box.clickedButton() == static_cast<QAbstractButton*>(discard);
}

void MainWindow::newDocument()
{
    if (!maybeSave()) {
        return;
    }
    ++documentGeneration_;
    abandonEditing();
    recovery_->discard();
    document_.restore("Sin título", {}, 0, 1);
    applyView(io::ViewState{});
    commands_.clear();
    commands_.markSaved();
    path_.clear();
    viewDirty_ = false;
    updateTitle();
    refreshModel();
    viewport_->resetView();
}

void MainWindow::openDocument()
{
    if (!maybeSave()) {
        return;
    }
    const QString file = QFileDialog::getOpenFileName(this, QStringLiteral("Abrir"), lastDirectory(), fileFilter());
    if (!file.isEmpty()) {
        openPath(toPath(file));
    }
}

bool MainWindow::openPath(const std::filesystem::path& path, bool recovered, const std::filesystem::path& original)
{
    ++documentGeneration_;
    abandonEditing();
    io::ViewState view;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QString error;
    try {
        io::loadDocument(path, document_, view);
    } catch (const std::exception& e) {
        error = QString::fromUtf8(e.what());
    }
    QApplication::restoreOverrideCursor();
    if (!error.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Abrir"),
                             QStringLiteral("No se pudo abrir «%1».\n\n%2").arg(fromPath(path.filename()), error));
        return false;
    }

    applyView(view);
    commands_.clear();
    commands_.markSaved();
    path_ = recovered ? original : path;
    viewDirty_ = recovered;  // lo recuperado sigue sin guardarse
    if (!recovered) {
        recovery_->discard();
        rememberDirectory(fromPath(path));
        RecentFiles::add(fromPath(path));
    }
    updateTitle();
    refreshModel();
    // El modelo se calcula en segundo plano: encuadre y resumen cuando esté listo.
    whenComputed([this, path] {
        if (savedCamera_.size() == 11) {
            // El diseño guardó cómo se miraba: se vuelve a esa vista en vez de encuadrar todo.
            const std::vector<double>& c = savedCamera_;
            viewport_->setCameraState({true, {c[0], c[1], c[2]}, {c[3], c[4], c[5]}, {c[6], c[7], c[8]}, c[9], c[10] > 0.5});
        } else {
            viewport_->fitAll();
        }
        int failed = 0;
        for (std::size_t i = 0; i < document_.featureCount(); ++i) {
            failed += document_.status(document_.feature(i).id()).health == document::Health::Error ? 1 : 0;
        }
        statusBar()->showMessage(failed == 0 ? QStringLiteral("Abierto %1").arg(QDir::toNativeSeparators(fromPath(path)))
                                             : QStringLiteral("Abierto con %1 operación(es) con errores: míralas en el timeline").arg(failed),
                                 failed == 0 ? 6000 : 15000);
    });
    return true;
}

bool MainWindow::saveDocument()
{
    return path_.empty() ? saveDocumentAs() : writeTo(path_);
}

bool MainWindow::saveDocumentAs()
{
    const QString suggested = path_.empty()
                                  ? QDir(lastDirectory()).filePath(QString::fromStdString(document_.name()) + QLatin1Char('.')
                                                                   + QLatin1String(kExtension))
                                  : fromPath(path_);
    QString file = QFileDialog::getSaveFileName(this, QStringLiteral("Guardar como"), suggested, fileFilter());
    if (file.isEmpty()) {
        return false;
    }
    if (QFileInfo(file).suffix().isEmpty()) {
        file += QLatin1Char('.') + QLatin1String(kExtension);
    }
    return writeTo(toPath(file));
}

// Imagen del visor para la lista de recientes (PNG); vacía si no se pudo.
std::string MainWindow::thumbnailPng()
{
    if (document_.result().bodies.empty()) {
        return {};
    }
    QImage image = viewport_->grabFramebuffer();
    if (image.isNull()) {
        return {};
    }
    image = image.scaled(QSize(400, 250), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    if (!image.save(&buffer, "PNG")) {
        return {};
    }
    return bytes.toStdString();
}

void MainWindow::rebuildRecentMenu()
{
    recentMenu_->clear();
    const QStringList paths = RecentFiles::list();
    for (const QString& path : paths) {
        QAction* action = recentMenu_->addAction(QFileInfo(path).fileName());
        action->setStatusTip(QDir::toNativeSeparators(path));
        connect(action, &QAction::triggered, this, [this, path] { openRecent(path); });
    }
    if (paths.isEmpty()) {
        recentMenu_->addAction(QStringLiteral("(vacío)"))->setEnabled(false);
    } else {
        recentMenu_->addSeparator();
        recentMenu_->addAction(QStringLiteral("Borrar la lista"), this, [] { RecentFiles::clear(); });
    }
}

void MainWindow::openRecent(const QString& path)
{
    if (!QFileInfo::exists(path)) {
        QMessageBox::warning(this, QStringLiteral("Abrir"),
                             QStringLiteral("«%1» ya no existe en esa ubicación. Se quita de la lista.")
                                 .arg(QDir::toNativeSeparators(path)));
        RecentFiles::remove(path);
        if (startScreen_ != nullptr && startScreen_->isVisible()) {
            startScreen_->reload();
        }
        return;
    }
    if (maybeSave()) {
        openPath(toPath(path));
    }
}

bool MainWindow::writeTo(const std::filesystem::path& path)
{
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QString error;
    try {
        io::saveDocument(path, document_, currentView(), thumbnailPng());
    } catch (const std::exception& e) {
        error = QString::fromUtf8(e.what());
    }
    QApplication::restoreOverrideCursor();
    if (!error.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Guardar"),
                             QStringLiteral("No se pudo guardar. El archivo anterior, si había, sigue intacto.\n\n%1")
                                 .arg(error));
        return false;
    }
    path_ = path;
    commands_.markSaved();
    viewDirty_ = false;
    recovery_->discard();
    rememberDirectory(fromPath(path));
    RecentFiles::add(fromPath(path));
    updateTitle();
    statusBar()->showMessage(isEditing() ? QStringLiteral("Guardado; lo que estás editando sin terminar no se incluyó")
                                         : QStringLiteral("Guardado en %1").arg(QDir::toNativeSeparators(fromPath(path))),
                             6000);
    return true;
}

void MainWindow::importStep()
{
    if (isEditing()) {
        return;
    }
    const QString file = QFileDialog::getOpenFileName(this, QStringLiteral("Importar STEP"), lastDirectory(),
                                                      QStringLiteral("Archivos STEP (*.step *.stp);;Todos los archivos (*)"));
    if (!file.isEmpty()) {
        importStepFile(toPath(file));
    }
}

// Vuelve a leer del disco un STEP ya importado (que cambió desde entonces) y sustituye el sólido de esa importación; lo que se
// hizo después sobre él se rehace solo. Se puede deshacer (O6).
void MainWindow::reimportStep()
{
    if (isEditing()) {
        return;
    }
    if (importRun_.running()) {
        statusBar()->showMessage(QStringLiteral("Ya se está importando un STEP; espera a que termine"), 4000);
        return;
    }
    std::vector<const document::ImportFeature*> imports;
    for (std::size_t i = 0; i < document_.featureCount(); ++i) {
        if (const auto* feature = dynamic_cast<const document::ImportFeature*>(&document_.feature(i)); feature != nullptr) {
            imports.push_back(feature);
        }
    }
    if (imports.empty()) {
        statusBar()->showMessage(QStringLiteral("Este diseño no tiene ninguna importación de STEP"), 6000);
        return;
    }
    const document::ImportFeature* chosen = imports.front();
    if (imports.size() > 1) {
        QStringList names;
        for (const auto* feature : imports) {
            names << QString::fromStdString(feature->name());
        }
        bool ok = false;
        const QString picked = QInputDialog::getItem(this, QStringLiteral("Actualizar importación"), QStringLiteral("¿Cuál?"), names, 0, false, &ok);
        if (!ok) {
            return;
        }
        chosen = imports[std::size_t(names.indexOf(picked))];
    }
    std::filesystem::path path = chosen->sourcePath.empty() ? std::filesystem::path{} : toPath(QString::fromStdString(chosen->sourcePath));
    std::error_code ignored;
    if (path.empty() || !std::filesystem::exists(path, ignored)) {
        const QString file = QFileDialog::getOpenFileName(this, QStringLiteral("¿Dónde está ahora «%1»?").arg(QString::fromStdString(chosen->source)),
                                                          lastDirectory(), QStringLiteral("Archivos STEP (*.step *.stp);;Todos los archivos (*)"));
        if (file.isEmpty()) {
            return;
        }
        path = toPath(file);
    }
    struct Outcome {
        geometry::StepImport imported;
        QString error;
    };
    auto outcome = std::make_shared<Outcome>();
    const std::uint64_t generation = documentGeneration_;
    const document::FeatureId id = chosen->id();
    const QString name = QString::fromStdString(chosen->name());
    statusBar()->showMessage(QStringLiteral("Actualizando %1 desde su archivo… puedes seguir mirando el diseño").arg(name));
    importRun_.start(
        [outcome, path] {
            try {
                outcome->imported = geometry::readStep(path);
            } catch (const std::exception& e) {
                outcome->error = QString::fromUtf8(e.what());
            } catch (...) {
                outcome->error = QStringLiteral("error desconocido al leer el archivo");
            }
        },
        [this, outcome, path, id, name, generation] {
            updateBusyBadge();
            statusBar()->clearMessage();
            if (!outcome->error.isEmpty()) {
                QMessageBox::warning(this, QStringLiteral("Actualizar importación"), QStringLiteral("No se pudo leer «%1».\n\n%2").arg(name, outcome->error));
                return;
            }
            const auto* current = dynamic_cast<const document::ImportFeature*>(document_.findFeature(id));
            if (generation != documentGeneration_ || current == nullptr || isEditing()) {
                statusBar()->showMessage(QStringLiteral("Se descartó la actualización de %1: el diseño cambió mientras se leía").arg(name), 10000);
                return;
            }
            auto updated = std::make_unique<document::ImportFeature>(*current);
            updated->brep = std::make_shared<const std::string>(std::move(outcome->imported.brep));
            updated->notes = std::move(outcome->imported.warnings);
            updated->sourcePath = QString(fromPath(std::filesystem::absolute(path))).toStdString();
            commands_.push(std::make_unique<commands::EditFeature>(id, std::move(updated), "Actualizar importación"));
            statusBar()->showMessage(QStringLiteral("«%1» actualizado desde su archivo").arg(name), 8000);
        });
    updateBusyBadge();
}

// Agrega el sólido del STEP como una operación más del historial (se puede
// deshacer). El diseño guarda el sólido ya reparado, no la ruta del archivo.
// Leer y reparar el STEP (lo que tarda) va en un hilo aparte; la ventana sigue viva y el historial se toca
// al terminar. Devuelve true si la importación empezó.
bool MainWindow::importStepFile(const std::filesystem::path& path)
{
    if (isEditing()) {
        return false;
    }
    if (importRun_.running()) {
        statusBar()->showMessage(QStringLiteral("Ya se está importando un STEP; espera a que termine"), 4000);
        return false;
    }
    struct Outcome {
        geometry::StepImport imported;
        QString error;
    };
    auto outcome = std::make_shared<Outcome>();
    const std::uint64_t generation = documentGeneration_;
    const QString name = fromPath(path.filename());
    statusBar()->showMessage(QStringLiteral("Importando %1… puedes seguir mirando el diseño").arg(name));
    importRun_.start(
        [outcome, path] {
            try {
                outcome->imported = geometry::readStep(path);
            } catch (const std::exception& e) {
                outcome->error = QString::fromUtf8(e.what());
            } catch (...) {
                outcome->error = QStringLiteral("error desconocido al leer el archivo");
            }
        },
        [this, outcome, path, name, generation] {
            updateBusyBadge();
            statusBar()->clearMessage();
            if (!outcome->error.isEmpty()) {
                QMessageBox::warning(this, QStringLiteral("Importar STEP"),
                                     QStringLiteral("No se pudo importar «%1».\n\n%2").arg(name, outcome->error));
                return;
            }
            if (generation != documentGeneration_) {
                statusBar()->showMessage(QStringLiteral("Se descartó la importación de %1: mientras tanto se abrió otro diseño").arg(name), 10000);
                return;
            }
            if (isEditing()) {
                statusBar()->showMessage(QStringLiteral("«%1» ya se leyó, pero hay un sketch o una herramienta abiertos: ciérralo y vuelve a importarlo").arg(name), 15000);
                return;
            }
            auto feature = std::make_unique<document::ImportFeature>();
            feature->source = name.toStdString();
            feature->sourcePath = QString(fromPath(std::filesystem::absolute(path))).toStdString();
            feature->setName(QString(fromPath(path.stem())).toStdString());
            feature->brep = std::make_shared<const std::string>(std::move(outcome->imported.brep));
            feature->notes = std::move(outcome->imported.warnings);
            const bool wasEmpty = document_.result().bodies.empty();
            auto command = std::make_unique<commands::AddFeature>(std::move(feature));
            const commands::AddFeature* raw = command.get();
            commands_.push(std::move(command));
            rememberDirectory(fromPath(path));

            whenComputed([this, id = raw->id(), name, wasEmpty] {
                const document::FeatureStatus status = document_.status(id);
                if (status.health == document::Health::Error) {
                    statusBar()->showMessage(QStringLiteral("La importación falló: %1").arg(QString::fromStdString(status.message)), 15000);
                    return;
                }
                if (wasEmpty) {
                    viewport_->fitAll();
                }
                statusBar()->showMessage(QStringLiteral("Importado %1 (%2 cuerpo(s))").arg(name).arg(document_.result().bodies.size()), 8000);
            });
        });
    updateBusyBadge();
    return true;
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasUrls()) {
        for (const QUrl& url : event->mimeData()->urls()) {
            if (url.isLocalFile() && (isDesign(url.toLocalFile()) || isStep(url.toLocalFile()))) {
                event->acceptProposedAction();
                return;
            }
        }
    }
}

void MainWindow::dropEvent(QDropEvent* event)
{
    for (const QUrl& url : event->mimeData()->urls()) {
        if (url.isLocalFile() && (isDesign(url.toLocalFile()) || isStep(url.toLocalFile()))) {
            event->acceptProposedAction();
            // Fuera del evento: los diálogos no deben abrirse dentro de un drop.
            const QString file = url.toLocalFile();
            QMetaObject::invokeMethod(this, [this, file] {
                if (isStep(file)) {
                    importStepFile(toPath(file));
                } else if (maybeSave()) {
                    openPath(toPath(file));
                }
            }, Qt::QueuedConnection);
            return;
        }
    }
}

void MainWindow::saveRecovery()
{
    if (hasUnsavedWork()) {
        recovery_->writeAsync(document_, currentView(), QString::fromStdU16String(path_.u16string()));
    } else {
        recovery_->discard();
    }
}

void MainWindow::offerRecovery()
{
    const std::vector<Recovery::Orphan> orphans = Recovery::orphans();
    if (orphans.empty() || hasUnsavedWork() || !path_.empty()) {
        return;
    }
    // Un aviso discreto sobre el lienzo (U2): no tapa la ventana ni exige contestar para seguir trabajando.
    const Recovery::Orphan newest = orphans.front();
    const QString name = newest.name.isEmpty() ? QStringLiteral("Sin título") : newest.name;
    delete recoveryBar_;
    recoveryBar_ = new QFrame(viewport_);
    recoveryBar_->setObjectName(QStringLiteral("recoveryBar"));
    recoveryBar_->setStyleSheet(QStringLiteral(
        "#recoveryBar { background: rgba(32, 38, 48, 235); border: 1px solid #F2A34B; border-radius: 6px; }"
        "#recoveryBar QLabel { color: #E6EAF0; }"
        "#recoveryBar QPushButton { padding: 3px 12px; }"));
    auto* row = new QHBoxLayout(recoveryBar_);
    row->setContentsMargins(12, 6, 8, 6);
    row->addWidget(new QLabel(QStringLiteral("La sesión anterior se cerró sin guardar «%1» (copia del %2).")
                                  .arg(name, QLocale().toString(newest.when, QLocale::ShortFormat))));
    auto* restore = new QPushButton(QStringLiteral("Restaurar"));
    auto* discard = new QPushButton(QStringLiteral("Descartar"));
    auto* later = new QPushButton(QStringLiteral("×"));
    later->setToolTip(QStringLiteral("Decidir después: la copia se conserva y se avisará la próxima vez"));
    later->setFixedWidth(28);
    row->addWidget(restore);
    row->addWidget(discard);
    row->addWidget(later);
    const auto close = [this] {
        recoveryBar_->deleteLater();
        recoveryBar_ = nullptr;
    };
    connect(restore, &QPushButton::clicked, this, [this, newest, close] {
        close();
        if (openPath(newest.file, true, newest.original.isEmpty() ? std::filesystem::path{} : toPath(newest.original))) {
            Recovery::remove(newest);
            statusBar()->showMessage(QStringLiteral("Diseño restaurado: guárdalo para no perderlo otra vez"), 15000);
        }
    });
    connect(discard, &QPushButton::clicked, this, [newest, close] {
        Recovery::remove(newest);
        close();
    });
    connect(later, &QPushButton::clicked, this, close);
    recoveryBar_->adjustSize();
    placeRecoveryBar();
    recoveryBar_->show();
    recoveryBar_->raise();
}

void MainWindow::updateStartCard()
{
    if (viewport_ == nullptr) {
        return;
    }
    const bool wanted = !startCardDismissed_ && document_.featureCount() == 0 && !tool_ && !sketch_ &&
                        QSettings().value(QStringLiteral("ui/startCard"), true).toBool();
    if (!wanted) {
        if (startCard_ != nullptr) {
            startCard_->hide();
        }
        return;
    }
    if (startCard_ == nullptr) {
        startCard_ = new QFrame(viewport_);
        startCard_->setObjectName(QStringLiteral("startCard"));
        startCard_->setStyleSheet(QStringLiteral(
            "#startCard { background: palette(window); border: 1px solid palette(mid); border-radius: 8px; }"
            "#startCard QToolButton { padding: 4px 8px; text-align: left; }"));
        auto* column = new QVBoxLayout(startCard_);
        column->setContentsMargins(14, 10, 14, 10);
        column->setSpacing(4);
        auto* header = new QHBoxLayout();
        auto* title = new QLabel(QStringLiteral("Empieza un diseño"));
        QFont bold = title->font();
        bold.setBold(true);
        bold.setPointSizeF(bold.pointSizeF() * 1.15);
        title->setFont(bold);
        auto* close = new QToolButton();
        close->setText(QStringLiteral("×"));
        close->setAutoRaise(true);
        close->setToolTip(QStringLiteral("Cerrar esta tarjeta (vuelve a salir al abrir un diseño nuevo)"));
        connect(close, &QToolButton::clicked, this, [this] {
            startCardDismissed_ = true;
            updateStartCard();
        });
        header->addWidget(title);
        header->addStretch();
        header->addWidget(close);
        column->addLayout(header);
        // Dos columnas: lo que se dibuja y se abre a la izquierda, las piezas de los formularios a la derecha.
        auto* columns = new QHBoxLayout();
        auto* left = new QVBoxLayout();
        auto* right = new QVBoxLayout();
        left->setSpacing(2);
        right->setSpacing(2);
        columns->addLayout(left, 1);
        columns->addLayout(right, 1);
        column->addLayout(columns);
        const auto section = [&](QVBoxLayout* into, const QString& text) {
            auto* label = new QLabel(text);
            label->setForegroundRole(QPalette::PlaceholderText);
            into->addSpacing(4);
            into->addWidget(label);
        };
        const auto addButton = [&](QVBoxLayout* into, const QIcon& icon, const QString& text, const QString& tip,
                                   std::function<void()> run) {
            auto* b = new QToolButton();
            b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
            b->setIcon(icon);
            b->setText(text);
            b->setToolTip(tip);
            b->setAutoRaise(true);
            b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            connect(b, &QToolButton::clicked, this, [run = std::move(run)] { run(); });
            into->addWidget(b);
        };
        const auto button = [&](QVBoxLayout* into, QAction* action, const QString& text = QString()) {
            if (action != nullptr) {
                addButton(into, action->icon(), text.isEmpty() ? action->text().remove(QLatin1Char('&')) : text, action->toolTip(),
                          [action] { action->trigger(); });
            }
        };
        section(left, QStringLiteral("Dibujar y modelar"));
        button(left, createSketchAction_, QStringLiteral("Crear un sketch (S)"));
        button(left, boxAction_, QStringLiteral("Una caja (B)"));
        button(left, cylinderAction_, QStringLiteral("Un cilindro (Mayús+C)"));
        button(left, gearAction_, QStringLiteral("Un engranaje (G)"));
        button(left, threadAction_, QStringLiteral("Una rosca (Mayús+T)"));
        section(left, QStringLiteral("Archivos"));
        addButton(left, QIcon(), QStringLiteral("Abrir un diseño…"), QString(), [this] { openDocument(); });
        button(left, importAction_, QStringLiteral("Importar un STEP…"));
        left->addStretch();
        section(right, QStringLiteral("Piezas hechas con un formulario"));
        for (QAction* generator : generatorActions_) {
            button(right, generator);
        }
        right->addStretch();
        auto* never = new QCheckBox(QStringLiteral("No mostrar más esta tarjeta"));
        connect(never, &QCheckBox::toggled, this, [this](bool on) {
            QSettings().setValue(QStringLiteral("ui/startCard"), !on);
            if (on) {
                startCardDismissed_ = true;
                updateStartCard();
            }
        });
        column->addSpacing(6);
        column->addWidget(never);
    }
    startCard_->setVisible(placeStartCard());
    startCard_->raise();
}

bool MainWindow::placeStartCard()
{
    if (startCard_ == nullptr) {
        return false;
    }
    startCard_->setMinimumWidth(0);
    startCard_->setMaximumWidth(QWIDGETSIZE_MAX);
    startCard_->adjustSize();
    const int width = std::min(std::max(460, startCard_->sizeHint().width()), std::max(220, viewport_->width() - 24));
    startCard_->setFixedWidth(width);
    startCard_->adjustSize();
    // Si la vista es pequeña no cabe entera: se oculta (el lienzo importa más que la tarjeta).
    const bool fits = width <= viewport_->width() - 24 && startCard_->height() + 24 <= viewport_->height();
    startCard_->move(std::max(12, (viewport_->width() - width) / 2), std::max(12, (viewport_->height() - startCard_->height()) / 2));
    return fits;
}

void MainWindow::placeRecoveryBar()
{
    if (recoveryBar_ == nullptr) {
        return;
    }
    recoveryBar_->adjustSize();
    const int width = std::min(recoveryBar_->sizeHint().width(), std::max(200, viewport_->width() - 24));
    recoveryBar_->setFixedWidth(width);
    recoveryBar_->move((viewport_->width() - width) / 2, 12);
}

void MainWindow::openFromCommandLine(const QString& file)
{
    openPath(toPath(file));
}

} // namespace nuf::app
