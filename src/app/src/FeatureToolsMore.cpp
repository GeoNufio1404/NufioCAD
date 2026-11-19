#include "FeatureTools.hpp"
#include "Theme.hpp"

#include "ExpressionSpinBox.hpp"
#include "Units.hpp"

#include <nuf/geometry/Modeling.hpp>
#include <nuf/render/Viewport.hpp>

#include <QButtonGroup>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include "FeatureToolsCommon.hpp"

namespace nuf::app {

// --- Barrido ----------------------------------------------------------------------------

SweepTool::SweepTool(document::Document& document, render::Viewport& viewport,
                     std::optional<document::FeatureId> editing)
    : FeatureTool(document, viewport, editing)
{
    if (const auto* s = dynamic_cast<const document::SweepFeature*>(original_.get())) {
        profiles_ = s->profiles;
        pathSketch_ = s->pathSketch;
        path_ = s->path;
    }
}

void SweepTool::createFields(QFormLayout* form)
{
    orientation_ = combo({QStringLiteral("Sigue la trayectoria"), QStringLiteral("Fija (paralela a sí misma)")});
    operation_ = combo({QStringLiteral("Unir"), QStringLiteral("Cortar"), QStringLiteral("Intersecar"),
                        QStringLiteral("Cuerpo nuevo")});
    if (const auto* s = dynamic_cast<const document::SweepFeature*>(original_.get())) {
        orientation_->setCurrentIndex(s->spec.followPath ? 0 : 1);
        operation_->setCurrentIndex(operationIndex(s->operation));
    } else {
        operation_->setCurrentIndex(operationIndex(before().bodies.empty() ? document::Operation::NewBody
                                                                           : document::Operation::Join));
    }
    endScale_ = new ExpressionSpinBox();
    endScale_->setRange(0.01, 100.0);
    endScale_->setDecimals(2);
    endScale_->setSingleStep(0.1);
    endScale_->setValue(1.0);
    endScale_->setKeyboardTracking(false);
    endScale_->setToolTip(QStringLiteral("Cómo de grande queda el perfil al final de la trayectoria (1 = igual, 0,5 = a la mitad): un barrido que se afina o se ensancha"));
    field("endScale", endScale_);
    if (const auto* s = dynamic_cast<const document::SweepFeature*>(original_.get())) {
        endScale_->setValue(s->spec.endScale);
    }
    form->addRow(QStringLiteral("Orientación"), orientation_);
    form->addRow(QStringLiteral("Escala al final"), endScale_);
    form->addRow(QStringLiteral("Operación"), operation_);
    for (QComboBox* box : {orientation_, operation_}) {
        connect(box, &QComboBox::currentIndexChanged, this, [this] { updatePreview(); });
    }
    connect(endScale_, &QDoubleSpinBox::valueChanged, this, [this] { updatePreview(); });
}

void SweepTool::selectionChanged(const render::ModelSelection& selection)
{
    const std::vector<document::ProfileRef> profiles = profilesOf(selection);
    if (!profiles.empty()) {
        profiles_ = profiles;
    }
    // Las curvas elegidas son la trayectoria (de un solo sketch: el de la primera).
    if (!selection.curves.empty()) {
        pathSketch_ = selection.curves.front().sketch;
        path_.clear();
        for (const render::ModelSelection::Curve& c : selection.curves) {
            if (c.sketch == pathSketch_) {
                path_.push_back(c.entity);
            }
        }
    }
}

std::unique_ptr<document::Feature> SweepTool::build() const
{
    if (profiles_.empty() || path_.empty()) {
        return nullptr;
    }
    auto f = std::make_unique<document::SweepFeature>();
    if (original_) {
        f->setName(original_->name());
    }
    f->profiles = profiles_;
    f->pathSketch = pathSketch_;
    f->path = path_;
    f->spec.followPath = orientation_->currentIndex() == 0;
    f->spec.endScale = endScale_->value();
    f->operation = operationAt(operation_->currentIndex());
    return f;
}

QString SweepTool::inputsText() const
{
    if (profiles_.empty()) {
        return QStringLiteral("Elige el perfil (una región de un sketch) y las curvas de la trayectoria");
    }
    if (path_.empty()) {
        return QStringLiteral("%1 perfil(es). Ahora elige las curvas de la trayectoria (de otro sketch)").arg(profiles_.size());
    }
    return QStringLiteral("%1 perfil(es), trayectoria de %2 curva(s)").arg(profiles_.size()).arg(path_.size());
}

// --- Loft -------------------------------------------------------------------------------

LoftTool::LoftTool(document::Document& document, render::Viewport& viewport,
                   std::optional<document::FeatureId> editing)
    : FeatureTool(document, viewport, editing)
{
    if (const auto* l = dynamic_cast<const document::LoftFeature*>(original_.get())) {
        profiles_ = l->profiles;
    }
}

void LoftTool::createFields(QFormLayout* form)
{
    kind_ = combo({QStringLiteral("Suave"), QStringLiteral("Recto (entre perfiles)")});
    operation_ = combo({QStringLiteral("Unir"), QStringLiteral("Cortar"), QStringLiteral("Intersecar"),
                        QStringLiteral("Cuerpo nuevo")});
    if (const auto* l = dynamic_cast<const document::LoftFeature*>(original_.get())) {
        kind_->setCurrentIndex(l->ruled ? 1 : 0);
        operation_->setCurrentIndex(operationIndex(l->operation));
    } else {
        operation_->setCurrentIndex(operationIndex(before().bodies.empty() ? document::Operation::NewBody
                                                                           : document::Operation::Join));
    }
    form->addRow(QStringLiteral("Superficie"), kind_);
    form->addRow(QStringLiteral("Operación"), operation_);
    for (QComboBox* box : {kind_, operation_}) {
        connect(box, &QComboBox::currentIndexChanged, this, [this] { updatePreview(); });
    }
}

void LoftTool::selectionChanged(const render::ModelSelection& selection)
{
    // Se conserva el orden en que se eligieron: los que siguen elegidos mantienen su sitio
    // y los nuevos van al final.
    const std::vector<document::ProfileRef> current = profilesOf(selection);
    if (current.empty()) {
        return;
    }
    std::vector<document::ProfileRef> next;
    for (const document::ProfileRef& p : profiles_) {
        if (std::ranges::find(current, p) != current.end()) {
            next.push_back(p);
        }
    }
    for (const document::ProfileRef& p : current) {
        if (std::ranges::find(next, p) == next.end()) {
            next.push_back(p);
        }
    }
    profiles_ = std::move(next);
}

std::unique_ptr<document::Feature> LoftTool::build() const
{
    if (profiles_.size() < 2) {
        return nullptr;
    }
    auto f = std::make_unique<document::LoftFeature>();
    if (original_) {
        f->setName(original_->name());
    }
    f->profiles = profiles_;
    f->ruled = kind_->currentIndex() == 1;
    f->operation = operationAt(operation_->currentIndex());
    return f;
}

QString LoftTool::inputsText() const
{
    if (profiles_.size() < 2) {
        return QStringLiteral("Elige dos o más perfiles, en orden (%1 elegidos)").arg(profiles_.size());
    }
    return QStringLiteral("%1 perfiles, en el orden en que los elegiste").arg(profiles_.size());
}

// --- Mover cuerpo ---------------------------------------------------------------------------
// Como en Orca Slicer: tres flechas para mover y tres anillos (con marcas cada 45°) para girar, o los cuadros
// numéricos. Al arrastrar solo se mueve una sombra; el modelo se calcula al soltar.

namespace {
constexpr double kSnapDegrees = 45.0;   // las marcas de los anillos
constexpr double kSnapTolerance = 4.0;  // a cuántos grados de una marca se pega
constexpr double kHoldTolerance = 9.0;  // y a cuántos se despega (el «softblock»: se queda en la marca un buen trecho)
}

MoveTool::MoveTool(document::Document& document, render::Viewport& viewport,
                   std::optional<document::FeatureId> editing)
    : FeatureTool(document, viewport, editing)
{
    if (const auto* m = dynamic_cast<const document::MoveFeature*>(original_.get())) {
        body_ = m->body;
        alsoMove_ = m->alsoMove;
        legacyAxis_ = m->axisRef != 0;
    }
}

MoveTool::~MoveTool()
{
    viewport_.hideGizmo();
    viewport_.hideGhost();
    viewport_.hideAngleSweep();
    viewport_.hideAlignmentLine();
}

void MoveTool::stopPreview()
{
    FeatureTool::stopPreview();
    dragging_ = false;
    viewport_.hideGizmo();
    viewport_.hideGhost();
    viewport_.hideAngleSweep();
    viewport_.hideAlignmentLine();
}

void MoveTool::createFields(QFormLayout* form)
{
    // Casillas: qué cuerpos se mueven juntos (un llavero son una placa y un texto). Con Shift + clic se eligen varios
    // en la vista.
    bodyList_ = new QListWidget();
    for (const document::Body& b : before().bodies) {
        auto* item = new QListWidgetItem(QString::fromStdString(b.name), bodyList_);
        item->setData(Qt::UserRole, QString::fromStdString(b.key));
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Unchecked);
    }
    bodyList_->setMaximumHeight(110);
    if (body_.empty() && bodyList_->count() > 0) {
        body_ = bodyList_->item(0)->data(Qt::UserRole).toString().toStdString();
    }
    {
        std::vector<std::string> start{body_};
        start.insert(start.end(), alsoMove_.begin(), alsoMove_.end());
        choose(start);
    }
    x_ = lengthBox(0.0);
    field("x", x_);
    y_ = lengthBox(0.0);
    field("y", y_);
    z_ = lengthBox(0.0);
    field("z", z_);
    rx_ = angleBox(0.0, 360.0);
    field("rx", rx_);
    ry_ = angleBox(0.0, 360.0);
    field("ry", ry_);
    rz_ = angleBox(0.0, 360.0);
    field("rz", rz_);
    around_ = combo({QStringLiteral("Centro del cuerpo"), QStringLiteral("Origen")});
    copy_ = new QCheckBox(QStringLiteral("Crear una copia (deja el original)"));
    // El giro sobre un eje de construcción (lo único que los anillos no hacen) solo se ofrece si el diseño ya lo usaba.
    if (legacyAxis_) {
        axis_ = combo({QStringLiteral("Eje X"), QStringLiteral("Eje Y"), QStringLiteral("Eje Z")});
        for (const auto& [id, line] : before().axes) {
            const document::Feature* f = document_.findFeature(id);
            axis_->addItem(f ? QString::fromStdString(f->name()) : QStringLiteral("Eje"));
            axisIds_.push_back(id);
        }
        axis_->setCurrentIndex(2);
        angle_ = angleBox(0.0, 360.0);
        field("angle", angle_);
    }
    if (const auto* m = dynamic_cast<const document::MoveFeature*>(original_.get())) {
        x_->setValue(m->translation.x);
        y_->setValue(m->translation.y);
        z_->setValue(m->translation.z);
        rx_->setValue(m->rotation.x / kDegree);
        ry_->setValue(m->rotation.y / kDegree);
        rz_->setValue(m->rotation.z / kDegree);
        if (legacyAxis_) {
            const auto it = std::ranges::find(axisIds_, m->axisRef);
            axis_->setCurrentIndex(it != axisIds_.end() ? 3 + int(it - axisIds_.begin()) : 2);
            angle_->setValue(m->angle / kDegree);
        } else if (m->angle != 0.0) {
            // Un giro de antes sobre X, Y o Z es el mismo que el del anillo de ese eje.
            QDoubleSpinBox* ring = m->axis == 0 ? rx_ : m->axis == 1 ? ry_ : rz_;
            ring->setValue(ring->value() + m->angle / kDegree);
        }
        around_->setCurrentIndex(m->aroundOrigin ? 1 : 0);
        copy_->setChecked(m->copy);
    }
    moveMode_ = new QPushButton(QStringLiteral("Mover"));
    turnMode_ = new QPushButton(QStringLiteral("Girar"));
    for (QPushButton* button : {moveMode_, turnMode_}) {
        button->setCheckable(true);
    }
    moveMode_->setChecked(true);
    auto* modes = new QButtonGroup(this);
    modes->setExclusive(true);
    modes->addButton(moveMode_);
    modes->addButton(turnMode_);
    auto* modeRow = new QHBoxLayout();
    modeRow->addWidget(moveMode_);
    modeRow->addWidget(turnMode_);
    auto* hint = new QLabel(QStringLiteral("Arrastra las flechas en la vista, o los anillos: el ángulo se mide desde la perilla "
                                           "(0°) y se pega a las marcas cada 45°. La pieza se recalcula al soltar. También "
                                           "puedes escribir los valores."));
    hint->setWordWrap(true);
    {
        // Sin hoja de estilos (cada una obliga a Qt a calcular el estilo de todo el panel en cada repintado).
        QPalette palette = hint->palette();
        palette.setColor(QPalette::WindowText, QColor(toneColor(Tone::Muted)));
        hint->setPalette(palette);
    }
    form->addRow(QStringLiteral("Cuerpos"), bodyList_);
    form->addRow(modeRow);
    form->addRow(hint);
    form->addRow(QStringLiteral("Mover en X"), x_);
    form->addRow(QStringLiteral("Mover en Y"), y_);
    form->addRow(QStringLiteral("Mover en Z"), z_);
    form->addRow(QStringLiteral("Girar en X"), rx_);
    form->addRow(QStringLiteral("Girar en Y"), ry_);
    form->addRow(QStringLiteral("Girar en Z"), rz_);
    form->addRow(QStringLiteral("Girar por"), around_);
    if (legacyAxis_) {
        form->addRow(QStringLiteral("Girar alrededor de"), axis_);
        form->addRow(QStringLiteral("Ángulo"), angle_);
    }
    form->addRow(QString(), copy_);
    connect(bodyList_, &QListWidget::itemChanged, this, [this](QListWidgetItem*) {
        std::vector<std::string> keys;
        for (int i = 0; i < bodyList_->count(); ++i) {
            if (bodyList_->item(i)->checkState() == Qt::Checked) {
                keys.push_back(bodyList_->item(i)->data(Qt::UserRole).toString().toStdString());
            }
        }
        if (keys.empty()) {
            return;  // siempre hay uno (si se desmarca el último, se vuelve a marcar al elegir otro)
        }
        body_ = keys.front();
        alsoMove_.assign(keys.begin() + 1, keys.end());
        copy_->setEnabled(keys.size() == 1);
        updatePreview();
    });
    for (QComboBox* box : {axis_, around_}) {
        if (box != nullptr) {
            connect(box, &QComboBox::currentIndexChanged, this, [this] { updatePreview(); });
        }
    }
    for (QDoubleSpinBox* box : {x_, y_, z_, rx_, ry_, rz_, angle_}) {
        if (box != nullptr) {
            connect(box, &QDoubleSpinBox::valueChanged, this, [this] { updatePreview(); });
        }
    }
    connect(copy_, &QCheckBox::toggled, this, [this] { updatePreview(); });
    connect(moveMode_, &QPushButton::toggled, this, [this] { refreshGizmo(); });
    connect(&viewport_, &render::Viewport::gizmoPressed, this, &MoveTool::gizmoPressed);
    connect(&viewport_, &render::Viewport::gizmoDragged, this, &MoveTool::gizmoDragged);
    connect(&viewport_, &render::Viewport::gizmoReleased, this, [this](int) { gizmoReleased(); });
}

std::array<double, 3> MoveTool::positionValues() const
{
    return {x_->value(), y_->value(), z_->value()};
}

std::array<double, 3> MoveTool::turnValues() const
{
    return {rx_->value(), ry_->value(), rz_->value()};
}

std::vector<std::string> MoveTool::chosen() const
{
    std::vector<std::string> keys{body_};
    keys.insert(keys.end(), alsoMove_.begin(), alsoMove_.end());
    return keys;
}

void MoveTool::choose(const std::vector<std::string>& keys)
{
    if (keys.empty()) {
        return;
    }
    body_ = keys.front();
    alsoMove_.assign(keys.begin() + 1, keys.end());
    const QSignalBlocker block(bodyList_);
    for (int i = 0; i < bodyList_->count(); ++i) {
        const std::string key = bodyList_->item(i)->data(Qt::UserRole).toString().toStdString();
        bodyList_->item(i)->setCheckState(std::ranges::find(keys, key) != keys.end() ? Qt::Checked : Qt::Unchecked);
    }
    if (copy_ != nullptr) {
        copy_->setEnabled(keys.size() == 1);
        if (keys.size() > 1) {
            copy_->setChecked(false);
        }
    }
}

std::optional<sketch::Vec3> MoveTool::bodiesCenter() const
{
    std::optional<geometry::Box> all;
    for (const std::string& key : chosen()) {
        for (const document::Body& b : before().bodies) {
            if (b.key != key || b.shape.isNull()) {
                continue;
            }
            const geometry::Box box = b.shape.bounds();
            if (!all) {
                all = box;
            } else {
                all->min = {std::min(all->min.x, box.min.x), std::min(all->min.y, box.min.y), std::min(all->min.z, box.min.z)};
                all->max = {std::max(all->max.x, box.max.x), std::max(all->max.y, box.max.y), std::max(all->max.z, box.max.z)};
            }
        }
    }
    if (!all) {
        return std::nullopt;
    }
    return sketch::Vec3{(all->min.x + all->max.x) / 2.0, (all->min.y + all->max.y) / 2.0, (all->min.z + all->max.z) / 2.0};
}

std::optional<sketch::Vec3> MoveTool::pivot() const
{
    if (around_->currentIndex() == 1) {
        return sketch::Vec3{0.0, 0.0, 0.0};
    }
    return bodiesCenter();
}

void MoveTool::afterPreview(const document::ModelState& /*before*/)
{
    refreshGizmo();
}

void MoveTool::refreshGizmo()
{
    if (dragging_ || moveMode_ == nullptr) {
        return;
    }
    const auto center = pivot();
    if (!center || body_.empty()) {
        viewport_.hideGizmo();
        return;
    }
    // Las flechas y los anillos van donde queda el cuerpo (el pivote, desplazado).
    const std::array<double, 3> at = positionValues();
    viewport_.showGizmo(moveMode_->isChecked() ? render::Viewport::GizmoKind::Move : render::Viewport::GizmoKind::Rotate,
                        {center->x + at[0], center->y + at[1], center->z + at[2]});
}

void MoveTool::moveGhost(const std::array<double, 3>& position, const std::array<double, 3>& turns)
{
    const auto center = pivot();
    if (!center) {
        return;
    }
    viewport_.setGhostMotion(*center, {turns[0] * kDegree, turns[1] * kDegree, turns[2] * kDegree},
                             {position[0], position[1], position[2]});
}

void MoveTool::gizmoPressed(int axis)
{
    dragging_ = true;
    viewport_.isolateGizmo(axis);  // los otros dos se quitan mientras se arrastra este
    heldMark_.reset();
    dragPosition_ = positionValues();
    dragTurns_ = turnValues();
    // La sombra parte del cuerpo de antes de la operación, con la pose de los cuadros.
    std::vector<geometry::Shape> shapes;
    const std::vector<std::string> keys = chosen();
    for (const document::Body& b : before().bodies) {
        if (std::ranges::find(keys, b.key) != keys.end()) {
            shapes.push_back(b.shape);
        }
    }
    viewport_.showGhost(shapes);
    moveGhost(dragPosition_, dragTurns_);
}

void MoveTool::gizmoDragged(int axis, double amount)
{
    if (!dragging_ || axis < 0 || axis > 2) {
        return;
    }
    std::array<double, 3> position = dragPosition_;
    std::array<double, 3> turns = dragTurns_;
    QString text;
    bool snapped = false;  // pegado a una marca de un anillo
    double turnsAngle = 0.0;  // el ángulo de ese giro (grados)
    if (moveMode_->isChecked()) {
        // Con Ctrl el movimiento se pega a una rejilla de una unidad entera (1 mm, 1 cm, 1 in…): U8.
        const bool grid = QApplication::keyboardModifiers().testFlag(Qt::ControlModifier);
        const double raw = dragPosition_[std::size_t(axis)] + amount;
        const double step = Units::instance().factor();
        position[std::size_t(axis)] = grid ? std::round(raw / step) * step : std::round(raw * 100.0) / 100.0;
        text = QStringLiteral("Mover en %1: %2%3").arg(QLatin1Char("XYZ"[axis]))
                   .arg(Units::instance().formatWithUnit(position[std::size_t(axis)]),
                        grid ? QStringLiteral("  (rejilla; suelta Ctrl para mover libre)") : QStringLiteral("  (Ctrl: pegar a la rejilla)"));
    } else {
        // Lo que se gira en este arrastre, sobre el eje del mundo que dibuja el anillo: el ángulo del cursor sobre el
        // anillo desde su marca 0 (como Orca Slicer), que se pega a las marcas (0, 45, 90, 180, 270…) si se pasa
        // cerca: con el cursor sobre una marca, el giro es exactamente ese ángulo.
        double angle = std::remainder(amount / kDegree, 360.0);
        const double mark = std::round(angle / kSnapDegrees) * kSnapDegrees;
        if (heldMark_) {
            // Softblock: ya pegado a una marca, el giro se queda en ella mientras el cursor siga cerca (más que lo
            // que hizo falta para pegarse); solo se suelta cuando se aleja claramente.
            if (std::abs(std::remainder(angle - *heldMark_, 360.0)) < kHoldTolerance) {
                angle = *heldMark_;
                snapped = true;
            } else {
                heldMark_.reset();
            }
        }
        if (!snapped && std::abs(angle - mark) < kSnapTolerance) {
            heldMark_ = mark;
            angle = mark;
            snapped = true;
        }
        turnsAngle = angle;
        // Se compone con la orientación que ya tenía (primero la de antes, después este giro sobre el eje del mundo)
        // y se vuelve a los tres giros X, Y, Z de la operación.
        const Mat3 start = fromTurns(dragTurns_[0] * kDegree, dragTurns_[1] * kDegree, dragTurns_[2] * kDegree);
        const auto now = toTurns(multiply(rotationAbout(axis, angle * kDegree), start));
        for (std::size_t i = 0; i < 3; ++i) {
            turns[i] = std::round(now[i] / kDegree * 10.0) / 10.0;
        }
        text = QStringLiteral("Girar en %1: %2°%3").arg(QLatin1Char("XYZ"[axis])).arg(angle, 0, 'f', 1)
                   .arg(snapped ? QStringLiteral("  (marca)") : QString());
    }
    // Los cuadros siguen al arrastre, sin pedir una vista previa a cada paso (y solo se tocan los que cambian: cada
    // uno reformatea su texto y avisa a quien lo escucha).
    updating_ = true;
    const std::array<QDoubleSpinBox*, 6> boxes{x_, y_, z_, rx_, ry_, rz_};
    const std::array<double, 6> values{position[0], position[1], position[2], turns[0], turns[1], turns[2]};
    for (std::size_t i = 0; i < boxes.size(); ++i) {
        if (std::abs(boxes[i]->value() - values[i]) > 1e-9) {
            boxes[i]->setValue(values[i]);
        }
    }
    updating_ = false;
    moveGhost(position, turns);
    // El texto va a la barra de estado: cambiar el del panel (con varias líneas) obliga a recalcular todo su layout a
    // cada paso del ratón (la mitad del coste de un paso de arrastre).
    emit message(text);
    if (!moveMode_->isChecked()) {
        // El arco desde la marca 0 hasta donde se gira: se ve cuánto se lleva y dónde cae entre las marcas.
        if (const auto center = pivot()) {
            const sketch::Vec3 axes[3] = {{1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}};
            const sketch::Vec3 refs[3] = {{0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}, {1.0, 0.0, 0.0}};
            const sketch::Vec3 at{center->x + position[0], center->y + position[1], center->z + position[2]};
            const double radius = 0.85 * 90.0 * viewport_.sketchPixelSize();  // el del anillo
            viewport_.showAngleSweep(at, axes[axis], moved(at, refs[axis], radius), turnsAngle * kDegree);
            // Pegado a una marca: una recta por el centro del anillo, en su plano y en la dirección de esa marca, que
            // confirma «estás alineado con 90°, 180°…».
            if (snapped) {
                const sketch::Vec3 direction = rotatedAbout(refs[axis], {0.0, 0.0, 0.0}, axes[axis], turnsAngle * kDegree);
                viewport_.showAlignmentLine(moved(at, direction, -1.3 * radius), moved(at, direction, 1.3 * radius));
            } else {
                viewport_.hideAlignmentLine();
            }
        }
    } else if (const auto center = pivot()) {
        // Al girar, el centro no se mueve: solo las flechas de mover siguen a la pieza.
        viewport_.showGizmo(render::Viewport::GizmoKind::Move,
                            {center->x + position[0], center->y + position[1], center->z + position[2]});
    }
}

void MoveTool::gizmoReleased()
{
    if (!dragging_) {
        return;
    }
    dragging_ = false;
    heldMark_.reset();
    viewport_.isolateGizmo(-1);  // vuelven los demás
    viewport_.hideGhost();
    viewport_.hideAngleSweep();
    viewport_.hideAlignmentLine();
    viewport_.hideDimensionArrow();
    statusLabel_->clear();
    refreshArrow();   // si algún cuadro tiene el foco, vuelve su flecha de medida
    updatePreview();  // aquí sí se calcula el cuerpo (en segundo plano)
    refreshGizmo();
}

void MoveTool::selectionChanged(const render::ModelSelection& selection)
{
    // Un clic en una cara elige su cuerpo; con Shift + clic en caras de varios cuerpos, todos esos se mueven juntos.
    std::vector<std::string> keys;
    for (const render::ModelSelection::Face& face : selection.faces) {
        if (std::ranges::find(keys, face.body) == keys.end() &&
            std::ranges::any_of(before().bodies, [&](const document::Body& b) { return b.key == face.body; })) {
            keys.push_back(face.body);
        }
    }
    if (!keys.empty() && keys != chosen()) {
        choose(keys);
        if (dragging_) {
            return;
        }
        updatePreview();
    }
}

std::unique_ptr<document::Feature> MoveTool::build() const
{
    if (body_.empty()) {
        return nullptr;
    }
    auto f = std::make_unique<document::MoveFeature>();
    if (original_) {
        f->setName(original_->name());
    }
    f->body = body_;
    f->alsoMove = alsoMove_;
    f->translation = {x_->value(), y_->value(), z_->value()};
    f->rotation = {rx_->value() * kDegree, ry_->value() * kDegree, rz_->value() * kDegree};
    if (legacyAxis_) {
        if (axis_->currentIndex() >= 3) {
            f->axisRef = axisIds_[std::size_t(axis_->currentIndex() - 3)];
        } else {
            f->axis = axis_->currentIndex();
        }
        f->angle = angle_->value() * kDegree;
    }
    f->aroundOrigin = around_->currentIndex() == 1;
    f->copy = copy_->isChecked();
    return f;
}

std::optional<FeatureTool::Arrow> MoveTool::dimensionArrow(const std::string& key) const
{
    const auto center = bodiesCenter();
    if (!center) {
        return std::nullopt;
    }
    if (key == "x") {
        return arrowAlong(*center, {1.0, 0.0, 0.0}, x_->value());
    }
    if (key == "y") {
        return arrowAlong(*center, {0.0, 1.0, 0.0}, y_->value());
    }
    if (key == "z") {
        return arrowAlong(*center, {0.0, 0.0, 1.0}, z_->value());
    }
    return std::nullopt;
}

std::optional<FeatureTool::Arc> MoveTool::dimensionArc(const std::string& key) const
{
    // Los giros de los anillos: el arco sale de la marca 0 del anillo (la misma que dibuja el visor).
    const int ring = key == "rx" ? 0 : key == "ry" ? 1 : key == "rz" ? 2 : -1;
    if (ring >= 0) {
        const auto center = pivot();
        if (!center) {
            return std::nullopt;
        }
        const sketch::Vec3 axes[3] = {{1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}};
        const sketch::Vec3 refs[3] = {{0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}, {1.0, 0.0, 0.0}};
        const double value = (ring == 0 ? rx_ : ring == 1 ? ry_ : rz_)->value();
        return Arc{*center, axes[ring], moved(*center, refs[ring], arcRadius()), value * kDegree};
    }
    if (key != "angle" || !legacyAxis_) {
        return std::nullopt;
    }
    sketch::Vec3 point{};
    sketch::Vec3 axis{0.0, 0.0, 1.0};
    const int at = axis_->currentIndex();
    if (at == 0) {
        axis = {1.0, 0.0, 0.0};
    } else if (at == 1) {
        axis = {0.0, 1.0, 0.0};
    } else if (at >= 3 && std::size_t(at - 3) < axisIds_.size()) {
        const auto it = before().axes.find(axisIds_[std::size_t(at - 3)]);
        if (it == before().axes.end()) {
            return std::nullopt;
        }
        point = it->second.point;
        axis = it->second.direction;
    }
    return Arc{point, axis, perpendicularPoint(point, axis, arcRadius()), angle_->value() * kDegree};
}

QString MoveTool::inputsText() const
{
    if (bodyList_->count() == 0) {
        return QStringLiteral("No hay cuerpos que mover");
    }
    if (alsoMove_.empty()) {
        return QStringLiteral("Marca los cuerpos que se mueven, o haz clic en una cara (Shift + clic para varios cuerpos)");
    }
    return QStringLiteral("Se mueven %1 cuerpos juntos, como un bloque").arg(1 + alsoMove_.size());
}

// --- Combinar cuerpos -------------------------------------------------------------------------

CombineTool::CombineTool(document::Document& document, render::Viewport& viewport,
                         std::optional<document::FeatureId> editing)
    : FeatureTool(document, viewport, editing)
{
    if (const auto* c = dynamic_cast<const document::CombineFeature*>(original_.get())) {
        checked_ = c->tools;
    }
}

void CombineTool::createFields(QFormLayout* form)
{
    target_ = new QComboBox();
    for (const document::Body& b : before().bodies) {
        target_->addItem(QString::fromStdString(b.name), QString::fromStdString(b.key));
    }
    tools_ = new QListWidget();
    tools_->setMaximumHeight(120);
    operation_ = combo({QStringLiteral("Unir"), QStringLiteral("Cortar"), QStringLiteral("Intersecar")});
    keep_ = new QCheckBox(QStringLiteral("Conservar los cuerpos herramienta"));
    if (const auto* c = dynamic_cast<const document::CombineFeature*>(original_.get())) {
        if (const int at = target_->findData(QString::fromStdString(c->target)); at >= 0) {
            target_->setCurrentIndex(at);
        }
        operation_->setCurrentIndex(c->operation == document::Operation::Cut ? 1 : c->operation == document::Operation::Intersect ? 2 : 0);
        keep_->setChecked(c->keepTools);
    }
    fillTools();
    form->addRow(QStringLiteral("Cuerpo destino"), target_);
    form->addRow(QStringLiteral("Cuerpos herramienta"), tools_);
    form->addRow(QStringLiteral("Operación"), operation_);
    form->addRow(QString(), keep_);
    connect(target_, &QComboBox::currentIndexChanged, this, [this] {
        fillTools();
        updatePreview();
    });
    connect(operation_, &QComboBox::currentIndexChanged, this, [this] { updatePreview(); });
    connect(keep_, &QCheckBox::toggled, this, [this] { updatePreview(); });
    connect(tools_, &QListWidget::itemChanged, this, [this] { updatePreview(); });
}

// La lista de herramientas son todos los cuerpos menos el destino.
void CombineTool::fillTools()
{
    std::vector<std::string> marked = checked_;
    for (int i = 0; i < tools_->count(); ++i) {
        if (tools_->item(i)->checkState() == Qt::Checked) {
            marked.push_back(tools_->item(i)->data(Qt::UserRole).toString().toStdString());
        }
    }
    const QSignalBlocker block(tools_);
    tools_->clear();
    const QString targetKey = target_->currentData().toString();
    for (const document::Body& b : before().bodies) {
        if (QString::fromStdString(b.key) == targetKey) {
            continue;
        }
        auto* item = new QListWidgetItem(QString::fromStdString(b.name), tools_);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(std::ranges::find(marked, b.key) != marked.end() ? Qt::Checked : Qt::Unchecked);
        item->setData(Qt::UserRole, QString::fromStdString(b.key));
    }
    checked_.clear();
}

std::unique_ptr<document::Feature> CombineTool::build() const
{
    if (target_->count() < 2) {
        return nullptr;
    }
    auto f = std::make_unique<document::CombineFeature>();
    if (original_) {
        f->setName(original_->name());
    }
    f->target = target_->currentData().toString().toStdString();
    for (int i = 0; i < tools_->count(); ++i) {
        if (tools_->item(i)->checkState() == Qt::Checked) {
            f->tools.push_back(tools_->item(i)->data(Qt::UserRole).toString().toStdString());
        }
    }
    if (f->tools.empty()) {
        return nullptr;
    }
    const int op = operation_->currentIndex();
    f->operation = op == 1 ? document::Operation::Cut : op == 2 ? document::Operation::Intersect : document::Operation::Join;
    f->keepTools = keep_->isChecked();
    return f;
}

QString CombineTool::inputsText() const
{
    if (target_->count() < 2) {
        return QStringLiteral("Hacen falta al menos dos cuerpos para combinar");
    }
    return QStringLiteral("Marca los cuerpos que se unen, cortan o intersecan con el destino");
}

bool CombineTool::removesMaterial() const
{
    return operation_ != nullptr && operation_->currentIndex() != 0;
}

// --- Alinear y unir ---------------------------------------------------------------------------
// Una sola herramienta para las dos cosas, como el cuadro de Uniones de Fusion 360: el primer tipo, «Alinear», coloca
// una pieza sobre otra de una vez; los demás dejan una unión que se puede mover (con límites, animar y enlazar).

namespace {

constexpr int kAlignType = 0;  // los demás son JointType(índice - 1)

}

JointTool::JointTool(document::Document& document, render::Viewport& viewport,
                     std::optional<document::FeatureId> editing, bool align)
    : FeatureTool(document, viewport, editing)
    , startAsAlign_(align)
{
    if (const auto* j = dynamic_cast<const document::JointFeature*>(original_.get())) {
        // En una unión, lo primero que se elige es la fija.
        first_ = {j->fixedFace, j->fixedEdge};
        second_ = {j->movingFace, j->movingEdge};
        startAsAlign_ = false;
    } else if (const auto* a = dynamic_cast<const document::AlignFeature*>(original_.get())) {
        // En un alineado, lo primero que se elige es lo que se mueve.
        first_ = a->moving;
        second_ = a->target;
        startAsAlign_ = true;
    }
}

bool JointTool::alignMode() const
{
    return type_ != nullptr && type_->currentIndex() == kAlignType;
}

document::ElementPick JointTool::movingPick() const
{
    return alignMode() ? first_ : second_;
}

document::ElementPick JointTool::targetPick() const
{
    return alignMode() ? second_ : first_;
}

void JointTool::createFields(QFormLayout* form)
{
    form_ = form;
    type_ = combo({QStringLiteral("Alinear (una sola vez)"), QStringLiteral("Rígida"), QStringLiteral("Giratoria"),
                   QStringLiteral("Deslizante"), QStringLiteral("Cilíndrica (gira y desliza)"),
                   QStringLiteral("Pasador y ranura"), QStringLiteral("Plana"), QStringLiteral("Esférica")});
    offset_ = lengthBox(0.0);
    field("offset", offset_);
    angle_ = angleBox(0.0, 360.0);
    field("angle", angle_);
    slide_ = lengthBox(0.0);
    field("slide", slide_);
    slideY_ = lengthBox(0.0);
    field("slideY", slideY_);
    tiltX_ = angleBox(0.0, 180.0);
    field("tiltX", tiltX_);
    tiltY_ = angleBox(0.0, 180.0);
    field("tiltY", tiltY_);
    flip_ = new QCheckBox(QStringLiteral("Del otro lado"));
    flip_->setToolTip(QStringLiteral("Caras enfrentadas ↔ del mismo lado; ejes en un sentido ↔ en el contrario"));
    auto* swap = new QPushButton(QStringLiteral("Intercambiar las dos piezas"));
    turn_ = new QPushButton(QStringLiteral("Girar 90°"));
    turn_->setToolTip(QStringLiteral("Suma 90° al ángulo (para alinear de cuatro en cuatro posiciones)"));
    limitAngle_ = new QCheckBox(QStringLiteral("Límite de giro"));
    angleMin_ = angleBox(0.0, 360.0);
    angleMax_ = angleBox(90.0, 360.0);
    limitSlide_ = new QCheckBox(QStringLiteral("Límite de deslizamiento"));
    slideMin_ = lengthBox(0.0);
    slideMax_ = lengthBox(10.0);
    animate_ = new QPushButton(QStringLiteral("▶ Animar"));
    animate_->setCheckable(true);
    animate_->setToolTip(QStringLiteral("La unión se mueve sola de un límite al otro para ver cómo funciona"));

    if (const auto* j = dynamic_cast<const document::JointFeature*>(original_.get())) {
        type_->setCurrentIndex(int(j->type) + 1);
        offset_->setValue(j->offset);
        angle_->setValue(j->angle / kDegree);
        slide_->setValue(j->slide);
        slideY_->setValue(j->slideY);
        tiltX_->setValue(j->tiltX / kDegree);
        tiltY_->setValue(j->tiltY / kDegree);
        flip_->setChecked(j->flip);
        limitAngle_->setChecked(j->limitAngle);
        angleMin_->setValue(j->angleMin / kDegree);
        angleMax_->setValue(j->angleMax / kDegree);
        limitSlide_->setChecked(j->limitSlide);
        slideMin_->setValue(j->slideMin);
        slideMax_->setValue(j->slideMax);
    } else if (const auto* a = dynamic_cast<const document::AlignFeature*>(original_.get())) {
        type_->setCurrentIndex(kAlignType);
        offset_->setValue(a->offset);
        angle_->setValue(a->angle / kDegree);
        flip_->setChecked(a->flip);
    } else {
        type_->setCurrentIndex(startAsAlign_ ? kAlignType : 1 + int(document::JointType::Revolute));
    }

    form->addRow(QStringLiteral("Tipo"), type_);
    form->addRow(QStringLiteral("Distancia"), offset_);
    form->addRow(QStringLiteral("Ángulo"), angle_);
    form->addRow(QString(), turn_);
    form->addRow(QStringLiteral("Desplazamiento"), slide_);
    form->addRow(QStringLiteral("Desplazamiento Y"), slideY_);
    form->addRow(QStringLiteral("Inclinación X"), tiltX_);
    form->addRow(QStringLiteral("Inclinación Y"), tiltY_);
    form->addRow(QString(), flip_);
    form->addRow(QString(), limitAngle_);
    form->addRow(QStringLiteral("Giro mínimo"), angleMin_);
    form->addRow(QStringLiteral("Giro máximo"), angleMax_);
    form->addRow(QString(), limitSlide_);
    form->addRow(QStringLiteral("Desplazamiento mínimo"), slideMin_);
    form->addRow(QStringLiteral("Desplazamiento máximo"), slideMax_);
    form->addRow(QString(), animate_);
    form->addRow(QString(), swap);

    connect(type_, &QComboBox::currentIndexChanged, this, [this] {
        stopAnimation();
        updateVisibility();
        updatePreview();
    });
    for (QDoubleSpinBox* box : {offset_, angle_, slide_, slideY_, tiltX_, tiltY_, angleMin_, angleMax_, slideMin_, slideMax_}) {
        connect(box, &QDoubleSpinBox::valueChanged, this, [this] { updatePreview(); });
    }
    for (QCheckBox* box : {flip_, limitAngle_, limitSlide_}) {
        connect(box, &QCheckBox::toggled, this, [this] {
            updateVisibility();
            updatePreview();
        });
    }
    connect(swap, &QPushButton::clicked, this, [this] {
        std::swap(first_, second_);
        updatePreview();
    });
    connect(turn_, &QPushButton::clicked, this, [this] {
        double next = angle_->value() + 90.0;
        if (next > 360.0) {
            next -= 360.0;
        }
        angle_->setValue(next);
    });
    connect(animate_, &QPushButton::toggled, this, [this](bool on) {
        if (on) {
            startAnimation();
        } else {
            stopAnimation();
        }
    });
    updateVisibility();
}

void JointTool::updateVisibility()
{
    const int index = type_->currentIndex();
    const bool align = index == kAlignType;
    const auto type = align ? document::JointType::Rigid : document::JointType(index - 1);
    using T = document::JointType;
    const bool turns = align || type == T::Revolute || type == T::Cylindrical || type == T::PinSlot || type == T::Planar ||
                       type == T::Ball;
    const bool slidesX = type == T::Slider || type == T::PinSlot || type == T::Planar;
    const bool slidesY = type == T::Planar;
    const bool tilts = type == T::Ball;
    form_->setRowVisible(angle_, turns);
    form_->setRowVisible(turn_, align);
    form_->setRowVisible(slide_, slidesX);
    form_->setRowVisible(slideY_, slidesY);
    form_->setRowVisible(tiltX_, tilts);
    form_->setRowVisible(tiltY_, tilts);
    const bool angleLimits = !align && (type == T::Revolute || type == T::Cylindrical || type == T::PinSlot);
    const bool slideLimits = !align && (type == T::Slider || type == T::Cylindrical || type == T::PinSlot);
    form_->setRowVisible(limitAngle_, angleLimits);
    form_->setRowVisible(angleMin_, angleLimits && limitAngle_->isChecked());
    form_->setRowVisible(angleMax_, angleLimits && limitAngle_->isChecked());
    form_->setRowVisible(limitSlide_, slideLimits);
    form_->setRowVisible(slideMin_, slideLimits && limitSlide_->isChecked());
    form_->setRowVisible(slideMax_, slideLimits && limitSlide_->isChecked());
    form_->setRowVisible(animate_, !align && type != T::Rigid);
    // En la cilíndrica, la distancia es el deslizamiento por el eje.
    if (auto* label = qobject_cast<QLabel*>(form_->labelForField(offset_))) {
        label->setText(type == T::Cylindrical ? QStringLiteral("Deslizamiento por el eje") : QStringLiteral("Distancia"));
    }
}

void JointTool::selectionChanged(const render::ModelSelection& selection)
{
    // Cada clic en una cara o una arista la elige (la primera y luego la segunda) o, si ya estaba elegida, la quita;
    // la lista es de la herramienta y el visor se deja sin selección (la vista previa lo redibuja todo).
    const auto take = [this](const document::ElementPick& pick) {
        if (pick == first_) {
            first_ = second_;
            second_ = {};
        } else if (pick == second_) {
            second_ = {};
        } else if (first_.ref.empty()) {
            first_ = pick;
        } else {
            second_ = pick;
        }
    };
    for (const render::ModelSelection::Face& f : selection.faces) {
        if (before().findFace(f.ref)) {  // una cara del resultado de la vista previa que no existe antes: no cuenta
            take({f.ref, false});
        }
    }
    for (const render::ModelSelection::Edge& e : selection.edges) {
        if (before().findEdge(e.ref)) {
            take({e.ref, true});
        }
    }
    if (!selection.empty()) {
        viewport_.clearModelSelection();
    }
}

QString JointTool::pickText(const document::ElementPick& pick) const
{
    if (pick.ref.empty()) {
        return QStringLiteral("—");
    }
    const auto body = [&]() -> std::optional<std::size_t> {
        if (pick.edge) {
            const auto hit = before().findEdge(pick.ref);
            return hit ? std::optional(hit->body) : std::nullopt;
        }
        const auto hit = before().findFace(pick.ref);
        return hit ? std::optional(hit->body) : std::nullopt;
    }();
    if (!body) {
        return QStringLiteral("(ya no existe)");
    }
    return QStringLiteral("%1 (%2)").arg(QString::fromStdString(before().bodies[*body].name),
                                         pick.edge ? QStringLiteral("arista") : QStringLiteral("cara"));
}

std::optional<geometry::Frame> JointTool::frameOf(const document::ElementPick& pick) const
{
    if (pick.ref.empty()) {
        return std::nullopt;
    }
    if (pick.edge) {
        const auto hit = before().findEdge(pick.ref);
        return hit ? before().bodies[hit->body].shape.edgeFrame(hit->edge) : std::nullopt;
    }
    const auto hit = before().findFace(pick.ref);
    return hit ? before().bodies[hit->body].shape.faceFrame(hit->face) : std::nullopt;
}

std::optional<std::string> JointTool::bodyOf(const document::ElementPick& pick) const
{
    if (pick.ref.empty()) {
        return std::nullopt;
    }
    if (pick.edge) {
        const auto hit = before().findEdge(pick.ref);
        return hit ? std::optional(before().bodies[hit->body].key) : std::nullopt;
    }
    const auto hit = before().findFace(pick.ref);
    return hit ? std::optional(before().bodies[hit->body].key) : std::nullopt;
}

std::vector<std::string> JointTool::companions() const
{
    // Si la pieza que se mueve forma parte de un componente, los demás cuerpos del componente van con ella (como una
    // sola pieza): el tren de engranajes entero se mueve al alinear uno de ellos.
    std::vector<std::string> keys;
    const auto moving = bodyOf(movingPick());
    if (!moving) {
        return keys;
    }
    const std::string component = document_.componentOf(*moving);
    if (component.empty()) {
        return keys;
    }
    for (const document::Body& body : before().bodies) {
        if (body.key != *moving && document_.componentOf(body.key) == component) {
            keys.push_back(body.key);
        }
    }
    return keys;
}

std::unique_ptr<document::Feature> JointTool::build() const
{
    if (first_.ref.empty() || second_.ref.empty()) {
        return nullptr;
    }
    const int index = type_->currentIndex();
    if (index == kAlignType) {
        auto f = std::make_unique<document::AlignFeature>();
        if (original_) {
            f->setName(original_->name());
        }
        f->moving = first_;
        f->target = second_;
        f->alsoMove = companions();
        f->flip = flip_->isChecked();
        f->angle = angle_->value() * kDegree;
        f->offset = offset_->value();
        return f;
    }
    auto f = std::make_unique<document::JointFeature>();
    if (original_) {
        f->setName(original_->name());
    }
    f->fixedFace = first_.ref;
    f->fixedEdge = first_.edge;
    f->movingFace = second_.ref;
    f->movingEdge = second_.edge;
    f->alsoMove = companions();
    f->type = document::JointType(index - 1);
    f->offset = offset_->value();
    f->angle = angle_->value() * kDegree;
    f->slide = slide_->value();
    f->slideY = slideY_->value();
    f->tiltX = tiltX_->value() * kDegree;
    f->tiltY = tiltY_->value() * kDegree;
    f->flip = flip_->isChecked();
    f->limitAngle = limitAngle_->isChecked();
    f->angleMin = angleMin_->value() * kDegree;
    f->angleMax = angleMax_->value() * kDegree;
    f->limitSlide = limitSlide_->isChecked();
    f->slideMin = slideMin_->value();
    f->slideMax = slideMax_->value();
    return f;
}

std::optional<FeatureTool::Arrow> JointTool::dimensionArrow(const std::string& key) const
{
    const auto frame = frameOf(targetPick());
    if (!frame) {
        return std::nullopt;
    }
    const sketch::Vec3 y{frame->direction.y * frame->xAxis.z - frame->direction.z * frame->xAxis.y,
                         frame->direction.z * frame->xAxis.x - frame->direction.x * frame->xAxis.z,
                         frame->direction.x * frame->xAxis.y - frame->direction.y * frame->xAxis.x};
    if (key == "offset") {
        return arrowAlong(frame->origin, frame->direction, offset_->value());
    }
    if (key == "slide") {
        return arrowAlong(frame->origin, frame->xAxis, slide_->value());
    }
    if (key == "slideY") {
        return arrowAlong(frame->origin, y, slideY_->value());
    }
    return std::nullopt;
}

std::optional<FeatureTool::Arc> JointTool::dimensionArc(const std::string& key) const
{
    const auto frame = frameOf(targetPick());
    if (key != "angle" || !frame) {
        return std::nullopt;
    }
    // Girar alrededor de la dirección del elemento de destino.
    return Arc{frame->origin, frame->direction, perpendicularPoint(frame->origin, frame->direction, arcRadius()),
               angle_->value() * kDegree};
}

QString JointTool::inputsText() const
{
    const bool align = alignMode();
    if (first_.ref.empty()) {
        return align ? QStringLiteral("Elige en la vista la cara o la arista de la pieza que se va a mover (un centro, un eje o una cara)")
                     : QStringLiteral("Elige en la vista la cara o la arista de la pieza que se queda quieta y luego la de la que se mueve");
    }
    if (second_.ref.empty()) {
        return align ? QStringLiteral("Se mueve: %1. Ahora elige adónde llega (cara, arista o eje de otra pieza)").arg(pickText(first_))
                     : QStringLiteral("Fija: %1. Ahora elige la cara o la arista de la pieza que se mueve").arg(pickText(first_));
    }
    QString text = align ? QStringLiteral("Se mueve: %1 — Llega a: %2").arg(pickText(first_), pickText(second_))
                         : QStringLiteral("Fija: %1 — Móvil: %2").arg(pickText(first_), pickText(second_));
    // Una pieza fija no se mueve: se mueve la otra.
    const auto moving = bodyOf(movingPick());
    const auto target = bodyOf(targetPick());
    if (moving && document_.isFixed(*moving)) {
        text += target && document_.isFixed(*target)
                    ? QStringLiteral(" — Las dos piezas están fijas")
                    : QStringLiteral(" — «%1» está fija: se moverá la otra").arg(pickText(movingPick()));
    }
    return text;
}

void JointTool::startAnimation()
{
    // La unión se mueve sola entre sus límites (o una vuelta, o ±25 mm si no los tiene), de ida y vuelta.
    const int index = type_->currentIndex();
    const auto type = index == kAlignType ? document::JointType::Rigid : document::JointType(index - 1);
    using T = document::JointType;
    animatedAngle_ = type == T::Revolute || type == T::Cylindrical || type == T::PinSlot || type == T::Planar || type == T::Ball;
    animatedBox_ = animatedAngle_ ? angle_ : (type == T::Slider || type == T::PinSlot) ? slide_ : nullptr;
    if (type == T::Cylindrical && !animatedAngle_) {
        animatedBox_ = offset_;
    }
    if (animatedBox_ == nullptr) {
        animate_->setChecked(false);
        return;
    }
    animationFrom_ = animatedBox_->value();
    if (animatedAngle_) {
        animationLow_ = limitAngle_->isChecked() ? angleMin_->value() : 0.0;
        animationHigh_ = limitAngle_->isChecked() ? angleMax_->value() : 360.0;
    } else {
        animationLow_ = limitSlide_->isChecked() ? slideMin_->value() : -25.0;
        animationHigh_ = limitSlide_->isChecked() ? slideMax_->value() : 25.0;
    }
    if (animationHigh_ < animationLow_) {
        std::swap(animationLow_, animationHigh_);
    }
    animationPhase_ = 0.0;
    if (animationTimer_ == nullptr) {
        animationTimer_ = new QTimer(this);
        animationTimer_->setInterval(60);
        connect(animationTimer_, &QTimer::timeout, this, [this] {
            if (animatedBox_ == nullptr || previewing()) {
                return;  // un cuadro por cálculo: si el anterior no acabó, se espera
            }
            animationPhase_ += 0.04;
            const double wave = animatedAngle_ && !limitAngle_->isChecked() ? std::fmod(animationPhase_, 1.0)
                                                                            : 0.5 - 0.5 * std::cos(animationPhase_ * 2.0 * std::numbers::pi);
            animatedBox_->setValue(animationLow_ + (animationHigh_ - animationLow_) * wave);
        });
    }
    animationTimer_->start();
}

void JointTool::stopAnimation()
{
    if (animationTimer_ != nullptr && animationTimer_->isActive()) {
        animationTimer_->stop();
        if (animatedBox_ != nullptr) {
            animatedBox_->setValue(animationFrom_);  // vuelve a donde estaba
        }
    }
    animatedBox_ = nullptr;
    if (animate_ != nullptr && animate_->isChecked()) {
        const QSignalBlocker block(animate_);
        animate_->setChecked(false);
    }
}

void JointTool::stopPreview()
{
    stopAnimation();
    FeatureTool::stopPreview();
}

void JointTool::accept()
{
    stopAnimation();  // lo que se acepta son los valores de antes de animar
    FeatureTool::accept();
}

// --- Rosca ----------------------------------------------------------------------------

namespace {

struct MetricThread {
    const char* name;
    double diameter;
    double pitch;
};
// Roscas métricas de paso normal.
constexpr MetricThread kMetric[] = {{"M3", 3.0, 0.5},   {"M4", 4.0, 0.7},   {"M5", 5.0, 0.8},   {"M6", 6.0, 1.0},
                                    {"M8", 8.0, 1.25},  {"M10", 10.0, 1.5}, {"M12", 12.0, 1.75}, {"M16", 16.0, 2.0},
                                    {"M20", 20.0, 2.5}, {"M24", 24.0, 3.0}};

} // namespace

ThreadTool::ThreadTool(document::Document& document, render::Viewport& viewport,
                       std::optional<document::FeatureId> editing)
    : FeatureTool(document, viewport, editing)
{
}

void ThreadTool::createFields(QFormLayout* form)
{
    form_ = form;
    const auto* original = dynamic_cast<const document::ThreadFeature*>(original_.get());
    createPlacement(form, placement_, original != nullptr ? &original->base : nullptr, original != nullptr ? original->x : 0.0,
                    original != nullptr ? original->y : 0.0);
    standard_ = combo({QStringLiteral("Personalizada")});
    for (const MetricThread& m : kMetric) {
        standard_->addItem(QStringLiteral("%1 × %2").arg(QLatin1String(m.name)).arg(m.pitch));
    }
    standard_->setCurrentIndex(5);  // M8
    diameter_ = lengthBox(8.0);
    diameter_->setMinimum(1.0);
    field("diameter", diameter_);
    pitch_ = lengthBox(1.25);
    pitch_->setMinimum(0.1);
    pitch_->setDecimals(3);
    field("pitch", pitch_);
    length_ = lengthBox(10.0);
    field("length", length_);
    clearance_ = lengthBox(0.15);
    clearance_->setMinimum(0.0);
    field("clearance", clearance_);
    left_ = new QCheckBox(QStringLiteral("Rosca a izquierdas"));
    operation_ = combo({QStringLiteral("Cuerpo nuevo"), QStringLiteral("Unir"), QStringLiteral("Cortar (rosca interior)")});
    if (original != nullptr) {
        diameter_->setValue(original->spec.diameter);
        pitch_->setValue(original->spec.pitch);
        length_->setValue(original->spec.length);
        clearance_->setValue(original->clearance);
        left_->setChecked(original->spec.leftHanded);
        operation_->setCurrentIndex(original->operation == document::Operation::Join  ? 1
                                    : original->operation == document::Operation::Cut ? 2
                                                                                      : 0);
        standard_->setCurrentIndex(0);
    }
    form->addRow(QStringLiteral("Medida"), standard_);
    form->addRow(QStringLiteral("Diámetro"), diameter_);
    form->addRow(QStringLiteral("Paso"), pitch_);
    form->addRow(QStringLiteral("Largo (±)"), length_);
    form->addRow(QString(), left_);
    form->addRow(QStringLiteral("Operación"), operation_);
    form->addRow(QStringLiteral("Holgura"), clearance_);
    form->setRowVisible(clearance_, operation_->currentIndex() == 2);
    connect(standard_, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (index > 0) {
            diameter_->setValue(kMetric[index - 1].diameter);
            pitch_->setValue(kMetric[index - 1].pitch);
        }
    });
    for (QDoubleSpinBox* box : {diameter_, pitch_}) {
        connect(box, &QDoubleSpinBox::valueChanged, this, [this] {
            // A mano deja de ser una medida estándar (solo si ya no coincide).
            const int index = standard_->currentIndex();
            if (index > 0 && (diameter_->value() != kMetric[index - 1].diameter || pitch_->value() != kMetric[index - 1].pitch)) {
                const QSignalBlocker block(standard_);
                standard_->setCurrentIndex(0);
            }
        });
    }
    connect(operation_, &QComboBox::currentIndexChanged, this, [this] {
        form_->setRowVisible(clearance_, operation_->currentIndex() == 2);
        adjustDirection();
        updatePreview();
    });
    connect(left_, &QCheckBox::toggled, this, [this] { updatePreview(); });
    for (QDoubleSpinBox* box : {diameter_, pitch_, length_, clearance_}) {
        connect(box, &QDoubleSpinBox::valueChanged, this, [this] { updatePreview(); });
    }
}

void ThreadTool::selectionChanged(const render::ModelSelection& selection)
{
    if (placementSelection(placement_, selection)) {
        adjustDirection();
        updatePreview();
    }
}

void ThreadTool::adjustDirection()
{
    if (!placementOnFace(placement_)) {
        return;
    }
    const double length = std::abs(length_->value());
    const bool cut = operation_->currentIndex() == 2;
    length_->setValue(cut ? -length : length);
}

std::unique_ptr<document::Feature> ThreadTool::build() const
{
    auto f = std::make_unique<document::ThreadFeature>();
    if (original_) {
        f->setName(original_->name());
    }
    f->base = placementAttachment(placement_);
    f->x = placement_.x->value();
    f->y = placement_.y->value();
    f->clearance = clearance_->value();
    f->spec.diameter = diameter_->value();
    f->spec.pitch = pitch_->value();
    f->spec.length = length_->value();
    f->spec.leftHanded = left_->isChecked();
    const int op = operation_->currentIndex();
    f->operation = op == 1 ? document::Operation::Join : op == 2 ? document::Operation::Cut : document::Operation::NewBody;
    return f;
}

QString ThreadTool::inputsText() const
{
    geometry::ThreadSpec spec;
    spec.diameter = diameter_->value();
    spec.pitch = pitch_->value();
    const Units& units = Units::instance();
    if (operation_->currentIndex() == 2) {
        return QStringLiteral("Rosca interior: agujero de Ø %1 (tornillo Ø %2 + holgura) · «Largo» negativo entra en la pieza")
            .arg(units.formatWithUnit(spec.diameter + 2.0 * clearance_->value()), units.formatWithUnit(spec.diameter));
    }
    return QStringLiteral("Ø mayor %1, Ø menor %2")
        .arg(units.formatWithUnit(spec.diameter), units.formatWithUnit(geometry::threadMinorDiameter(spec)));
}

std::optional<FeatureTool::Arrow> ThreadTool::dimensionArrow(const std::string& key) const
{
    if (key == "x" || key == "y") {
        return placementArrow(placement_, key);
    }
    if (key == "pitch") {
        const sketch::Plane plane = document::attachmentPlane(before(), placementAttachment(placement_));
        return arrowAlong(plane.toWorld({placement_.x->value(), placement_.y->value()}), plane.normal, pitch_->value());
    }
    const sketch::Plane plane = document::attachmentPlane(before(), placementAttachment(placement_));
    const sketch::Vec3 origin = plane.toWorld({placement_.x->value(), placement_.y->value()});
    if (key == "length") {
        return arrowAlong(origin, plane.normal, length_->value());
    }
    if (key == "diameter") {
        return arrowAlong(moved(origin, plane.xAxis, -diameter_->value() / 2.0), plane.xAxis, diameter_->value());
    }
    if (key == "clearance") {
        // La holgura es de 0,15 mm: demasiado poco para verla. Se marca lo que resulta, el diámetro del agujero.
        const double hole = diameter_->value() + 2.0 * clearance_->value();
        return arrowAlong(moved(origin, plane.xAxis, -hole / 2.0), plane.xAxis, hole);
    }
    return std::nullopt;
}

bool ThreadTool::removesMaterial() const
{
    return operation_ != nullptr && operation_->currentIndex() == 2;
}

// --- Patrón -----------------------------------------------------------------------

PatternTool::PatternTool(document::Document& document, render::Viewport& viewport,
                         std::optional<document::FeatureId> editing)
    : FeatureTool(document, viewport, editing)
{
    if (const auto* p = dynamic_cast<const document::PatternFeature*>(original_.get())) {
        body_ = p->body;
    }
}

void PatternTool::createFields(QFormLayout* form)
{
    form_ = form;
    bodyBox_ = new QComboBox();
    for (const document::Body& b : before().bodies) {
        bodyBox_->addItem(QString::fromStdString(b.name), QString::fromStdString(b.key));
    }
    if (body_.empty() && bodyBox_->count() > 0) {
        body_ = bodyBox_->itemData(0).toString().toStdString();
    }
    if (const int at = bodyBox_->findData(QString::fromStdString(body_)); at >= 0) {
        bodyBox_->setCurrentIndex(at);
    }
    layout_ = combo({QStringLiteral("Lineal"), QStringLiteral("Circular")});
    axis_ = combo({QStringLiteral("Eje X"), QStringLiteral("Eje Y"), QStringLiteral("Eje Z")});
    for (const auto& [id, line] : before().axes) {
        const document::Feature* f = document_.findFeature(id);
        axis_->addItem(f ? QString::fromStdString(f->name()) : QStringLiteral("Eje"));
        axisIds_.push_back(id);
    }
    count_ = new ExpressionSpinBox();
    count_->setRange(2, 200);
    count_->setDecimals(0);
    count_->setValue(3);
    setUnit(count_, QStringLiteral("copias en total"));
    count_->setKeyboardTracking(false);
    field("count", count_);
    spacing_ = lengthBox(30.0);
    field("spacing", spacing_);
    angle_ = angleBox(360.0, 360.0);
    field("angle", angle_);
    operation_ = combo({QStringLiteral("Unir"), QStringLiteral("Cuerpo nuevo")});
    if (const auto* p = dynamic_cast<const document::PatternFeature*>(original_.get())) {
        layout_->setCurrentIndex(int(p->layout));
        int at = p->axis;
        if (p->axisRef != 0) {
            const auto it = std::ranges::find(axisIds_, p->axisRef);
            at = it != axisIds_.end() ? 3 + int(it - axisIds_.begin()) : 0;
        }
        axis_->setCurrentIndex(at);
        count_->setValue(double(p->count));
        spacing_->setValue(p->spacing);
        angle_->setValue(p->angle / kDegree);
        operation_->setCurrentIndex(p->operation == document::Operation::NewBody ? 1 : 0);
    }
    // Otros cuerpos que se repiten a la vez (O1): una casilla por cada uno.
    others_ = new QListWidget();
    others_->setMaximumHeight(90);
    for (const document::Body& b : before().bodies) {
        auto* item = new QListWidgetItem(QString::fromStdString(b.name), others_);
        item->setData(Qt::UserRole, QString::fromStdString(b.key));
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        const auto* p = dynamic_cast<const document::PatternFeature*>(original_.get());
        item->setCheckState(p != nullptr && std::ranges::find(p->alsoBodies, b.key) != p->alsoBodies.end() ? Qt::Checked : Qt::Unchecked);
    }
    connect(others_, &QListWidget::itemChanged, this, [this] { updatePreview(); });
    form->addRow(QStringLiteral("Cuerpo"), bodyBox_);
    form->addRow(QStringLiteral("Repetir también"), others_);
    form->addRow(QStringLiteral("Tipo"), layout_);
    form->addRow(QStringLiteral("Eje"), axis_);
    form->addRow(QStringLiteral("Cantidad"), count_);
    form->addRow(QStringLiteral("Distancia entre copias"), spacing_);
    form->addRow(QStringLiteral("Ángulo total"), angle_);
    form->addRow(QStringLiteral("Operación"), operation_);
    connect(bodyBox_, &QComboBox::currentIndexChanged, this, [this] {
        body_ = bodyBox_->currentData().toString().toStdString();
        updatePreview();
    });
    connect(layout_, &QComboBox::currentIndexChanged, this, [this] {
        updateVisibility();
        updatePreview();
    });
    for (QComboBox* box : {axis_, operation_}) {
        connect(box, &QComboBox::currentIndexChanged, this, [this] { updatePreview(); });
    }
    connect(count_, &QDoubleSpinBox::valueChanged, this, [this] { updatePreview(); });
    for (QDoubleSpinBox* box : {spacing_, angle_}) {
        connect(box, &QDoubleSpinBox::valueChanged, this, [this] { updatePreview(); });
    }
    updateVisibility();
}

void PatternTool::updateVisibility()
{
    const bool circular = layout_->currentIndex() == 1;
    form_->setRowVisible(spacing_, !circular);
    form_->setRowVisible(angle_, circular);
}

std::unique_ptr<document::Feature> PatternTool::build() const
{
    if (body_.empty()) {
        return nullptr;
    }
    auto f = std::make_unique<document::PatternFeature>();
    if (original_) {
        f->setName(original_->name());
    }
    f->body = body_;
    for (int i = 0; others_ != nullptr && i < others_->count(); ++i) {
        const std::string key = others_->item(i)->data(Qt::UserRole).toString().toStdString();
        if (others_->item(i)->checkState() == Qt::Checked && key != body_) {
            f->alsoBodies.push_back(key);
        }
    }
    f->layout = layout_->currentIndex() == 1 ? document::PatternLayout::Circular : document::PatternLayout::Linear;
    if (axis_->currentIndex() >= 3) {
        f->axisRef = axisIds_[std::size_t(axis_->currentIndex() - 3)];
    } else {
        f->axis = axis_->currentIndex();
    }
    f->count = int(std::lround(count_->value()));
    f->spacing = spacing_->value();
    f->angle = angle_->value() * kDegree;
    f->operation = operation_->currentIndex() == 1 ? document::Operation::NewBody : document::Operation::Join;
    return f;
}

std::optional<FeatureTool::Arrow> PatternTool::dimensionArrow(const std::string& key) const
{
    const auto center = bodyCenter(body_);
    if (key != "spacing" || !center) {
        return std::nullopt;
    }
    sketch::Vec3 dir{1.0, 0.0, 0.0};
    const int at = axis_->currentIndex();
    if (at == 1) {
        dir = {0.0, 1.0, 0.0};
    } else if (at == 2) {
        dir = {0.0, 0.0, 1.0};
    } else if (at >= 3 && std::size_t(at - 3) < axisIds_.size()) {
        const auto it = before().axes.find(axisIds_[std::size_t(at - 3)]);
        if (it == before().axes.end()) {
            return std::nullopt;
        }
        dir = it->second.direction;
    }
    return arrowAlong(*center, dir, spacing_->value());
}

std::optional<FeatureTool::Arc> PatternTool::dimensionArc(const std::string& key) const
{
    if (key != "angle" || layout_->currentIndex() != 1) {
        return std::nullopt;
    }
    sketch::Vec3 point{};
    sketch::Vec3 axis{1.0, 0.0, 0.0};
    const int at = axis_->currentIndex();
    if (at == 1) {
        axis = {0.0, 1.0, 0.0};
    } else if (at == 2) {
        axis = {0.0, 0.0, 1.0};
    } else if (at >= 3 && std::size_t(at - 3) < axisIds_.size()) {
        const auto it = before().axes.find(axisIds_[std::size_t(at - 3)]);
        if (it == before().axes.end()) {
            return std::nullopt;
        }
        point = it->second.point;
        axis = it->second.direction;
    }
    // El arco empieza en el centro del cuerpo (lo que se repite), a su distancia del eje.
    sketch::Vec3 start = perpendicularPoint(point, axis, arcRadius());
    if (const auto center = bodyCenter(body_)) {
        const sketch::Vec3 v{center->x - point.x, center->y - point.y, center->z - point.z};
        const double along = v.x * axis.x + v.y * axis.y + v.z * axis.z;
        const sketch::Vec3 onAxis{point.x + axis.x * along, point.y + axis.y * along, point.z + axis.z * along};
        const sketch::Vec3 radial{center->x - onAxis.x, center->y - onAxis.y, center->z - onAxis.z};
        if (radial.x * radial.x + radial.y * radial.y + radial.z * radial.z > 1e-6) {
            start = *center;
        } else {
            start = perpendicularPoint(onAxis, axis, arcRadius());
        }
    }
    return Arc{point, axis, start, angle_->value() * kDegree};
}

QString PatternTool::inputsText() const
{
    if (bodyBox_->count() == 0) {
        return QStringLiteral("No hay cuerpos que repetir");
    }
    return QStringLiteral("Patrón %1 de «%2»: %3 en total")
        .arg(layout_->currentIndex() == 1 ? QStringLiteral("circular") : QStringLiteral("lineal"))
        .arg(bodyBox_->currentText())
        .arg(int(std::lround(count_->value())));
}

} // namespace nuf::app
