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

// --- Espejo ------------------------------------------------------------------------

MirrorTool::MirrorTool(document::Document& document, render::Viewport& viewport,
                       std::optional<document::FeatureId> editing)
    : FeatureTool(document, viewport, editing)
{
    if (const auto* m = dynamic_cast<const document::MirrorFeature*>(original_.get())) {
        body_ = m->body;
        face_ = m->plane.face;
    }
}

void MirrorTool::createFields(QFormLayout* form)
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
    plane_ = combo({QStringLiteral("Plano XY"), QStringLiteral("Plano XZ"), QStringLiteral("Plano YZ"),
                    QStringLiteral("Cara plana elegida")});
    operation_ = combo({QStringLiteral("Unir"), QStringLiteral("Cuerpo nuevo")});
    for (const auto& [id, plane] : before().planes) {
        const document::Feature* f = document_.findFeature(id);
        plane_->addItem(f ? QString::fromStdString(f->name()) : QStringLiteral("Plano"));
        planeIds_.push_back(id);
    }
    plane_->setCurrentIndex(2);  // YZ: la simetría más común
    if (const auto* m = dynamic_cast<const document::MirrorFeature*>(original_.get())) {
        int at = m->plane.origin ? int(*m->plane.origin) : 3;
        if (!m->plane.origin && m->plane.reference != 0) {
            const auto it = std::ranges::find(planeIds_, m->plane.reference);
            at = it != planeIds_.end() ? 4 + int(it - planeIds_.begin()) : 3;
        }
        plane_->setCurrentIndex(at);
        operation_->setCurrentIndex(m->operation == document::Operation::NewBody ? 1 : 0);
    }
    form->addRow(QStringLiteral("Cuerpo"), bodyBox_);
    form->addRow(QStringLiteral("Plano de simetría"), plane_);
    form->addRow(QStringLiteral("Operación"), operation_);
    connect(bodyBox_, &QComboBox::currentIndexChanged, this, [this] {
        body_ = bodyBox_->currentData().toString().toStdString();
        updatePreview();
    });
    for (QComboBox* box : {plane_, operation_}) {
        connect(box, &QComboBox::currentIndexChanged, this, [this] { updatePreview(); });
    }
}

void MirrorTool::selectionChanged(const render::ModelSelection& selection)
{
    // Una cara plana elegida en la vista es el plano de simetría.
    for (const render::ModelSelection::Face& face : selection.faces) {
        if (const auto hit = before().findFace(face.ref); hit && before().bodies[hit->body].shape.facePlane(hit->face)) {
            face_ = face.ref;
            plane_->setCurrentIndex(3);
            break;
        }
    }
}

std::unique_ptr<document::Feature> MirrorTool::build() const
{
    if (body_.empty()) {
        return nullptr;
    }
    auto f = std::make_unique<document::MirrorFeature>();
    if (original_) {
        f->setName(original_->name());
    }
    f->body = body_;
    f->operation = operation_->currentIndex() == 1 ? document::Operation::NewBody : document::Operation::Join;
    const int plane = plane_->currentIndex();
    if (plane < 3) {
        f->plane.origin = document::OriginPlane(plane);
        return f;
    }
    if (plane >= 4) {
        const document::FeatureId id = planeIds_[std::size_t(plane - 4)];
        f->plane.origin.reset();
        f->plane.reference = id;
        f->plane.lastPlane = before().planes.at(id);
        return f;
    }
    // Cara plana: hace falta que haya una elegida.
    const auto hit = before().findFace(face_);
    if (!hit) {
        return nullptr;
    }
    const auto facePlane = before().bodies[hit->body].shape.facePlane(hit->face);
    if (!facePlane) {
        return nullptr;
    }
    f->plane.origin.reset();
    f->plane.face = face_;
    f->plane.lastPlane = *facePlane;
    return f;
}

QString MirrorTool::inputsText() const
{
    if (bodyBox_->count() == 0) {
        return QStringLiteral("No hay cuerpos que reflejar");
    }
    if (plane_->currentIndex() == 3 && face_.empty()) {
        return QStringLiteral("Elige en la vista la cara plana que hace de plano de simetría");
    }
    return QStringLiteral("Reflejar «%1»").arg(bodyBox_->currentText());
}

// --- Plano de construcción ---------------------------------------------------------

PlaneTool::PlaneTool(document::Document& document, render::Viewport& viewport,
                     std::optional<document::FeatureId> editing)
    : FeatureTool(document, viewport, editing)
{
    if (const auto* p = dynamic_cast<const document::ConstructionPlaneFeature*>(original_.get())) {
        face_ = p->base.face;
        reference_ = p->base.reference;
    }
}

void PlaneTool::createFields(QFormLayout* form)
{
    base_ = combo({QStringLiteral("Plano XY"), QStringLiteral("Plano XZ"), QStringLiteral("Plano YZ"),
                   QStringLiteral("Cara plana elegida")});
    for (const auto& [id, plane] : before().planes) {
        const document::Feature* f = document_.findFeature(id);
        base_->addItem(f ? QString::fromStdString(f->name()) : QStringLiteral("Plano"));
        planeIds_.push_back(id);
    }
    offset_ = lengthBox(10.0);
    field("offset", offset_);
    angle_ = angleBox(0.0, 180.0);
    field("angle", angle_);
    angleAxis_ = combo({QStringLiteral("Eje X del plano"), QStringLiteral("Eje Y del plano")});
    base_->setCurrentIndex(0);
    if (const auto* p = dynamic_cast<const document::ConstructionPlaneFeature*>(original_.get())) {
        int at = p->base.origin ? int(*p->base.origin) : 3;
        if (!p->base.origin && p->base.reference != 0) {
            const auto it = std::ranges::find(planeIds_, p->base.reference);
            at = it != planeIds_.end() ? 4 + int(it - planeIds_.begin()) : 3;
        }
        base_->setCurrentIndex(at);
        offset_->setValue(p->offset);
        angle_->setValue(p->angle / kDegree);
        angleAxis_->setCurrentIndex(p->angleAxis);
    }
    form->addRow(QStringLiteral("Plano base"), base_);
    form->addRow(QStringLiteral("Distancia"), offset_);
    form->addRow(QStringLiteral("Ángulo"), angle_);
    form->addRow(QStringLiteral("Girar alrededor de"), angleAxis_);
    for (QComboBox* box : {base_, angleAxis_}) {
        connect(box, &QComboBox::currentIndexChanged, this, [this] { updatePreview(); });
    }
    for (QDoubleSpinBox* box : {offset_, angle_}) {
        connect(box, &QDoubleSpinBox::valueChanged, this, [this] { updatePreview(); });
    }
}

void PlaneTool::selectionChanged(const render::ModelSelection& selection)
{
    // Una cara plana elegida en la vista es la base.
    for (const render::ModelSelection::Face& face : selection.faces) {
        if (const auto hit = before().findFace(face.ref); hit && before().bodies[hit->body].shape.facePlane(hit->face)) {
            face_ = face.ref;
            base_->setCurrentIndex(3);
            break;
        }
    }
}

std::unique_ptr<document::Feature> PlaneTool::build() const
{
    auto f = std::make_unique<document::ConstructionPlaneFeature>();
    if (original_) {
        f->setName(original_->name());
    }
    f->offset = offset_->value();
    f->angle = angle_->value() * kDegree;
    f->angleAxis = angleAxis_->currentIndex();
    const int base = base_->currentIndex();
    if (base < 3) {
        f->base.origin = document::OriginPlane(base);
        return f;
    }
    f->base.origin.reset();
    if (base >= 4) {
        const document::FeatureId id = planeIds_[std::size_t(base - 4)];
        f->base.reference = id;
        f->base.lastPlane = before().planes.at(id);
        return f;
    }
    const auto hit = before().findFace(face_);
    if (!hit) {
        return nullptr;
    }
    const auto facePlane = before().bodies[hit->body].shape.facePlane(hit->face);
    if (!facePlane) {
        return nullptr;
    }
    f->base.face = face_;
    f->base.lastPlane = *facePlane;
    return f;
}

std::optional<FeatureTool::Arrow> PlaneTool::dimensionArrow(const std::string& key) const
{
    if (key != "offset") {
        return std::nullopt;
    }
    const auto feature = build();
    const auto* plane = dynamic_cast<const document::ConstructionPlaneFeature*>(feature.get());
    if (plane == nullptr) {
        return std::nullopt;
    }
    const sketch::Plane base = document::attachmentPlane(before(), plane->base);
    return arrowAlong(base.origin, base.normal, offset_->value());
}

std::optional<FeatureTool::Arc> PlaneTool::dimensionArc(const std::string& key) const
{
    if (key != "angle") {
        return std::nullopt;
    }
    const auto feature = build();
    const auto* plane = dynamic_cast<const document::ConstructionPlaneFeature*>(feature.get());
    if (plane == nullptr) {
        return std::nullopt;
    }
    // El plano gira alrededor de un eje suyo (X o Y) que pasa por su origen desplazado.
    const sketch::Plane base = document::attachmentPlane(before(), plane->base);
    const sketch::Vec3 origin = moved(base.origin, base.normal, plane->offset);
    const bool aboutX = plane->angleAxis == 0;
    const sketch::Vec3 axis = aboutX ? base.xAxis : base.yAxis;
    const sketch::Vec3 across = aboutX ? base.yAxis : base.xAxis;
    return Arc{origin, axis, moved(origin, across, arcRadius()), angle_->value() * kDegree * (aboutX ? 1.0 : -1.0)};
}

QString PlaneTool::inputsText() const
{
    if (base_->currentIndex() == 3 && face_.empty()) {
        return QStringLiteral("Elige en la vista la cara plana de la que parte el plano");
    }
    return QStringLiteral("Plano a %1 de «%2»").arg(Units::instance().formatWithUnit(offset_->value()), base_->currentText());
}

// --- Eje de construcción -----------------------------------------------------------

AxisTool::AxisTool(document::Document& document, render::Viewport& viewport,
                   std::optional<document::FeatureId> editing)
    : FeatureTool(document, viewport, editing)
{
    if (const auto* a = dynamic_cast<const document::ConstructionAxisFeature*>(original_.get())) {
        face_ = a->first.face;
    }
}

void AxisTool::createFields(QFormLayout* form)
{
    first_ = combo({QStringLiteral("Plano XY"), QStringLiteral("Plano XZ"), QStringLiteral("Plano YZ"),
                    QStringLiteral("Cara plana elegida")});
    second_ = combo({QStringLiteral("Ninguno (perpendicular al primero)"), QStringLiteral("Plano XY"),
                     QStringLiteral("Plano XZ"), QStringLiteral("Plano YZ")});
    for (const auto& [id, plane] : before().planes) {
        const document::Feature* f = document_.findFeature(id);
        const QString name = f ? QString::fromStdString(f->name()) : QStringLiteral("Plano");
        first_->addItem(name);
        second_->addItem(name);
        planeIds_.push_back(id);
    }
    second_->setCurrentIndex(2);  // XZ: con XY da el eje Y
    first_->setCurrentIndex(0);
    if (const auto* a = dynamic_cast<const document::ConstructionAxisFeature*>(original_.get())) {
        const auto indexOf = [&](const document::SketchAttachment& at, int whenFace) {
            if (at.origin) {
                return int(*at.origin);
            }
            if (at.reference != 0) {
                const auto it = std::ranges::find(planeIds_, at.reference);
                return it != planeIds_.end() ? 4 + int(it - planeIds_.begin()) : whenFace;
            }
            return whenFace;
        };
        first_->setCurrentIndex(indexOf(a->first, 3));
        if (!a->useSecond) {
            second_->setCurrentIndex(0);
        } else {
            const int at = indexOf(a->second, 0);
            second_->setCurrentIndex(a->second.origin ? at + 1 : at);
        }
    }
    form->addRow(QStringLiteral("Primer plano"), first_);
    form->addRow(QStringLiteral("Segundo plano"), second_);
    for (QComboBox* box : {first_, second_}) {
        connect(box, &QComboBox::currentIndexChanged, this, [this] { updatePreview(); });
    }
}

void AxisTool::selectionChanged(const render::ModelSelection& selection)
{
    for (const render::ModelSelection::Face& face : selection.faces) {
        if (const auto hit = before().findFace(face.ref); hit && before().bodies[hit->body].shape.facePlane(hit->face)) {
            face_ = face.ref;
            first_->setCurrentIndex(3);
            break;
        }
    }
}

std::unique_ptr<document::Feature> AxisTool::build() const
{
    auto f = std::make_unique<document::ConstructionAxisFeature>();
    if (original_) {
        f->setName(original_->name());
    }
    // Primer plano.
    const int a = first_->currentIndex();
    if (a < 3) {
        f->first.origin = document::OriginPlane(a);
    } else if (a >= 4) {
        const document::FeatureId id = planeIds_[std::size_t(a - 4)];
        f->first.origin.reset();
        f->first.reference = id;
        f->first.lastPlane = before().planes.at(id);
    } else {
        const auto hit = before().findFace(face_);
        const auto facePlane = hit ? before().bodies[hit->body].shape.facePlane(hit->face) : std::nullopt;
        if (!facePlane) {
            return nullptr;
        }
        f->first.origin.reset();
        f->first.face = face_;
        f->first.lastPlane = *facePlane;
    }
    // Segundo plano (o ninguno).
    const int b = second_->currentIndex();
    f->useSecond = b != 0;
    if (b >= 1 && b <= 3) {
        f->second.origin = document::OriginPlane(b - 1);
    } else if (b >= 4) {
        const document::FeatureId id = planeIds_[std::size_t(b - 4)];
        f->second.origin.reset();
        f->second.reference = id;
        f->second.lastPlane = before().planes.at(id);
    }
    return f;
}

QString AxisTool::inputsText() const
{
    if (first_->currentIndex() == 3 && face_.empty()) {
        return QStringLiteral("Elige en la vista la cara plana del primer plano");
    }
    return second_->currentIndex() == 0
               ? QStringLiteral("Perpendicular a «%1»").arg(first_->currentText())
               : QStringLiteral("Intersección de «%1» y «%2»").arg(first_->currentText(), second_->currentText());
}

// --- Engranaje -----------------------------------------------------------------------

GearTool::GearTool(document::Document& document, render::Viewport& viewport,
                   std::optional<document::FeatureId> editing)
    : FeatureTool(document, viewport, editing)
{
}

void GearTool::createFields(QFormLayout* form)
{
    const auto* original = dynamic_cast<const document::GearFeature*>(original_.get());
    createPlacement(form, placement_, original != nullptr ? &original->base : nullptr, original != nullptr ? original->x : 0.0,
                    original != nullptr ? original->y : 0.0);
    module_ = lengthBox(2.0);
    module_->setMinimum(0.1);
    field("module", module_);
    teeth_ = new ExpressionSpinBox();
    teeth_->setRange(6, 300);
    teeth_->setDecimals(0);
    teeth_->setValue(20);
    teeth_->setKeyboardTracking(false);
    field("teeth", teeth_);
    rotation_ = angleBox(original != nullptr ? original->rotation / kDegree : 0.0, 360.0);
    field("rotation", rotation_);
    pressure_ = angleBox(20.0, 35.0);
    pressure_->setMinimum(5.0);
    field("pressureAngle", pressure_);
    thickness_ = lengthBox(10.0);
    field("thickness", thickness_);
    bore_ = lengthBox(5.0);
    bore_->setMinimum(0.0);
    field("bore", bore_);
    helix_ = angleBox(0.0, 45.0);
    field("helixAngle", helix_);
    operation_ = combo({QStringLiteral("Cuerpo nuevo"), QStringLiteral("Unir"), QStringLiteral("Cortar")});
    if (original != nullptr) {
        module_->setValue(original->spec.module);
        teeth_->setValue(original->spec.teeth);
        pressure_->setValue(original->spec.pressureAngle / kDegree);
        thickness_->setValue(original->spec.thickness);
        bore_->setValue(original->spec.bore);
        helix_->setValue(original->spec.helixAngle / kDegree);
        operation_->setCurrentIndex(original->operation == document::Operation::Join  ? 1
                                    : original->operation == document::Operation::Cut ? 2
                                                                                      : 0);
    }
    form->addRow(QStringLiteral("Módulo"), module_);
    form->addRow(QStringLiteral("Dientes"), teeth_);
    form->addRow(QStringLiteral("Giro inicial"), rotation_);
    form->addRow(QStringLiteral("Ángulo de presión"), pressure_);
    form->addRow(QStringLiteral("Ancho (±)"), thickness_);
    form->addRow(QStringLiteral("Agujero central"), bore_);
    form->addRow(QStringLiteral("Ángulo de hélice"), helix_);
    form->addRow(QStringLiteral("Operación"), operation_);
    connect(operation_, &QComboBox::currentIndexChanged, this, [this] {
        adjustDirection();
        updatePreview();
    });
    for (QDoubleSpinBox* box : {teeth_, rotation_, module_, pressure_, thickness_, bore_, helix_}) {
        connect(box, &QDoubleSpinBox::valueChanged, this, [this] { updatePreview(); });
    }
}

void GearTool::selectionChanged(const render::ModelSelection& selection)
{
    if (placementSelection(placement_, selection)) {
        adjustDirection();
        updatePreview();
    }
}

void GearTool::adjustDirection()
{
    if (!placementOnFace(placement_)) {
        return;
    }
    const double width = std::abs(thickness_->value());
    const bool cut = operation_->currentIndex() == 2;
    thickness_->setValue(cut ? -width : width);
}

std::unique_ptr<document::Feature> GearTool::build() const
{
    auto f = std::make_unique<document::GearFeature>();
    if (original_) {
        f->setName(original_->name());
    }
    f->base = placementAttachment(placement_);
    f->x = placement_.x->value();
    f->y = placement_.y->value();
    f->spec.module = module_->value();
    f->rotation = rotation_->value() * kDegree;
    f->spec.teeth = int(std::lround(teeth_->value()));
    f->spec.pressureAngle = pressure_->value() * kDegree;
    f->spec.thickness = thickness_->value();
    f->spec.bore = bore_->value();
    f->spec.helixAngle = helix_->value() * kDegree;
    const int op = operation_->currentIndex();
    f->operation = op == 1 ? document::Operation::Join : op == 2 ? document::Operation::Cut : document::Operation::NewBody;
    return f;
}

std::optional<FeatureTool::Arrow> GearTool::dimensionArrow(const std::string& key) const
{
    if (key == "x" || key == "y") {
        return placementArrow(placement_, key);
    }
    const sketch::Plane plane = document::attachmentPlane(before(), placementAttachment(placement_));
    const sketch::Vec3 origin = plane.toWorld({placement_.x->value(), placement_.y->value()});
    if (key == "thickness") {
        return arrowAlong(origin, plane.normal, thickness_->value());
    }
    if (key == "bore") {
        return arrowAlong(moved(origin, plane.xAxis, -bore_->value() / 2.0), plane.xAxis, bore_->value());
    }
    if (key == "module" || key == "teeth") {
        // El diámetro primitivo: módulo × dientes.
        const double pitch = module_->value() * teeth_->value();
        return arrowAlong(moved(origin, plane.xAxis, -pitch / 2.0), plane.xAxis, pitch);
    }
    return std::nullopt;
}

std::optional<FeatureTool::Arc> GearTool::dimensionArc(const std::string& key) const
{
    if (key != "rotation" && key != "pressureAngle" && key != "helixAngle") {
        return std::nullopt;
    }
    const sketch::Plane plane = document::attachmentPlane(before(), placementAttachment(placement_));
    const sketch::Vec3 origin = plane.toWorld({placement_.x->value(), placement_.y->value()});
    const double pitchRadius = module_->value() * teeth_->value() / 2.0;
    const sketch::Vec3 onPitch = moved(origin, plane.xAxis, pitchRadius);
    const double size = std::max(3.0 * module_->value(), 3.0);
    // El arco del ángulo de presión es lo más pequeño de todas las flechas (con 6 mm apenas se veía): se hace de medio
    // radio primitivo, que es proporcional a la rueda.
    const double pressureSize = std::max(0.5 * pitchRadius, size);
    if (key == "rotation") {
        // Dónde queda el primer diente: el giro desde el eje X del plano.
        return Arc{origin, plane.normal, onPitch, rotation_->value() * kDegree};
    }
    if (key == "pressureAngle") {
        // En el punto primitivo: entre la tangente a la circunferencia primitiva y la línea de acción.
        return Arc{onPitch, plane.normal, moved(onPitch, plane.yAxis, pressureSize), pressure_->value() * kDegree};
    }
    // La hélice: entre el eje del engranaje y la dirección del diente.
    const double along = thickness_->value() < 0.0 ? -1.0 : 1.0;
    return Arc{onPitch, plane.xAxis, moved(onPitch, plane.normal, along * 1.5 * size), helix_->value() * kDegree};
}

QString GearTool::inputsText() const
{
    return QStringLiteral("%1 dientes, módulo %2: Ø primitivo %3%4")
        .arg(int(std::lround(teeth_->value())))
        .arg(Units::instance().format(module_->value()))
        .arg(Units::instance().formatWithUnit(module_->value() * std::lround(teeth_->value())),
             placementOnFace(placement_) ? QStringLiteral(" · en la cara elegida") : QString());
}

// --- Caja y cilindro --------------------------------------------------------------------

namespace {

document::Operation primitiveOperation(int index)
{
    switch (index) {
    case 1: return document::Operation::Join;
    case 2: return document::Operation::Cut;
    case 3: return document::Operation::Intersect;
    default: return document::Operation::NewBody;
    }
}

int primitiveIndex(document::Operation operation)
{
    switch (operation) {
    case document::Operation::Join: return 1;
    case document::Operation::Cut: return 2;
    case document::Operation::Intersect: return 3;
    case document::Operation::NewBody: return 0;
    }
    return 0;
}

} // namespace

BoxTool::BoxTool(document::Document& document, render::Viewport& viewport, std::optional<document::FeatureId> editing)
    : FeatureTool(document, viewport, editing)
{
}

void BoxTool::createFields(QFormLayout* form)
{
    const auto* original = dynamic_cast<const document::BoxFeature*>(original_.get());
    createPlacement(form, placement_, original != nullptr ? &original->base : nullptr, original != nullptr ? original->x : 0.0,
                    original != nullptr ? original->y : 0.0);
    z_ = lengthBox(original != nullptr ? original->z : 0.0);
    field("z", z_);
    width_ = lengthBox(original != nullptr ? original->width : 20.0);
    width_->setMinimum(0.01);
    field("width", width_);
    depth_ = lengthBox(original != nullptr ? original->depth : 20.0);
    depth_->setMinimum(0.01);
    field("depth", depth_);
    height_ = lengthBox(original != nullptr ? original->height : 10.0);
    field("height", height_);
    radius_ = lengthBox(original != nullptr ? original->cornerRadius : 0.0);
    radius_->setMinimum(0.0);
    field("radius", radius_);
    centered_ = new QCheckBox(QStringLiteral("Centrada en el punto (ancho y fondo)"));
    centered_->setChecked(original != nullptr && original->centered);
    symmetric_ = new QCheckBox(QStringLiteral("Alto repartido a los dos lados del plano"));
    symmetric_->setChecked(original != nullptr && original->symmetric);
    operation_ = combo({QStringLiteral("Cuerpo nuevo"), QStringLiteral("Unir"), QStringLiteral("Cortar"), QStringLiteral("Intersecar")});
    operation_->setCurrentIndex(original != nullptr ? primitiveIndex(original->operation) : before().bodies.empty() ? 0 : 1);
    form->addRow(QStringLiteral("Posición Z"), z_);
    form->addRow(QStringLiteral("Ancho"), width_);
    form->addRow(QStringLiteral("Fondo"), depth_);
    form->addRow(QStringLiteral("Alto (±)"), height_);
    form->addRow(QStringLiteral("Radio de esquinas"), radius_);
    form->addRow(QString(), centered_);
    form->addRow(QString(), symmetric_);
    form->addRow(QStringLiteral("Operación"), operation_);
    for (QDoubleSpinBox* box : {z_, width_, depth_, height_, radius_}) {
        connect(box, &QDoubleSpinBox::valueChanged, this, [this] { updatePreview(); });
    }
    for (QCheckBox* box : {centered_, symmetric_}) {
        connect(box, &QCheckBox::toggled, this, [this] { updatePreview(); });
    }
    connect(operation_, &QComboBox::currentIndexChanged, this, [this] { updatePreview(); });
}

void BoxTool::selectionChanged(const render::ModelSelection& selection)
{
    if (placementSelection(placement_, selection)) {
        updatePreview();
    }
}

std::unique_ptr<document::Feature> BoxTool::build() const
{
    auto f = std::make_unique<document::BoxFeature>();
    if (original_) {
        f->setName(original_->name());
    }
    f->base = placementAttachment(placement_);
    f->x = placement_.x->value();
    f->y = placement_.y->value();
    f->z = z_->value();
    f->width = width_->value();
    f->depth = depth_->value();
    f->height = height_->value();
    f->cornerRadius = radius_->value();
    f->centered = centered_->isChecked();
    f->symmetric = symmetric_->isChecked();
    f->operation = primitiveOperation(operation_->currentIndex());
    return f;
}

bool BoxTool::removesMaterial() const
{
    return operation_ != nullptr && operation_->currentIndex() == 2;
}

std::optional<FeatureTool::Arrow> BoxTool::dimensionArrow(const std::string& key) const
{
    if (key == "x" || key == "y") {
        return placementArrow(placement_, key);
    }
    const sketch::Plane plane = document::attachmentPlane(before(), placementAttachment(placement_));
    const sketch::Vec3 point = plane.toWorld({placement_.x->value(), placement_.y->value()});
    const double height = height_->value();
    if (key == "z") {
        return arrowAlong(point, plane.normal, z_->value());
    }
    sketch::Vec3 corner = moved(point, plane.normal, z_->value() - (symmetric_->isChecked() ? height / 2.0 : 0.0));
    if (centered_->isChecked()) {
        corner = moved(moved(corner, plane.xAxis, -width_->value() / 2.0), plane.yAxis, -depth_->value() / 2.0);
    }
    if (key == "radius") {
        // El radio de la esquina: lo que se redondea a lo largo de la arista que sale de ella.
        return arrowAlong(corner, plane.xAxis, radius_->value());
    }
    if (key == "width") {
        return arrowAlong(corner, plane.xAxis, width_->value());
    }
    if (key == "depth") {
        return arrowAlong(corner, plane.yAxis, depth_->value());
    }
    if (key == "height") {
        return arrowAlong(corner, plane.normal, height);
    }
    return std::nullopt;
}

QString BoxTool::inputsText() const
{
    const Units& units = Units::instance();
    return QStringLiteral("%1 × %2 × %3").arg(units.format(width_->value()), units.format(depth_->value()),
                                              units.formatWithUnit(height_->value()));
}

CylinderTool::CylinderTool(document::Document& document, render::Viewport& viewport,
                           std::optional<document::FeatureId> editing)
    : FeatureTool(document, viewport, editing)
{
}

void CylinderTool::createFields(QFormLayout* form)
{
    const auto* original = dynamic_cast<const document::CylinderFeature*>(original_.get());
    createPlacement(form, placement_, original != nullptr ? &original->base : nullptr, original != nullptr ? original->x : 0.0,
                    original != nullptr ? original->y : 0.0);
    z_ = lengthBox(original != nullptr ? original->z : 0.0);
    field("z", z_);
    diameter_ = lengthBox(original != nullptr ? original->diameter : 20.0);
    diameter_->setMinimum(0.01);
    field("diameter", diameter_);
    height_ = lengthBox(original != nullptr ? original->height : 10.0);
    field("height", height_);
    symmetric_ = new QCheckBox(QStringLiteral("Alto repartido a los dos lados del plano"));
    symmetric_->setChecked(original != nullptr && original->symmetric);
    operation_ = combo({QStringLiteral("Cuerpo nuevo"), QStringLiteral("Unir"), QStringLiteral("Cortar"), QStringLiteral("Intersecar")});
    operation_->setCurrentIndex(original != nullptr ? primitiveIndex(original->operation) : before().bodies.empty() ? 0 : 1);
    form->addRow(QStringLiteral("Posición Z"), z_);
    form->addRow(QStringLiteral("Diámetro"), diameter_);
    form->addRow(QStringLiteral("Alto (±)"), height_);
    form->addRow(QString(), symmetric_);
    form->addRow(QStringLiteral("Operación"), operation_);
    for (QDoubleSpinBox* box : {z_, diameter_, height_}) {
        connect(box, &QDoubleSpinBox::valueChanged, this, [this] { updatePreview(); });
    }
    connect(symmetric_, &QCheckBox::toggled, this, [this] { updatePreview(); });
    connect(operation_, &QComboBox::currentIndexChanged, this, [this] { updatePreview(); });
}

void CylinderTool::selectionChanged(const render::ModelSelection& selection)
{
    if (placementSelection(placement_, selection)) {
        updatePreview();
    }
}

std::unique_ptr<document::Feature> CylinderTool::build() const
{
    auto f = std::make_unique<document::CylinderFeature>();
    if (original_) {
        f->setName(original_->name());
    }
    f->base = placementAttachment(placement_);
    f->x = placement_.x->value();
    f->y = placement_.y->value();
    f->z = z_->value();
    f->diameter = diameter_->value();
    f->height = height_->value();
    f->symmetric = symmetric_->isChecked();
    f->operation = primitiveOperation(operation_->currentIndex());
    return f;
}

std::optional<FeatureTool::Arrow> CylinderTool::dimensionArrow(const std::string& key) const
{
    if (key == "x" || key == "y") {
        return placementArrow(placement_, key);
    }
    const sketch::Plane plane = document::attachmentPlane(before(), placementAttachment(placement_));
    const sketch::Vec3 point = plane.toWorld({placement_.x->value(), placement_.y->value()});
    const double height = height_->value();
    if (key == "z") {
        return arrowAlong(point, plane.normal, z_->value());
    }
    const sketch::Vec3 center = moved(point, plane.normal, z_->value() - (symmetric_->isChecked() ? height / 2.0 : 0.0));
    if (key == "diameter") {
        return arrowAlong(moved(center, plane.xAxis, -diameter_->value() / 2.0), plane.xAxis, diameter_->value());
    }
    if (key == "height") {
        return arrowAlong(center, plane.normal, height);
    }
    return std::nullopt;
}

bool CylinderTool::removesMaterial() const
{
    return operation_ != nullptr && operation_->currentIndex() == 2;
}

QString CylinderTool::inputsText() const
{
    const Units& units = Units::instance();
    return QStringLiteral("Ø %1 × %2").arg(units.format(diameter_->value()), units.formatWithUnit(height_->value()));
}

} // namespace nuf::app
