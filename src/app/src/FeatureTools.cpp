#include "FeatureTools.hpp"

#include <nuf/geometry/Modeling.hpp>
#include <nuf/render/Viewport.hpp>

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace nuf::app {

namespace {

constexpr double kDegree = std::numbers::pi / 180.0;

QDoubleSpinBox* lengthBox(double value)
{
    auto* box = new QDoubleSpinBox();
    box->setRange(-100000.0, 100000.0);
    box->setDecimals(2);
    box->setSuffix(QStringLiteral(" mm"));
    box->setValue(value);
    box->setKeyboardTracking(false);
    return box;
}

QDoubleSpinBox* angleBox(double value, double limit)
{
    auto* box = new QDoubleSpinBox();
    box->setRange(-limit, limit);
    box->setDecimals(1);
    box->setSuffix(QStringLiteral(" °"));
    box->setValue(value);
    box->setKeyboardTracking(false);
    return box;
}

QComboBox* combo(std::initializer_list<QString> items)
{
    auto* box = new QComboBox();
    for (const QString& item : items) {
        box->addItem(item);
    }
    return box;
}

// Operación ↔ índice del desplegable («Unir», «Cortar», «Intersecar», «Cuerpo nuevo»).
int operationIndex(document::Operation op) { return int(op); }
document::Operation operationAt(int index) { return document::Operation(index); }

const document::SketchFeature* sketchFeature(const document::Document& doc, document::FeatureId id)
{
    const document::Feature* f = doc.findFeature(id);
    return f && f->kind() == document::FeatureKind::Sketch ? static_cast<const document::SketchFeature*>(f) : nullptr;
}

// El sketch tal como quedó calculado en «state» (con lo proyectado al día),
// o el guardado si no está.
const sketch::Sketch* sketchIn(const document::Document& doc, const document::ModelState& state, document::FeatureId id)
{
    if (const auto out = state.sketches.find(id); out != state.sketches.end()) {
        return out->second.sketch.get();
    }
    const document::SketchFeature* sk = sketchFeature(doc, id);
    return sk ? &sk->sketch() : nullptr;
}

// Centro aproximado de un perfil (media de su polígono).
sketch::Vec2 profileCenter(const sketch::Profile& profile)
{
    sketch::Vec2 c;
    for (const sketch::Vec2& p : profile.outer.polygon) {
        c.x += p.x;
        c.y += p.y;
    }
    const double n = std::max<double>(1.0, double(profile.outer.polygon.size()));
    return {c.x / n, c.y / n};
}

} // namespace

// --- Base ------------------------------------------------------------------------

FeatureTool::FeatureTool(document::Document& document, render::Viewport& viewport,
                         std::optional<document::FeatureId> editing)
    : document_(document)
    , viewport_(viewport)
    , editing_(editing)
{
    if (editing_) {
        index_ = document_.indexOf(*editing_).value_or(document_.marker());
        id_ = *editing_;
        original_ = document_.findFeature(*editing_)->clone();
    } else {
        index_ = document_.marker();
        id_ = document_.reserveId();
    }
}

FeatureTool::~FeatureTool()
{
    viewport_.clearToolPreview();
    viewport_.hideDistanceHandle();
    viewport_.setAdditiveSelection(false);
    if (panel_) {
        panel_->deleteLater();
    }
}

void FeatureTool::start()
{
    panel_ = new QWidget();
    auto* layout = new QVBoxLayout(panel_);
    auto* heading = new QLabel(editing_ ? QStringLiteral("Editar %1").arg(QString::fromStdString(original_->name()))
                                        : title());
    QFont bold = heading->font();
    bold.setBold(true);
    bold.setPointSizeF(bold.pointSizeF() * 1.15);
    heading->setFont(bold);
    layout->addWidget(heading);
    inputsLabel_ = new QLabel();
    inputsLabel_->setWordWrap(true);
    layout->addWidget(inputsLabel_);
    auto* form = new QFormLayout();
    layout->addLayout(form);
    updating_ = true;
    createFields(form);
    updating_ = false;
    statusLabel_ = new QLabel();
    statusLabel_->setWordWrap(true);
    layout->addWidget(statusLabel_);
    auto* buttons = new QHBoxLayout();
    okButton_ = new QPushButton(QStringLiteral("Aceptar"));
    okButton_->setDefault(true);
    auto* cancel = new QPushButton(QStringLiteral("Cancelar"));
    buttons->addStretch();
    buttons->addWidget(cancel);
    buttons->addWidget(okButton_);
    layout->addLayout(buttons);
    layout->addStretch();
    connect(okButton_, &QPushButton::clicked, this, &FeatureTool::accept);
    connect(cancel, &QPushButton::clicked, this, &FeatureTool::cancelled);

    viewport_.setAdditiveSelection(true);
    connect(&viewport_, &render::Viewport::modelSelectionChanged, this, [this] {
        selectionChanged(viewport_.modelSelection());
        updatePreview();
    });
    if (editing_) {
        // Se marcan en la vista los perfiles que usa.
        std::vector<document::ProfileRef> used;
        if (const auto* e = dynamic_cast<const document::ExtrudeFeature*>(original_.get())) {
            used = e->profiles;
        } else if (const auto* r = dynamic_cast<const document::RevolveFeature*>(original_.get())) {
            used = r->profiles;
        }
        std::vector<render::ModelSelection::Profile> regions;
        for (const document::ProfileRef& ref : used) {
            const sketch::Sketch* sk = sketchIn(document_, before(), ref.sketch);
            if (sk == nullptr) {
                continue;
            }
            const std::vector<sketch::Profile> found = sketch::findProfiles(*sk);
            if (const auto profile = document::resolveProfile(found, ref)) {
                const document::ProfileRef exact = document::profileRef(ref.sketch, *profile);
                for (std::size_t i = 0; i < found.size(); ++i) {
                    if (document::profileRef(ref.sketch, found[i]) == exact) {
                        regions.push_back({ref.sketch, int(i)});
                    }
                }
            }
        }
        viewport_.selectProfiles(regions);
    } else {
        // Lo que ya estaba seleccionado son las entradas.
        selectionChanged(viewport_.modelSelection());
    }
    updatePreview();
}

void FeatureTool::accept()
{
    if (build()) {
        emit accepted();
    }
}

std::vector<document::ProfileRef> FeatureTool::profilesOf(const render::ModelSelection& selection) const
{
    std::vector<document::ProfileRef> refs;
    for (const render::ModelSelection::Profile& p : selection.profiles) {
        const sketch::Sketch* sk = sketchIn(document_, before(), p.sketch);
        if (sk == nullptr) {
            continue;
        }
        const std::vector<sketch::Profile> found = sketch::findProfiles(*sk);
        if (p.index >= 0 && p.index < int(found.size())) {
            refs.push_back(document::profileRef(p.sketch, found[std::size_t(p.index)]));
        }
    }
    return refs;
}

void FeatureTool::updatePreview()
{
    if (updating_ || !panel_) {
        return;
    }
    inputsLabel_->setText(inputsText());
    std::unique_ptr<document::Feature> feature = build();
    if (!feature) {
        viewport_.showBodies(before().bodies);
        viewport_.clearToolPreview();
        viewport_.hideDistanceHandle();
        statusLabel_->clear();
        okButton_->setEnabled(false);
        return;
    }
    feature->setId(id_);
    const document::Preview result = document::preview(document_, *feature, index_);
    viewport_.showBodies(result.state.bodies);

    // Lo que se quita, en rojo translúcido encima del resultado.
    viewport_.clearToolPreview();
    if (removesMaterial() && result.status.health != document::Health::Error) {
        try {
            geometry::Shape tool;
            if (const auto* e = dynamic_cast<const document::ExtrudeFeature*>(feature.get())) {
                tool = e->tool(before());
            } else if (const auto* r = dynamic_cast<const document::RevolveFeature*>(feature.get())) {
                tool = r->tool(before());
            }
            viewport_.showToolPreview(tool, true);
        } catch (const std::exception&) {
            // Ya lo informa el estado.
        }
    }

    const QString text = QString::fromStdString(result.status.message);
    switch (result.status.health) {
    case document::Health::Error:
        statusLabel_->setText(QStringLiteral("<span style='color:#E5484D'>%1</span>").arg(text.toHtmlEscaped()));
        okButton_->setEnabled(false);
        break;
    case document::Health::Warning:
        statusLabel_->setText(QStringLiteral("<span style='color:#F2A34B'>%1</span>").arg(text.toHtmlEscaped()));
        okButton_->setEnabled(true);
        break;
    default:
        statusLabel_->clear();
        okButton_->setEnabled(true);
        break;
    }
    afterPreview(before());
}

// --- Extruir -----------------------------------------------------------------------

ExtrudeTool::ExtrudeTool(document::Document& document, render::Viewport& viewport,
                         std::optional<document::FeatureId> editing)
    : FeatureTool(document, viewport, editing)
{
    if (const auto* e = dynamic_cast<const document::ExtrudeFeature*>(original_.get())) {
        profiles_ = e->profiles;
        face_ = e->face;
        toFace1_ = e->spec.side1.face;
        toFace2_ = e->spec.side2.face;
        operationTouched_ = true;
    }
}

void ExtrudeTool::createFields(QFormLayout* form)
{
    form_ = form;
    direction_ = combo({QStringLiteral("Un lado"), QStringLiteral("Simétrica"), QStringLiteral("Dos lados")});
    const auto extentCombo = [] {
        return combo({QStringLiteral("Distancia"), QStringLiteral("A través de todo"), QStringLiteral("Hasta una cara")});
    };
    extent1_ = extentCombo();
    distance1_ = lengthBox(1.0);
    taper1_ = angleBox(0.0, 89.0);
    target1_ = new QPushButton(QStringLiteral("Elegir cara de destino"));
    extent2_ = extentCombo();
    distance2_ = lengthBox(1.0);
    distance2_->setMinimum(0.0);
    taper2_ = angleBox(0.0, 89.0);
    target2_ = new QPushButton(QStringLiteral("Elegir cara de destino"));
    offset_ = lengthBox(0.0);
    operation_ = combo({QStringLiteral("Unir"), QStringLiteral("Cortar"), QStringLiteral("Intersecar"),
                        QStringLiteral("Cuerpo nuevo")});

    form->addRow(QStringLiteral("Dirección"), direction_);
    form->addRow(QStringLiteral("Extensión"), extent1_);
    form->addRow(QStringLiteral("Distancia"), distance1_);
    form->addRow(QString(), target1_);
    form->addRow(QStringLiteral("Ángulo"), taper1_);
    form->addRow(QStringLiteral("Lado 2"), extent2_);
    form->addRow(QStringLiteral("Distancia 2"), distance2_);
    form->addRow(QString(), target2_);
    form->addRow(QStringLiteral("Ángulo 2"), taper2_);
    form->addRow(QStringLiteral("Desfase inicial"), offset_);
    form->addRow(QStringLiteral("Operación"), operation_);

    if (const auto* e = dynamic_cast<const document::ExtrudeFeature*>(original_.get())) {
        direction_->setCurrentIndex(int(e->spec.direction));
        extent1_->setCurrentIndex(int(e->spec.side1.extent));
        distance1_->setValue(e->spec.side1.distance);
        taper1_->setValue(e->spec.side1.taper / kDegree);
        extent2_->setCurrentIndex(int(e->spec.side2.extent));
        distance2_->setValue(e->spec.side2.distance);
        taper2_->setValue(e->spec.side2.taper / kDegree);
        offset_->setValue(e->spec.startOffset);
        operation_->setCurrentIndex(operationIndex(e->operation));
    } else {
        operation_->setCurrentIndex(operationIndex(before().bodies.empty() ? document::Operation::NewBody
                                                                           : document::Operation::Join));
    }

    for (QDoubleSpinBox* box : {distance1_, taper1_, distance2_, taper2_, offset_}) {
        connect(box, &QDoubleSpinBox::valueChanged, this, [this] {
            const int sign = distance1_->value() < 0.0 ? -1 : 1;
            if (sign != lastSign_) {
                lastSign_ = sign;
                chooseOperation();
            }
            updatePreview();
        });
    }
    for (QComboBox* box : {direction_, extent1_, extent2_}) {
        connect(box, &QComboBox::currentIndexChanged, this, [this] {
            updateVisibility();
            updatePreview();
        });
    }
    connect(operation_, &QComboBox::activated, this, [this] { operationTouched_ = true; });
    connect(operation_, &QComboBox::currentIndexChanged, this, [this] { updatePreview(); });
    for (const auto& [button, side] : {std::pair{target1_, 1}, std::pair{target2_, 2}}) {
        button->setCheckable(true);
        connect(button, &QPushButton::toggled, this, [this, side = side](bool on) {
            pickingTarget_ = on ? side : 0;
            (side == 1 ? target2_ : target1_)->setChecked(false);
            if (on) {
                emit message(QStringLiteral("Haz clic en la cara plana hasta la que llega la extrusión"));
            }
        });
    }
    // Se escribe la distancia directamente (como el valor junto al cursor).
    connect(&viewport_, &render::Viewport::distanceHandleDragged, this, [this](double d) {
        distance1_->setValue(std::round(d * 10.0) / 10.0);
    });
    lastSign_ = distance1_->value() < 0.0 ? -1 : 1;
    updateVisibility();
    distance1_->setFocus();
    distance1_->selectAll();
}

void ExtrudeTool::updateVisibility()
{
    const bool two = direction_->currentIndex() == int(geometry::ExtrudeDirection::Two);
    const auto extent1 = geometry::Extent(extent1_->currentIndex());
    const auto extent2 = geometry::Extent(extent2_->currentIndex());
    const auto show = [&](QWidget* field, bool visible) { form_->setRowVisible(field, visible); };
    show(distance1_, extent1 == geometry::Extent::Distance);
    show(target1_, extent1 == geometry::Extent::ToFace);
    show(extent2_, two);
    show(distance2_, two && extent2 == geometry::Extent::Distance);
    show(target2_, two && extent2 == geometry::Extent::ToFace);
    show(taper2_, two);
    target1_->setText(toFace1_.empty() ? QStringLiteral("Elegir cara de destino") : QStringLiteral("Cara de destino ✓"));
    target2_->setText(toFace2_.empty() ? QStringLiteral("Elegir cara de destino") : QStringLiteral("Cara de destino ✓"));
}

void ExtrudeTool::selectionChanged(const render::ModelSelection& selection)
{
    // Caras que no son de destino ni de esta misma operación (en la vista
    // previa se ven las caras que crea).
    const std::string own = "f" + std::to_string(id_) + ":";
    std::vector<geometry::ElementRef> faces;
    for (const render::ModelSelection::Face& f : selection.faces) {
        if (f.ref.name.find(own) == std::string::npos && f.ref != toFace1_ && f.ref != toFace2_) {
            faces.push_back(f.ref);
        }
    }
    std::optional<geometry::ElementRef> added;
    for (const geometry::ElementRef& f : faces) {
        if (std::ranges::find(selectedFaces_, f) == selectedFaces_.end()) {
            added = f;
        }
    }
    selectedFaces_ = faces;

    if (pickingTarget_ != 0 && added) {
        (pickingTarget_ == 1 ? toFace1_ : toFace2_) = *added;
        (pickingTarget_ == 1 ? target1_ : target2_)->setChecked(false);
        std::erase(selectedFaces_, *added);
        faces = selectedFaces_;
        updateVisibility();
    }

    const bool hadInputs = !profiles_.empty() || !face_.empty();
    const std::vector<document::ProfileRef> profiles = profilesOf(selection);
    if (!profiles.empty()) {
        profiles_ = profiles;
        face_ = {};
    } else if (!faces.empty()) {
        // Empujar/tirar una cara plana.
        const auto hit = before().findFace(faces.front());
        if (hit && before().bodies[hit->body].shape.facePlane(hit->face)) {
            face_ = faces.front();
            profiles_.clear();
        }
    }
    if (!hadInputs && (!profiles_.empty() || !face_.empty())) {
        chooseOperation();
    }
}

void ExtrudeTool::chooseOperation()
{
    if (operationTouched_ || updating_) {
        return;
    }
    document::Operation op = document::Operation::Join;
    if (before().bodies.empty()) {
        op = document::Operation::NewBody;
    } else if (auto feature = build()) {
        // Hacia dentro de un cuerpo: cortar; hacia fuera: unir.
        try {
            feature->setId(id_);
            const geometry::Shape tool = static_cast<document::ExtrudeFeature&>(*feature).tool(before());
            for (const document::Body& body : before().bodies) {
                const geometry::Shape common = geometry::boolean(geometry::BooleanOp::Common, body.shape, tool);
                if (!common.isNull() && common.volume() > 1e-6) {
                    op = document::Operation::Cut;
                    break;
                }
            }
        } catch (const std::exception&) {
            // Sin sólido válido, se queda en unir.
        }
    }
    const bool wasUpdating = updating_;
    updating_ = true;
    operation_->setCurrentIndex(operationIndex(op));
    updating_ = wasUpdating;
}

std::unique_ptr<document::Feature> ExtrudeTool::build() const
{
    if (profiles_.empty() && face_.empty()) {
        return nullptr;
    }
    auto f = std::make_unique<document::ExtrudeFeature>();
    if (original_) {
        f->setName(original_->name());
    }
    f->profiles = profiles_;
    f->face = face_;
    f->spec.direction = geometry::ExtrudeDirection(direction_->currentIndex());
    f->spec.side1.extent = geometry::Extent(extent1_->currentIndex());
    f->spec.side1.distance = distance1_->value();
    f->spec.side1.taper = taper1_->value() * kDegree;
    f->spec.side1.face = toFace1_;
    f->spec.side2.extent = geometry::Extent(extent2_->currentIndex());
    f->spec.side2.distance = distance2_->value();
    f->spec.side2.taper = taper2_->value() * kDegree;
    f->spec.side2.face = toFace2_;
    f->spec.startOffset = offset_->value();
    f->operation = operationAt(operation_->currentIndex());
    return f;
}

void ExtrudeTool::afterPreview(const document::ModelState& state)
{
    // La flecha sale del centro del primer perfil (o de la cara), a lo largo
    // de la normal, con la punta a la distancia actual.
    if (geometry::Extent(extent1_->currentIndex()) != geometry::Extent::Distance) {
        viewport_.hideDistanceHandle();
        return;
    }
    std::optional<std::pair<sketch::Vec3, sketch::Vec3>> base;
    if (!profiles_.empty()) {
        const auto out = state.sketches.find(profiles_.front().sketch);
        if (out != state.sketches.end()) {
            if (const auto profile = document::resolveProfile(sketch::findProfiles(*out->second.sketch),
                                                              profiles_.front())) {
                base = std::pair{out->second.plane.toWorld(profileCenter(*profile)), out->second.plane.normal};
            }
        }
    } else if (const auto hit = state.findFace(face_)) {
        const geometry::Shape& shape = state.bodies[hit->body].shape;
        if (const auto plane = shape.facePlane(hit->face)) {
            base = std::pair{shape.faceCenter(hit->face), plane->normal};
        }
    }
    if (!base) {
        viewport_.hideDistanceHandle();
        return;
    }
    const sketch::Vec3 n = base->second;
    const double offset = offset_->value();
    const sketch::Vec3 origin{base->first.x + n.x * offset, base->first.y + n.y * offset, base->first.z + n.z * offset};
    viewport_.showDistanceHandle(origin, n, distance1_->value());
}

QString ExtrudeTool::inputsText() const
{
    if (!profiles_.empty()) {
        return profiles_.size() == 1 ? QStringLiteral("1 perfil") : QStringLiteral("%1 perfiles").arg(profiles_.size());
    }
    if (!face_.empty()) {
        return QStringLiteral("1 cara");
    }
    return QStringLiteral("Elige perfiles de un sketch o una cara plana");
}

bool ExtrudeTool::removesMaterial() const
{
    const auto op = operationAt(operation_->currentIndex());
    return op == document::Operation::Cut || op == document::Operation::Intersect;
}

// --- Revolución --------------------------------------------------------------------

RevolveTool::RevolveTool(document::Document& document, render::Viewport& viewport,
                         std::optional<document::FeatureId> editing)
    : FeatureTool(document, viewport, editing)
{
    if (const auto* r = dynamic_cast<const document::RevolveFeature*>(original_.get())) {
        profiles_ = r->profiles;
        axis_ = r->axis;
    }
}

void RevolveTool::createFields(QFormLayout* form)
{
    form_ = form;
    axisKind_ = combo({QStringLiteral("Eje X"), QStringLiteral("Eje Y"), QStringLiteral("Eje Z"),
                       QStringLiteral("Línea o arista elegida")});
    axisLabel_ = new QLabel(QStringLiteral("Haz clic en una línea del sketch o en una arista recta"));
    axisLabel_->setWordWrap(true);
    extent_ = combo({QStringLiteral("Ángulo"), QStringLiteral("Vuelta completa")});
    direction_ = combo({QStringLiteral("Un lado"), QStringLiteral("Simétrica"), QStringLiteral("Dos lados")});
    angle1_ = angleBox(180.0, 360.0);
    angle2_ = angleBox(90.0, 360.0);
    angle2_->setMinimum(0.0);
    operation_ = combo({QStringLiteral("Unir"), QStringLiteral("Cortar"), QStringLiteral("Intersecar"),
                        QStringLiteral("Cuerpo nuevo")});
    form->addRow(QStringLiteral("Eje"), axisKind_);
    form->addRow(QString(), axisLabel_);
    form->addRow(QStringLiteral("Extensión"), extent_);
    form->addRow(QStringLiteral("Dirección"), direction_);
    form->addRow(QStringLiteral("Ángulo"), angle1_);
    form->addRow(QStringLiteral("Ángulo 2"), angle2_);
    form->addRow(QStringLiteral("Operación"), operation_);

    if (const auto* r = dynamic_cast<const document::RevolveFeature*>(original_.get())) {
        axisKind_->setCurrentIndex(std::min(int(r->axis.kind), 3));
        extent_->setCurrentIndex(int(r->spec.extent));
        direction_->setCurrentIndex(int(r->spec.direction));
        angle1_->setValue(r->spec.angle1 / kDegree);
        angle2_->setValue(r->spec.angle2 / kDegree);
        operation_->setCurrentIndex(operationIndex(r->operation));
    } else {
        axisKind_->setCurrentIndex(2);
        extent_->setCurrentIndex(1);  // vuelta completa
        operation_->setCurrentIndex(operationIndex(before().bodies.empty() ? document::Operation::NewBody
                                                                           : document::Operation::Join));
    }
    for (QComboBox* box : {axisKind_, extent_, direction_, operation_}) {
        connect(box, &QComboBox::currentIndexChanged, this, [this] {
            updateVisibility();
            updatePreview();
        });
    }
    for (QDoubleSpinBox* box : {angle1_, angle2_}) {
        connect(box, &QDoubleSpinBox::valueChanged, this, [this] { updatePreview(); });
    }
    updateVisibility();
}

void RevolveTool::updateVisibility()
{
    const bool full = extent_->currentIndex() == int(geometry::RevolveExtent::Full);
    form_->setRowVisible(axisLabel_, axisKind_->currentIndex() == 3);
    form_->setRowVisible(direction_, !full);
    form_->setRowVisible(angle1_, !full);
    form_->setRowVisible(angle2_, !full && direction_->currentIndex() == int(geometry::ExtrudeDirection::Two));
    if (axisKind_->currentIndex() == 3) {
        const bool chosen = axis_.kind == document::AxisRef::Kind::SketchLine || axis_.kind == document::AxisRef::Kind::Edge;
        axisLabel_->setText(chosen ? QStringLiteral("Eje elegido ✓")
                                   : QStringLiteral("Haz clic en una línea del sketch o en una arista recta"));
    }
}

void RevolveTool::selectionChanged(const render::ModelSelection& selection)
{
    const std::vector<document::ProfileRef> profiles = profilesOf(selection);
    if (!profiles.empty()) {
        profiles_ = profiles;
    }
    // Una línea del sketch o una arista recta elegidas son el eje.
    for (const render::ModelSelection::Curve& c : selection.curves) {
        const sketch::Sketch* sk = sketchIn(document_, before(), c.sketch);
        const sketch::Entity* e = sk ? sk->entity(c.entity) : nullptr;
        if (e && std::holds_alternative<sketch::Line>(e->geometry)) {
            axis_ = {document::AxisRef::Kind::SketchLine, c.sketch, c.entity, {}};
            axisKind_->setCurrentIndex(3);
        }
    }
    for (const render::ModelSelection::Edge& edge : selection.edges) {
        const auto hit = before().findEdge(edge.ref);
        if (hit && before().bodies[hit->body].shape.edgeLine(hit->edge)) {
            axis_ = {document::AxisRef::Kind::Edge, 0, 0, edge.ref};
            axisKind_->setCurrentIndex(3);
        }
    }
    updateVisibility();
}

std::unique_ptr<document::Feature> RevolveTool::build() const
{
    if (profiles_.empty()) {
        return nullptr;
    }
    auto f = std::make_unique<document::RevolveFeature>();
    if (original_) {
        f->setName(original_->name());
    }
    f->profiles = profiles_;
    switch (axisKind_->currentIndex()) {
    case 0:
        f->axis = {document::AxisRef::Kind::OriginX, 0, 0, {}};
        break;
    case 1:
        f->axis = {document::AxisRef::Kind::OriginY, 0, 0, {}};
        break;
    case 2:
        f->axis = {document::AxisRef::Kind::OriginZ, 0, 0, {}};
        break;
    default:
        if (axis_.kind != document::AxisRef::Kind::SketchLine && axis_.kind != document::AxisRef::Kind::Edge) {
            return nullptr;
        }
        f->axis = axis_;
        break;
    }
    f->spec.extent = geometry::RevolveExtent(extent_->currentIndex());
    f->spec.direction = geometry::ExtrudeDirection(direction_->currentIndex());
    f->spec.angle1 = angle1_->value() * kDegree;
    f->spec.angle2 = angle2_->value() * kDegree;
    f->operation = operationAt(operation_->currentIndex());
    return f;
}

QString RevolveTool::inputsText() const
{
    if (profiles_.empty()) {
        return QStringLiteral("Elige perfiles de un sketch y el eje");
    }
    return profiles_.size() == 1 ? QStringLiteral("1 perfil") : QStringLiteral("%1 perfiles").arg(profiles_.size());
}

bool RevolveTool::removesMaterial() const
{
    const auto op = operationAt(operation_->currentIndex());
    return op == document::Operation::Cut || op == document::Operation::Intersect;
}

} // namespace nuf::app
