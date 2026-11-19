// Fase 21: lo que se hace con los cuerpos y componentes como piezas de un conjunto: duplicar, fijar, agrupar y mover.

#include "MainWindowCommon.hpp"

#include <nuf/commands/DuplicateBodies.hpp>

#include <QClipboard>
#include <QGuiApplication>
#include <QShortcut>

#include <algorithm>
#include <cmath>
#include <numbers>

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
    connectBodyDrag();
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


// --- Arrastrar piezas (A5 y A7) ------------------------------------------------------------------
// Con el modo «Arrastrar piezas» encendido, agarrar un cuerpo y arrastrar lo mueve sobre el plano de la pantalla (con
// Ctrl, pegado a la rejilla); al soltar queda guardado como una operación Mover. Si el cuerpo está colocado por una
// unión, el arrastre solo deja moverse como la unión permite: una giratoria gira, una deslizante desliza (y los
// límites de la unión mandan); al soltar queda guardado el nuevo ángulo o desplazamiento en la propia unión.

namespace {

sketch::Vec3 minus(const sketch::Vec3& a, const sketch::Vec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
double dotOf(const sketch::Vec3& a, const sketch::Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
sketch::Vec3 crossOf(const sketch::Vec3& u, const sketch::Vec3& v)
{
    return {u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z, u.x * v.y - u.y * v.x};
}

} // namespace

bool MainWindow::canDragBody(const std::string& key)
{
    if (sketch_ || tool_) {
        return false;
    }
    // Un componente se arrastra entero, y si alguno de sus cuerpos está fijo, no se mueve.
    const std::string component = document_.componentOf(key);
    for (const document::Body& body : document_.result().bodies) {
        const bool member = body.key == key || (!component.empty() && document_.componentOf(body.key) == component);
        if (member && document_.isFixed(body.key)) {
            statusBar()->showMessage(QStringLiteral("«%1» está fijo: no se arrastra (clic derecho › Soltar en el navegador)")
                                         .arg(QString::fromStdString(body.name)),
                                     5000);
            return false;
        }
    }
    return true;
}

void MainWindow::beginBodyDrag(const std::string& key, const sketch::Vec3& grab)
{
    auto drag = std::make_unique<BodyDragState>();
    drag->grab = grab;
    const document::ModelState& state = document_.result();
    // ¿La coloca una unión? (la última que la mueve)
    for (auto it = state.joints.rbegin(); it != state.joints.rend(); ++it) {
        if (std::ranges::find(it->second.moved, key) == it->second.moved.end()) {
            continue;
        }
        const auto* joint = dynamic_cast<const document::JointFeature*>(document_.findFeature(it->first));
        if (joint == nullptr || joint->suppressed()) {
            continue;
        }
        drag->joint = true;
        drag->jointId = it->first;
        drag->axis = it->second;
        drag->original = joint->clone();
        drag->startValue = it->second.dof == 1 ? joint->slide : joint->angle;
        drag->group = it->second.moved;
        break;
    }
    if (!drag->joint) {
        const std::string component = document_.componentOf(key);
        for (const document::Body& body : state.bodies) {
            if (body.key == key || (!component.empty() && document_.componentOf(body.key) == component)) {
                drag->group.push_back(body.key);
            }
        }
        // El que se agarró va primero (es el principal de la operación Mover).
        std::ranges::stable_partition(drag->group, [&](const std::string& k) { return k == key; });
        std::vector<geometry::Shape> shapes;
        for (const std::string& k : drag->group) {
            for (const document::Body& body : state.bodies) {
                if (body.key == k) {
                    shapes.push_back(body.shape);
                }
            }
        }
        viewport_->showGhost(shapes);
        viewport_->setGhostMotion({}, {}, {});
    }
    bodyDrag_ = std::move(drag);
}

void MainWindow::moveBodyDrag(const sketch::Vec3& delta, bool snap)
{
    if (!bodyDrag_) {
        return;
    }
    BodyDragState& drag = *bodyDrag_;
    if (!drag.joint) {
        sketch::Vec3 t = delta;
        if (snap) {
            const double step = std::max(viewport_->sketchGridStep(), 0.1);
            t = {std::round(t.x / step) * step, std::round(t.y / step) * step, std::round(t.z / step) * step};
        }
        drag.translation = t;
        viewport_->setGhostMotion({}, {}, t);
        return;
    }
    // Con una unión: el arrastre se convierte en el giro o el deslizamiento que ella permite.
    auto edited = drag.original->clone();
    auto& joint = static_cast<document::JointFeature&>(*edited);
    if (drag.axis.dof == 0) {
        const sketch::Vec3 dir = drag.axis.direction;
        const sketch::Vec3 v = minus(drag.grab, drag.axis.origin);
        const double along = dotOf(v, dir);
        sketch::Vec3 radial{v.x - dir.x * along, v.y - dir.y * along, v.z - dir.z * along};
        double radius = std::sqrt(dotOf(radial, radial));
        if (radius < 1e-6) {
            radius = 10.0;  // agarrado justo sobre el eje: se gira con un brazo de 10 mm
            radial = drag.axis.xAxis;
        }
        sketch::Vec3 tangent = crossOf(dir, radial);
        const double len = std::sqrt(dotOf(tangent, tangent));
        tangent = {tangent.x / len, tangent.y / len, tangent.z / len};
        double angle = dotOf(delta, tangent) / radius;
        if (snap) {
            const double step = 15.0 * std::numbers::pi / 180.0;
            angle = std::round((drag.startValue + angle) / step) * step - drag.startValue;
        }
        joint.angle = drag.startValue + angle;
    } else {
        double slide = dotOf(delta, drag.axis.xAxis);
        if (snap) {
            const double step = std::max(viewport_->sketchGridStep(), 0.1);
            slide = std::round((drag.startValue + slide) / step) * step - drag.startValue;
        }
        joint.slide = drag.startValue + slide;
    }
    drag.edited = std::move(edited);
    // En vivo: la unión se rehace con el valor de ahora (sin deshacer; al soltar se guarda una sola edición).
    document_.replaceFeature(drag.jointId, drag.edited->clone());
}

void MainWindow::endBodyDrag(bool moved)
{
    if (!bodyDrag_) {
        return;
    }
    std::unique_ptr<BodyDragState> drag = std::move(bodyDrag_);
    viewport_->hideGhost();
    viewport_->clearModelSelection();
    if (drag->joint) {
        // Se vuelve a lo de antes para que el comando guarde el cambio como una edición que se puede deshacer.
        document_.replaceFeature(drag->jointId, drag->original->clone());
        if (moved && drag->edited) {
            commands_.push(std::make_unique<commands::EditFeature>(drag->jointId, std::move(drag->edited), "Mover una unión"));
            statusBar()->showMessage(QStringLiteral("Posición de la unión guardada"), 4000);
        }
        return;
    }
    if (!moved || (std::abs(drag->translation.x) + std::abs(drag->translation.y) + std::abs(drag->translation.z)) < 1e-9) {
        return;
    }
    auto move = std::make_unique<document::MoveFeature>();
    move->body = drag->group.front();
    move->alsoMove.assign(drag->group.begin() + 1, drag->group.end());
    move->translation = drag->translation;
    commands_.push(std::make_unique<commands::AddFeature>(std::move(move)));
    statusBar()->showMessage(QStringLiteral("Posición guardada como una operación Mover"), 4000);
}

void MainWindow::connectBodyDrag()
{
    viewport_->setBodyDragFilter([this](const std::string& key) { return canDragBody(key); });
    connect(viewport_, &render::Viewport::bodyDragStarted, this,
            [this](const QString& key, const sketch::Vec3& grab) { beginBodyDrag(key.toStdString(), grab); });
    connect(viewport_, &render::Viewport::bodyDragged, this,
            [this](const QString&, const sketch::Vec3& delta, bool snap) { moveBodyDrag(delta, snap); });
    connect(viewport_, &render::Viewport::bodyDragFinished, this,
            [this](const QString&, bool moved) { endBodyDrag(moved); });
}

} // namespace nuf::app
