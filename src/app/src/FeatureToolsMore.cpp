#include "FeatureTools.hpp"

#include "ExpressionSpinBox.hpp"
#include "Units.hpp"

#include <nuf/geometry/Modeling.hpp>
#include <nuf/render/Viewport.hpp>

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
    form->addRow(QStringLiteral("Orientación"), orientation_);
    form->addRow(QStringLiteral("Operación"), operation_);
    for (QComboBox* box : {orientation_, operation_}) {
        connect(box, &QComboBox::currentIndexChanged, this, [this] { updatePreview(); });
    }
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

MoveTool::MoveTool(document::Document& document, render::Viewport& viewport,
                   std::optional<document::FeatureId> editing)
    : FeatureTool(document, viewport, editing)
{
    if (const auto* m = dynamic_cast<const document::MoveFeature*>(original_.get())) {
        body_ = m->body;
    }
}

void MoveTool::createFields(QFormLayout* form)
{
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
    x_ = lengthBox(0.0);
    field("x", x_);
    y_ = lengthBox(0.0);
    field("y", y_);
    z_ = lengthBox(0.0);
    field("z", z_);
    axis_ = combo({QStringLiteral("Eje X"), QStringLiteral("Eje Y"), QStringLiteral("Eje Z")});
    for (const auto& [id, line] : before().axes) {
        const document::Feature* f = document_.findFeature(id);
        axis_->addItem(f ? QString::fromStdString(f->name()) : QStringLiteral("Eje"));
        axisIds_.push_back(id);
    }
    axis_->setCurrentIndex(2);
    around_ = combo({QStringLiteral("Centro del cuerpo"), QStringLiteral("Origen")});
    angle_ = angleBox(0.0, 360.0);
    field("angle", angle_);
    copy_ = new QCheckBox(QStringLiteral("Crear una copia (deja el original)"));
    if (const auto* m = dynamic_cast<const document::MoveFeature*>(original_.get())) {
        x_->setValue(m->translation.x);
        y_->setValue(m->translation.y);
        z_->setValue(m->translation.z);
        int at = m->axis;
        if (m->axisRef != 0) {
            const auto it = std::ranges::find(axisIds_, m->axisRef);
            at = it != axisIds_.end() ? 3 + int(it - axisIds_.begin()) : 2;
        }
        axis_->setCurrentIndex(at);
        around_->setCurrentIndex(m->aroundOrigin ? 1 : 0);
        angle_->setValue(m->angle / kDegree);
        copy_->setChecked(m->copy);
    }
    form->addRow(QStringLiteral("Cuerpo"), bodyBox_);
    form->addRow(QStringLiteral("Mover en X"), x_);
    form->addRow(QStringLiteral("Mover en Y"), y_);
    form->addRow(QStringLiteral("Mover en Z"), z_);
    form->addRow(QStringLiteral("Girar alrededor de"), axis_);
    form->addRow(QStringLiteral("El eje pasa por"), around_);
    form->addRow(QStringLiteral("Ángulo"), angle_);
    form->addRow(QString(), copy_);
    connect(bodyBox_, &QComboBox::currentIndexChanged, this, [this] {
        body_ = bodyBox_->currentData().toString().toStdString();
        updatePreview();
    });
    for (QComboBox* box : {axis_, around_}) {
        connect(box, &QComboBox::currentIndexChanged, this, [this] { updatePreview(); });
    }
    for (QDoubleSpinBox* box : {x_, y_, z_, angle_}) {
        connect(box, &QDoubleSpinBox::valueChanged, this, [this] { updatePreview(); });
    }
    connect(copy_, &QCheckBox::toggled, this, [this] { updatePreview(); });
}

void MoveTool::selectionChanged(const render::ModelSelection& selection)
{
    // Un clic en una cara elige su cuerpo.
    if (!selection.faces.empty()) {
        const int at = bodyBox_->findData(QString::fromStdString(selection.faces.front().body));
        if (at >= 0) {
            bodyBox_->setCurrentIndex(at);
        }
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
    f->translation = {x_->value(), y_->value(), z_->value()};
    if (axis_->currentIndex() >= 3) {
        f->axisRef = axisIds_[std::size_t(axis_->currentIndex() - 3)];
    } else {
        f->axis = axis_->currentIndex();
    }
    f->aroundOrigin = around_->currentIndex() == 1;
    f->angle = angle_->value() * kDegree;
    f->copy = copy_->isChecked();
    return f;
}

std::optional<FeatureTool::Arrow> MoveTool::dimensionArrow(const std::string& key) const
{
    const auto center = bodyCenter(body_);
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

QString MoveTool::inputsText() const
{
    if (bodyBox_->count() == 0) {
        return QStringLiteral("No hay cuerpos que mover");
    }
    return QStringLiteral("Mover «%1» (o haz clic en una de sus caras para elegirlo)").arg(bodyBox_->currentText());
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

// --- Unión ---------------------------------------------------------------------------------

JointTool::JointTool(document::Document& document, render::Viewport& viewport,
                     std::optional<document::FeatureId> editing)
    : FeatureTool(document, viewport, editing)
{
    if (const auto* j = dynamic_cast<const document::JointFeature*>(original_.get())) {
        fixed_ = j->fixedFace;
        moving_ = j->movingFace;
    }
}

void JointTool::createFields(QFormLayout* form)
{
    form_ = form;
    type_ = combo({QStringLiteral("Rígida"), QStringLiteral("Giratoria (con ángulo)"),
                   QStringLiteral("Deslizante (con desplazamiento)")});
    offset_ = lengthBox(0.0);
    field("offset", offset_);
    angle_ = angleBox(0.0, 360.0);
    field("angle", angle_);
    slide_ = lengthBox(0.0);
    field("slide", slide_);
    flip_ = new QCheckBox(QStringLiteral("Caras del mismo lado (no enfrentadas)"));
    auto* swap = new QPushButton(QStringLiteral("Intercambiar fija y móvil"));
    if (const auto* j = dynamic_cast<const document::JointFeature*>(original_.get())) {
        type_->setCurrentIndex(int(j->type));
        offset_->setValue(j->offset);
        angle_->setValue(j->angle / kDegree);
        slide_->setValue(j->slide);
        flip_->setChecked(j->flip);
    }
    form->addRow(QStringLiteral("Tipo"), type_);
    form->addRow(QStringLiteral("Distancia"), offset_);
    form->addRow(QStringLiteral("Ángulo"), angle_);
    form->addRow(QStringLiteral("Desplazamiento"), slide_);
    form->addRow(QString(), flip_);
    form->addRow(QString(), swap);
    connect(type_, &QComboBox::currentIndexChanged, this, [this] {
        updateVisibility();
        updatePreview();
    });
    for (QDoubleSpinBox* box : {offset_, angle_, slide_}) {
        connect(box, &QDoubleSpinBox::valueChanged, this, [this] { updatePreview(); });
    }
    connect(flip_, &QCheckBox::toggled, this, [this] { updatePreview(); });
    connect(swap, &QPushButton::clicked, this, [this] {
        std::swap(fixed_, moving_);
        updatePreview();
    });
    updateVisibility();
}

void JointTool::updateVisibility()
{
    const int type = type_->currentIndex();
    form_->setRowVisible(angle_, type == int(document::JointType::Revolute));
    form_->setRowVisible(slide_, type == int(document::JointType::Slider));
}

void JointTool::selectionChanged(const render::ModelSelection& selection)
{
    // Cada clic en una cara la elige (primero la fija, luego la móvil) o, si ya estaba elegida, la quita;
    // la lista es de la herramienta y el visor se deja sin selección (la vista previa lo redibuja todo).
    for (const render::ModelSelection::Face& f : selection.faces) {
        if (!before().findFace(f.ref)) {
            continue;  // una cara del resultado de la vista previa que no existe antes
        }
        if (f.ref == fixed_) {
            fixed_ = moving_;
            moving_ = {};
        } else if (f.ref == moving_) {
            moving_ = {};
        } else if (fixed_.empty()) {
            fixed_ = f.ref;
        } else {
            moving_ = f.ref;
        }
    }
    if (!selection.empty()) {
        viewport_.clearModelSelection();
    }
}

QString JointTool::faceText(const geometry::ElementRef& ref) const
{
    if (ref.empty()) {
        return QStringLiteral("—");
    }
    const auto hit = before().findFace(ref);
    return hit ? QString::fromStdString(before().bodies[hit->body].name) : QStringLiteral("(ya no existe)");
}

std::unique_ptr<document::Feature> JointTool::build() const
{
    if (fixed_.empty() || moving_.empty()) {
        return nullptr;
    }
    auto f = std::make_unique<document::JointFeature>();
    if (original_) {
        f->setName(original_->name());
    }
    f->fixedFace = fixed_;
    f->movingFace = moving_;
    f->type = document::JointType(type_->currentIndex());
    f->offset = offset_->value();
    f->angle = angle_->value() * kDegree;
    f->slide = slide_->value();
    f->flip = flip_->isChecked();
    return f;
}

QString JointTool::inputsText() const
{
    if (fixed_.empty()) {
        return QStringLiteral("Elige en la vista una cara del cuerpo que se queda quieto y luego una cara del que se mueve");
    }
    if (moving_.empty()) {
        return QStringLiteral("Fija: %1. Ahora elige la cara del cuerpo que se mueve").arg(faceText(fixed_));
    }
    return QStringLiteral("Fija: %1 — Móvil: %2").arg(faceText(fixed_), faceText(moving_));
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
    const sketch::Plane plane = document::attachmentPlane(before(), placementAttachment(placement_));
    const sketch::Vec3 origin = plane.toWorld({placement_.x->value(), placement_.y->value()});
    if (key == "length") {
        return arrowAlong(origin, plane.normal, length_->value());
    }
    if (key == "diameter") {
        return arrowAlong(moved(origin, plane.xAxis, -diameter_->value() / 2.0), plane.xAxis, diameter_->value());
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
    form->addRow(QStringLiteral("Cuerpo"), bodyBox_);
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
