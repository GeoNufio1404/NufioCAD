// Archivo: Nuevo, Abrir, Guardar y Guardar como, y el aviso de cambios sin guardar.

#include "MainWindow.hpp"
#include "ModelPanels.hpp"
#include "Recovery.hpp"
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
#include <QLabel>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QSettings>
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
    return view;
}

void MainWindow::applyView(const io::ViewState& view)
{
    hiddenBodies_ = view.hiddenBodies;
    sketchVisibility_ = view.sketchVisibility;
    originVisible_ = view.originVisible;
    groundVisible_ = view.groundVisible;
    viewport_->setOriginVisible(originVisible_);
    viewport_->setGroundVisible(groundVisible_);
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
    viewport_->fitAll();

    int failed = 0;
    for (std::size_t i = 0; i < document_.featureCount(); ++i) {
        failed += document_.status(document_.feature(i).id()).health == document::Health::Error ? 1 : 0;
    }
    statusBar()->showMessage(failed == 0 ? QStringLiteral("Abierto %1").arg(QDir::toNativeSeparators(fromPath(path)))
                                         : QStringLiteral("Abierto con %1 operación(es) con errores: míralas en el timeline").arg(failed),
                             failed == 0 ? 6000 : 15000);
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

// Agrega el sólido del STEP como una operación más del historial (se puede
// deshacer). El diseño guarda el sólido ya reparado, no la ruta del archivo.
bool MainWindow::importStepFile(const std::filesystem::path& path)
{
    if (isEditing()) {
        return false;
    }
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QString error;
    geometry::StepImport imported;
    try {
        imported = geometry::readStep(path);
    } catch (const std::exception& e) {
        error = QString::fromUtf8(e.what());
    }
    QApplication::restoreOverrideCursor();
    if (!error.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Importar STEP"),
                             QStringLiteral("No se pudo importar «%1».\n\n%2").arg(fromPath(path.filename()), error));
        return false;
    }

    auto feature = std::make_unique<document::ImportFeature>();
    feature->source = QString(fromPath(path.filename())).toStdString();
    feature->setName(QString(fromPath(path.stem())).toStdString());
    feature->brep = std::make_shared<const std::string>(std::move(imported.brep));
    feature->notes = std::move(imported.warnings);
    const bool wasEmpty = document_.result().bodies.empty();
    auto command = std::make_unique<commands::AddFeature>(std::move(feature));
    const commands::AddFeature* raw = command.get();
    QApplication::setOverrideCursor(Qt::WaitCursor);
    commands_.push(std::move(command));
    QApplication::restoreOverrideCursor();
    rememberDirectory(fromPath(path));

    const document::FeatureStatus status = document_.status(raw->id());
    if (status.health == document::Health::Error) {
        statusBar()->showMessage(QStringLiteral("La importación falló: %1").arg(QString::fromStdString(status.message)), 15000);
        return true;
    }
    if (wasEmpty) {
        viewport_->fitAll();
    }
    statusBar()->showMessage(QStringLiteral("Importado %1 (%2 cuerpo(s))")
                                 .arg(fromPath(path.filename()))
                                 .arg(document_.result().bodies.size()),
                             8000);
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
        recovery_->write(document_, currentView(), QString::fromStdU16String(path_.u16string()));
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
    const Recovery::Orphan& newest = orphans.front();
    const QString name = newest.name.isEmpty() ? QStringLiteral("Sin título") : newest.name;
    QMessageBox box(QMessageBox::Question, QStringLiteral("NufioCAD"),
                    QStringLiteral("La sesión anterior se cerró sin guardar «%1».").arg(name), QMessageBox::NoButton, this);
    box.setInformativeText(QStringLiteral("Hay una copia de recuperación del %1. ¿Quieres restaurarla?")
                               .arg(QLocale().toString(newest.when, QLocale::ShortFormat)));
    QPushButton* restore = box.addButton(QStringLiteral("Restaurar"), QMessageBox::AcceptRole);
    QPushButton* discard = box.addButton(QStringLiteral("Descartar"), QMessageBox::DestructiveRole);
    box.addButton(QStringLiteral("Decidir después"), QMessageBox::RejectRole);
    box.setDefaultButton(restore);
    box.exec();
    if (box.clickedButton() == static_cast<QAbstractButton*>(restore)) {
        if (openPath(newest.file, true, newest.original.isEmpty() ? std::filesystem::path{} : toPath(newest.original))) {
            Recovery::remove(newest);
            statusBar()->showMessage(QStringLiteral("Diseño restaurado: guárdalo para no perderlo otra vez"), 15000);
        }
    } else if (box.clickedButton() == static_cast<QAbstractButton*>(discard)) {
        Recovery::remove(newest);
    }
}

void MainWindow::openFromCommandLine(const QString& file)
{
    openPath(toPath(file));
}

} // namespace nuf::app
