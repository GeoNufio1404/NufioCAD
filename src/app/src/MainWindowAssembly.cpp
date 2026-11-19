// Fase 21: lo que se hace con los cuerpos y componentes como piezas de un conjunto: duplicar, fijar, agrupar y mover.

#include "MainWindowCommon.hpp"

#include <nuf/commands/DuplicateBodies.hpp>

#include <QClipboard>
#include <QGuiApplication>
#include <QShortcut>

#include <algorithm>

namespace nuf::app {

void MainWindow::connectAssembly()
{
    connect(navigator_, &NavigatorWidget::duplicateRequested, this,
            [this](const std::vector<std::string>& keys) { duplicateBodies(keys); });
    connect(navigator_, &NavigatorWidget::fixRequested, this,
            [this](const std::vector<std::string>& keys, bool fix) { setBodiesFixed(keys, fix); });
    connect(navigator_, &NavigatorWidget::componentRequested, this,
            [this](const std::vector<std::string>& keys) { groupBodies(keys); });
    connect(navigator_, &NavigatorWidget::ungroupRequested, this, [this](const std::vector<std::string>& keys) {
        std::map<std::string, document::BodyInfo> info = document_.bodyInfo();
        for (const std::string& key : keys) {
            info[key].component.clear();
        }
        commands_.push(std::make_unique<commands::SetBodyInfo>(info));
        statusBar()->showMessage(QStringLiteral("Sacado del componente"), 4000);
    });
    // Ctrl+C: con Medir activo copia el resultado al portapapeles; si no, copia los cuerpos elegidos (Ctrl+V los pega
    // como un Duplicar). Ctrl+D duplica sin pasar por el portapapeles.
    auto* copy = new QShortcut(QKeySequence::Copy, viewport_);
    copy->setContext(Qt::WidgetShortcut);
    connect(copy, &QShortcut::activated, this, [this] {
        if (measureAction_ != nullptr && measureAction_->isChecked()) {
            if (!lastMeasureText_.isEmpty()) {
                QGuiApplication::clipboard()->setText(lastMeasureText_);
                statusBar()->showMessage(QStringLiteral("Medida copiada al portapapeles"), 4000);
            }
        } else if (!sketch_ && !tool_) {
            bodyClipboard_ = selectedBodyKeys();
            if (!bodyClipboard_.empty()) {
                statusBar()->showMessage(QStringLiteral("Copiado: Ctrl+V lo pega"), 4000);
            }
        }
    });
    auto* paste = new QShortcut(QKeySequence::Paste, viewport_);
    paste->setContext(Qt::WidgetShortcut);
    connect(paste, &QShortcut::activated, this, [this] {
        if (!sketch_ && !tool_ && !bodyClipboard_.empty()) {
            duplicateBodies(bodyClipboard_);
        }
    });
    connect(navigator_, &NavigatorWidget::moveRequested, this, [this](const std::vector<std::string>& keys) {
        if (!sketch_ && !tool_) {
            startTool(std::make_unique<MoveTool>(document_, *viewport_, std::nullopt, keys));
        }
    });
}

std::vector<std::string> MainWindow::selectedBodyKeys() const
{
    // Lo elegido en el navegador y, si no hay nada, los cuerpos de las caras y aristas elegidas en la vista.
    std::vector<std::string> keys = navigator_ != nullptr ? navigator_->selectedBodies() : std::vector<std::string>{};
    if (keys.empty() && viewport_ != nullptr) {
        const render::ModelSelection selection = viewport_->modelSelection();
        const auto add = [&](const std::string& key) {
            if (std::ranges::find(keys, key) == keys.end()) {
                keys.push_back(key);
            }
        };
        for (const auto& face : selection.faces) {
            add(face.body);
        }
        for (const auto& edge : selection.edges) {
            add(edge.body);
        }
    }
    return keys;
}

void MainWindow::duplicateBodies(const std::vector<std::string>& keys)
{
    if (keys.empty() || sketch_ || tool_) {
        statusBar()->showMessage(QStringLiteral("Elige antes un cuerpo o un componente en el navegador (o una cara en la vista)"), 5000);
        return;
    }
    // La copia queda al lado: corrida en X lo que mide el conjunto más un poco de aire, para que se vea.
    std::optional<geometry::Box> all;
    for (const document::Body& body : document_.result().bodies) {
        if (std::ranges::find(keys, body.key) == keys.end() || body.shape.isNull()) {
            continue;
        }
        const geometry::Box box = body.shape.bounds();
        if (!all) {
            all = box;
        } else {
            all->min = {std::min(all->min.x, box.min.x), std::min(all->min.y, box.min.y), std::min(all->min.z, box.min.z)};
            all->max = {std::max(all->max.x, box.max.x), std::max(all->max.y, box.max.y), std::max(all->max.z, box.max.z)};
        }
    }
    const double width = all ? all->max.x - all->min.x : 0.0;
    commands_.push(std::make_unique<commands::DuplicateBodies>(keys, sketch::Vec3{width + 5.0, 0.0, 0.0}));
    statusBar()->showMessage(keys.size() == 1 ? QStringLiteral("Duplicado") : QStringLiteral("Duplicados %1 cuerpos").arg(keys.size()), 4000);
}

void MainWindow::setBodiesFixed(const std::vector<std::string>& keys, bool fix)
{
    std::map<std::string, document::BodyInfo> info = document_.bodyInfo();
    for (const std::string& key : keys) {
        info[key].fixed = fix;
    }
    commands_.push(std::make_unique<commands::SetBodyInfo>(info));
    statusBar()->showMessage(fix ? QStringLiteral("Fijado: las uniones y alineaciones ya no lo mueven")
                                 : QStringLiteral("Soltado"),
                             4000);
}

void MainWindow::groupBodies(const std::vector<std::string>& keys)
{
    std::map<std::string, document::BodyInfo> info = document_.bodyInfo();
    const std::string suggestion = document::numberedComponent(info, "Componente");
    bool ok = false;
    const QString name = QInputDialog::getText(this, QStringLiteral("Crear componente"),
                                               QStringLiteral("Nombre del componente con %1 cuerpo(s):").arg(keys.size()),
                                               QLineEdit::Normal, QString::fromStdString(suggestion), &ok)
                             .trimmed();
    if (!ok || name.isEmpty()) {
        return;
    }
    for (const std::string& key : keys) {
        info[key].component = name.toStdString();
    }
    commands_.push(std::make_unique<commands::SetBodyInfo>(info));
    statusBar()->showMessage(QStringLiteral("Componente «%1» creado").arg(name), 4000);
}

} // namespace nuf::app
