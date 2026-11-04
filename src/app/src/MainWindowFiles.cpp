// Archivo: Nuevo, Abrir, Guardar y Guardar como, y el aviso de cambios sin guardar.

#include "MainWindow.hpp"
#include "ModelPanels.hpp"

#include <nuf/io/DocumentFile.hpp>
#include <nuf/render/Viewport.hpp>

#include <QApplication>
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

bool MainWindow::openPath(const std::filesystem::path& path)
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
    path_ = path;
    viewDirty_ = false;
    rememberDirectory(fromPath(path));
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

bool MainWindow::writeTo(const std::filesystem::path& path)
{
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QString error;
    try {
        io::saveDocument(path, document_, currentView());
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
    rememberDirectory(fromPath(path));
    updateTitle();
    statusBar()->showMessage(isEditing() ? QStringLiteral("Guardado; lo que estás editando sin terminar no se incluyó")
                                         : QStringLiteral("Guardado en %1").arg(QDir::toNativeSeparators(fromPath(path))),
                             6000);
    return true;
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasUrls()) {
        for (const QUrl& url : event->mimeData()->urls()) {
            if (url.isLocalFile() && url.toLocalFile().endsWith(QLatin1String(".nuf"), Qt::CaseInsensitive)) {
                event->acceptProposedAction();
                return;
            }
        }
    }
}

void MainWindow::dropEvent(QDropEvent* event)
{
    for (const QUrl& url : event->mimeData()->urls()) {
        if (url.isLocalFile() && url.toLocalFile().endsWith(QLatin1String(".nuf"), Qt::CaseInsensitive)) {
            event->acceptProposedAction();
            // Fuera del evento: el diálogo de guardar no debe abrirse dentro de un drop.
            const QString file = url.toLocalFile();
            QMetaObject::invokeMethod(this, [this, file] {
                if (maybeSave()) {
                    openPath(toPath(file));
                }
            }, Qt::QueuedConnection);
            return;
        }
    }
}

void MainWindow::openFromCommandLine(const QString& file)
{
    openPath(toPath(file));
}

} // namespace nuf::app
