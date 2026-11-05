#include "FeatureTools.hpp"

#include "ExpressionSpinBox.hpp"

#include <nuf/geometry/Modeling.hpp>
#include <nuf/render/Viewport.hpp>

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace nuf::app {

namespace {

constexpr double kDegree = std::numbers::pi / 180.0;

QDoubleSpinBox* lengthBox(double value)
{
    auto* box = new ExpressionSpinBox();
    box->setRange(-100000.0, 100000.0);
    box->setDecimals(2);
    box->setSuffix(QStringLiteral(" mm"));
    box->setValue(value);
    box->setKeyboardTracking(false);
    return box;
}

QDoubleSpinBox* angleBox(double value, double limit)
{
    auto* box = new ExpressionSpinBox();
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
    // Al editar, vuelven las expresiones que tenía la operación.
    if (original_) {
        for (const auto& [key, box] : fields_) {
            const auto it = original_->expressions.find(key);
            if (auto* e = dynamic_cast<ExpressionSpinBox*>(box); e != nullptr && it != original_->expressions.end()) {
                e->setExpression(QString::fromStdString(it->second));
            }
        }
    }
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

std::unique_ptr<document::Feature> FeatureTool::make() const
{
    std::unique_ptr<document::Feature> feature = build();
    if (feature) {
        for (const auto& [key, box] : fields_) {
            if (const auto* e = dynamic_cast<const ExpressionSpinBox*>(box); e != nullptr && !e->expression().isEmpty()) {
                feature->expressions[key] = e->expression().toStdString();
            }
        }
    }
    return feature;
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

namespace {

// Los planos de construcción que hay en el estado, con su nombre (el de la
// operación en vista previa, si la hay, aunque aún no esté en el documento).
void showPlanes(render::Viewport& viewport, const document::Document& doc, const document::ModelState& state,
                const document::Feature* preview)
{
    std::vector<render::Viewport::ConstructionPlaneView> planes;
    for (const auto& [id, plane] : state.planes) {
        const document::Feature* f = preview != nullptr && preview->id() == id ? preview : doc.findFeature(id);
        planes.push_back({id, f ? QString::fromStdString(f->name()) : QString(), plane});
    }
    viewport.showConstructionPlanes(planes);
    std::vector<render::Viewport::ConstructionAxisView> axes;
    for (const auto& [id, line] : state.axes) {
        const document::Feature* f = preview != nullptr && preview->id() == id ? preview : doc.findFeature(id);
        axes.push_back({id, f ? QString::fromStdString(f->name()) : QString(), line.point, line.direction});
    }
    viewport.showConstructionAxes(axes);
}

} // namespace

void FeatureTool::updatePreview()
{
    if (updating_ || !panel_) {
        return;
    }
    inputsLabel_->setText(inputsText());
    std::unique_ptr<document::Feature> feature = make();
    if (!feature) {
        viewport_.showBodies(before().bodies);
        showPlanes(viewport_, document_, before(), nullptr);
        viewport_.clearToolPreview();
        viewport_.hideDistanceHandle();
        statusLabel_->clear();
        okButton_->setEnabled(false);
        return;
    }
    feature->setId(id_);
    const document::Preview result = document::preview(document_, *feature, index_);
    viewport_.showBodies(result.state.bodies);
    showPlanes(viewport_, document_, result.state, feature.get());

    // Lo que se quita, en rojo translúcido encima del resultado.
    viewport_.clearToolPreview();
    if (removesMaterial() && result.status.health != document::Health::Error) {
        try {
            geometry::Shape tool;
            if (const auto* e = dynamic_cast<const document::ExtrudeFeature*>(feature.get())) {
                tool = e->tool(before());
            } else if (const auto* r = dynamic_cast<const document::RevolveFeature*>(feature.get())) {
                tool = r->tool(before());
            } else if (const auto* h = dynamic_cast<const document::HoleFeature*>(feature.get())) {
                tool = h->tool(before());
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
    field("distance", distance1_);
    taper1_ = angleBox(0.0, 89.0);
    target1_ = new QPushButton(QStringLiteral("Elegir cara de destino"));
    extent2_ = extentCombo();
    distance2_ = lengthBox(1.0);
    field("distance2", distance2_);
    distance2_->setMinimum(0.0);
    taper2_ = angleBox(0.0, 89.0);
    target2_ = new QPushButton(QStringLiteral("Elegir cara de destino"));
    offset_ = lengthBox(0.0);
    field("startOffset", offset_);
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
    // Los movimientos del ratón llegan más rápido de lo que se rehace la vista
    // previa: se aplica solo el último valor por cuadro, no uno por evento.
    dragTimer_ = new QTimer(this);
    dragTimer_->setSingleShot(true);
    dragTimer_->setInterval(12);
    connect(dragTimer_, &QTimer::timeout, this, [this] {
        distance1_->setValue(pendingDistance_);
    });
    connect(&viewport_, &render::Viewport::distanceHandleDragged, this, [this](double d) {
        pendingDistance_ = std::round(d * 10.0) / 10.0;
        if (!dragTimer_->isActive()) {
            dragTimer_->start();
        }
    });
    connect(&viewport_, &render::Viewport::distanceHandleReleased, this, [this] {
        // Lo que quedó pendiente al soltar se aplica de inmediato.
        if (dragTimer_->isActive()) {
            dragTimer_->stop();
            distance1_->setValue(pendingDistance_);
        }
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
    for (const auto& [id, line] : before().axes) {
        const document::Feature* f = document_.findFeature(id);
        axisKind_->addItem(f ? QString::fromStdString(f->name()) : QStringLiteral("Eje"));
        axisIds_.push_back(id);
    }
    axisLabel_ = new QLabel(QStringLiteral("Haz clic en una línea del sketch o en una arista recta"));
    axisLabel_->setWordWrap(true);
    extent_ = combo({QStringLiteral("Ángulo"), QStringLiteral("Vuelta completa")});
    direction_ = combo({QStringLiteral("Un lado"), QStringLiteral("Simétrica"), QStringLiteral("Dos lados")});
    angle1_ = angleBox(180.0, 360.0);
    field("angle", angle1_);
    angle2_ = angleBox(90.0, 360.0);
    field("angle2", angle2_);
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
        int at = std::min(int(r->axis.kind), 3);
        if (r->axis.kind == document::AxisRef::Kind::Construction) {
            const auto it = std::ranges::find(axisIds_, r->axis.reference);
            at = it != axisIds_.end() ? 4 + int(it - axisIds_.begin()) : 3;
        }
        axisKind_->setCurrentIndex(at);
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
        if (axisKind_->currentIndex() >= 4) {
            f->axis = {document::AxisRef::Kind::Construction, 0, 0, {}, axisIds_[std::size_t(axisKind_->currentIndex() - 4)]};
            break;
        }
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

// --- Redondeo y chaflán ------------------------------------------------------------

EdgeTool::EdgeTool(document::Document& document, render::Viewport& viewport,
                   std::optional<document::FeatureId> editing, Style style)
    : FeatureTool(document, viewport, editing)
    , style_(style)
{
    if (const auto* e = dynamic_cast<const document::EdgeFeature*>(original_.get())) {
        style_ = e->kind() == document::FeatureKind::Chamfer ? Style::Chamfer : Style::Fillet;
        edges_ = e->edges;
    }
}

QString EdgeTool::title() const
{
    return style_ == Style::Fillet ? QStringLiteral("Redondeo") : QStringLiteral("Chaflán");
}

void EdgeTool::createFields(QFormLayout* form)
{
    size_ = lengthBox(1.0);
    field("size", size_);
    size_->setMinimum(0.01);
    if (const auto* e = dynamic_cast<const document::EdgeFeature*>(original_.get())) {
        size_->setValue(e->size);
    }
    form->addRow(style_ == Style::Fillet ? QStringLiteral("Radio") : QStringLiteral("Distancia"), size_);
    connect(size_, &QDoubleSpinBox::valueChanged, this, [this] { updatePreview(); });
    auto* clear = new QPushButton(QStringLiteral("Quitar selección"));
    form->addRow(QString(), clear);
    connect(clear, &QPushButton::clicked, this, [this] {
        edges_.clear();
        viewport_.clearModelSelection();
        updatePreview();
    });
}

void EdgeTool::selectionChanged(const render::ModelSelection& selection)
{
    // La lista de aristas es de la herramienta, no del visor (que redibuja el
    // resultado en cada cambio y pierde su selección): cada clic en una arista
    // la agrega o la quita, y el visor se deja sin selección.
    for (const render::ModelSelection::Edge& edge : selection.edges) {
        if (!before().findEdge(edge.ref)) {
            continue;  // una arista del resultado de la vista previa, no del cuerpo
        }
        const auto it = std::ranges::find(edges_, edge.ref);
        if (it == edges_.end()) {
            edges_.push_back(edge.ref);
        } else {
            edges_.erase(it);
        }
    }
    if (!selection.empty()) {
        viewport_.clearModelSelection();
    }
}

std::unique_ptr<document::Feature> EdgeTool::build() const
{
    if (edges_.empty()) {
        return nullptr;
    }
    std::unique_ptr<document::EdgeFeature> f;
    if (style_ == Style::Fillet) {
        f = std::make_unique<document::FilletFeature>();
    } else {
        f = std::make_unique<document::ChamferFeature>();
    }
    if (original_) {
        f->setName(original_->name());
    }
    f->edges = edges_;
    f->size = size_->value();
    return f;
}

void EdgeTool::afterPreview(const document::ModelState& before)
{
    // Las aristas elegidas, resaltadas sobre el cuerpo de antes.
    std::optional<std::size_t> body;
    std::vector<int> indices;
    for (const geometry::ElementRef& ref : edges_) {
        if (const auto hit = before.findEdge(ref); hit && (!body || *body == hit->body)) {
            body = hit->body;
            indices.push_back(hit->edge);
        }
    }
    if (body) {
        viewport_.showToolEdges(before.bodies[*body].shape, indices);
    }
}

QString EdgeTool::inputsText() const
{
    if (edges_.empty()) {
        return QStringLiteral("Elige las aristas del cuerpo (varias con clics sucesivos)");
    }
    return edges_.size() == 1 ? QStringLiteral("1 arista") : QStringLiteral("%1 aristas").arg(edges_.size());
}

// --- Vaciado -----------------------------------------------------------------------

ShellTool::ShellTool(document::Document& document, render::Viewport& viewport,
                     std::optional<document::FeatureId> editing)
    : FeatureTool(document, viewport, editing)
{
    if (const auto* s = dynamic_cast<const document::ShellFeature*>(original_.get())) {
        faces_ = s->faces;
    }
}

void ShellTool::createFields(QFormLayout* form)
{
    thickness_ = lengthBox(1.0);
    field("thickness", thickness_);
    thickness_->setMinimum(0.01);
    direction_ = combo({QStringLiteral("Hacia dentro"), QStringLiteral("Hacia fuera")});
    if (const auto* s = dynamic_cast<const document::ShellFeature*>(original_.get())) {
        thickness_->setValue(s->thickness);
        direction_->setCurrentIndex(s->outward ? 1 : 0);
    }
    form->addRow(QStringLiteral("Espesor"), thickness_);
    form->addRow(QStringLiteral("Dirección"), direction_);
    auto* clear = new QPushButton(QStringLiteral("Quitar selección"));
    form->addRow(QString(), clear);
    connect(thickness_, &QDoubleSpinBox::valueChanged, this, [this] { updatePreview(); });
    connect(direction_, &QComboBox::currentIndexChanged, this, [this] { updatePreview(); });
    connect(clear, &QPushButton::clicked, this, [this] {
        faces_.clear();
        viewport_.clearModelSelection();
        updatePreview();
    });
}

void ShellTool::selectionChanged(const render::ModelSelection& selection)
{
    // Igual que en EdgeTool: la lista es de la herramienta y cada clic agrega o quita.
    for (const render::ModelSelection::Face& face : selection.faces) {
        if (!before().findFace(face.ref)) {
            continue;  // una cara del resultado de la vista previa, no del cuerpo
        }
        const auto it = std::ranges::find(faces_, face.ref);
        if (it == faces_.end()) {
            faces_.push_back(face.ref);
        } else {
            faces_.erase(it);
        }
    }
    if (!selection.empty()) {
        viewport_.clearModelSelection();
    }
}

void ShellTool::afterPreview(const document::ModelState& before)
{
    std::optional<std::size_t> body;
    std::vector<int> indices;
    for (const geometry::ElementRef& ref : faces_) {
        if (const auto hit = before.findFace(ref); hit && (!body || *body == hit->body)) {
            body = hit->body;
            indices.push_back(hit->face);
        }
    }
    if (body) {
        viewport_.showToolFaces(before.bodies[*body].shape, indices);
    }
}

std::unique_ptr<document::Feature> ShellTool::build() const
{
    if (faces_.empty()) {
        return nullptr;
    }
    auto f = std::make_unique<document::ShellFeature>();
    if (original_) {
        f->setName(original_->name());
    }
    f->faces = faces_;
    f->thickness = thickness_->value();
    f->outward = direction_->currentIndex() == 1;
    return f;
}

QString ShellTool::inputsText() const
{
    if (faces_.empty()) {
        return QStringLiteral("Elige las caras que quedan abiertas (varias con clics sucesivos)");
    }
    return faces_.size() == 1 ? QStringLiteral("1 cara abierta") : QStringLiteral("%1 caras abiertas").arg(faces_.size());
}

// --- Agujeros ----------------------------------------------------------------------

HoleTool::HoleTool(document::Document& document, render::Viewport& viewport,
                   std::optional<document::FeatureId> editing)
    : FeatureTool(document, viewport, editing)
{
    if (const auto* h = dynamic_cast<const document::HoleFeature*>(original_.get())) {
        sketch_ = h->sketch;
    }
}

void HoleTool::createFields(QFormLayout* form)
{
    form_ = form;
    sketchBox_ = new QComboBox();
    // Los sketches ya calculados con círculos o puntos donde poner agujeros.
    for (const auto& [id, out] : before().sketches) {
        const document::Feature* f = document_.findFeature(id);
        const bool marks = std::ranges::any_of(out.sketch->entities(), [](const sketch::Entity& e) {
            return !e.reference && (std::holds_alternative<sketch::Point>(e.geometry)
                                    || (std::holds_alternative<sketch::Circle>(e.geometry) && !e.construction));
        });
        if (f && marks) {
            sketchBox_->addItem(QString::fromStdString(f->name()), qulonglong(id));
        }
    }
    if (sketch_ == 0 && sketchBox_->count() > 0) {
        sketch_ = document::FeatureId(sketchBox_->itemData(sketchBox_->count() - 1).toULongLong());  // el último
    }
    if (const int at = sketchBox_->findData(qulonglong(sketch_)); at >= 0) {
        sketchBox_->setCurrentIndex(at);
    }
    kind_ = combo({QStringLiteral("Simple"), QStringLiteral("Abocardado"), QStringLiteral("Avellanado")});
    diameter_ = lengthBox(5.0);
    field("diameter", diameter_);
    diameter_->setMinimum(0.01);
    throughAll_ = new QCheckBox(QStringLiteral("Atravesar todo"));
    depth_ = lengthBox(10.0);
    field("depth", depth_);
    depth_->setMinimum(0.01);
    headDiameter_ = lengthBox(10.0);
    field("headDiameter", headDiameter_);
    headDiameter_->setMinimum(0.01);
    headDepth_ = lengthBox(3.0);
    field("headDepth", headDepth_);
    headDepth_->setMinimum(0.01);
    headAngle_ = angleBox(90.0, 179.0);
    field("headAngle", headAngle_);
    headAngle_->setMinimum(1.0);
    flip_ = new QCheckBox(QStringLiteral("Al otro lado del plano"));
    if (const auto* h = dynamic_cast<const document::HoleFeature*>(original_.get())) {
        kind_->setCurrentIndex(int(h->spec.kind));
        diameter_->setValue(h->spec.diameter);
        throughAll_->setChecked(h->spec.throughAll);
        depth_->setValue(h->spec.depth);
        headDiameter_->setValue(h->spec.headDiameter);
        headDepth_->setValue(h->spec.headDepth);
        headAngle_->setValue(h->spec.headAngle / kDegree);
        flip_->setChecked(h->flip);
    }
    form->addRow(QStringLiteral("Sketch"), sketchBox_);
    form->addRow(QStringLiteral("Tipo"), kind_);
    form->addRow(QStringLiteral("Diámetro"), diameter_);
    form->addRow(QString(), throughAll_);
    form->addRow(QStringLiteral("Profundidad"), depth_);
    form->addRow(QStringLiteral("Diámetro de la cabeza"), headDiameter_);
    form->addRow(QStringLiteral("Profundidad de la cabeza"), headDepth_);
    form->addRow(QStringLiteral("Ángulo del cono"), headAngle_);
    form->addRow(QString(), flip_);
    for (QDoubleSpinBox* box : {diameter_, depth_, headDiameter_, headDepth_, headAngle_}) {
        connect(box, &QDoubleSpinBox::valueChanged, this, [this] { updatePreview(); });
    }
    for (QCheckBox* box : {throughAll_, flip_}) {
        connect(box, &QCheckBox::toggled, this, [this] {
            updateVisibility();
            updatePreview();
        });
    }
    connect(kind_, &QComboBox::currentIndexChanged, this, [this] {
        updateVisibility();
        updatePreview();
    });
    connect(sketchBox_, &QComboBox::currentIndexChanged, this, [this] {
        sketch_ = document::FeatureId(sketchBox_->currentData().toULongLong());
        updatePreview();
    });
    updateVisibility();
}

void HoleTool::updateVisibility()
{
    const int kind = kind_->currentIndex();
    form_->setRowVisible(depth_, !throughAll_->isChecked());
    form_->setRowVisible(headDiameter_, kind != 0);
    form_->setRowVisible(headDepth_, kind == 1);
    form_->setRowVisible(headAngle_, kind == 2);
}

void HoleTool::selectionChanged(const render::ModelSelection&)
{
    // El sketch se elige en la lista; la selección en la vista no cambia nada.
}

std::unique_ptr<document::Feature> HoleTool::build() const
{
    if (sketch_ == 0) {
        return nullptr;
    }
    auto f = std::make_unique<document::HoleFeature>();
    if (original_) {
        f->setName(original_->name());
    }
    f->sketch = sketch_;
    f->spec.kind = geometry::HoleKind(kind_->currentIndex());
    f->spec.diameter = diameter_->value();
    f->spec.throughAll = throughAll_->isChecked();
    f->spec.depth = depth_->value();
    f->spec.headDiameter = headDiameter_->value();
    f->spec.headDepth = headDepth_->value();
    f->spec.headAngle = headAngle_->value() * kDegree;
    f->flip = flip_->isChecked();
    return f;
}

QString HoleTool::inputsText() const
{
    if (sketchBox_->count() == 0) {
        return QStringLiteral("Primero dibuja un sketch con círculos (o puntos) donde van los agujeros");
    }
    return QStringLiteral("Agujeros en los círculos de «%1»").arg(sketchBox_->currentText());
}

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

QString PlaneTool::inputsText() const
{
    if (base_->currentIndex() == 3 && face_.empty()) {
        return QStringLiteral("Elige en la vista la cara plana de la que parte el plano");
    }
    return QStringLiteral("Plano a %1 mm de «%2»").arg(offset_->value()).arg(base_->currentText());
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
    base_ = combo({QStringLiteral("Plano XY"), QStringLiteral("Plano XZ"), QStringLiteral("Plano YZ")});
    for (const auto& [id, plane] : before().planes) {
        const document::Feature* f = document_.findFeature(id);
        base_->addItem(f ? QString::fromStdString(f->name()) : QStringLiteral("Plano"));
        planeIds_.push_back(id);
    }
    module_ = lengthBox(2.0);
    module_->setMinimum(0.1);
    field("module", module_);
    teeth_ = new QSpinBox();
    teeth_->setRange(6, 300);
    teeth_->setValue(20);
    teeth_->setKeyboardTracking(false);
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
    operation_ = combo({QStringLiteral("Cuerpo nuevo"), QStringLiteral("Unir")});
    if (const auto* g = dynamic_cast<const document::GearFeature*>(original_.get())) {
        int at = g->base.origin ? int(*g->base.origin) : 0;
        if (!g->base.origin && g->base.reference != 0) {
            const auto it = std::ranges::find(planeIds_, g->base.reference);
            at = it != planeIds_.end() ? 3 + int(it - planeIds_.begin()) : 0;
        }
        base_->setCurrentIndex(at);
        module_->setValue(g->spec.module);
        teeth_->setValue(g->spec.teeth);
        pressure_->setValue(g->spec.pressureAngle / kDegree);
        thickness_->setValue(g->spec.thickness);
        bore_->setValue(g->spec.bore);
        helix_->setValue(g->spec.helixAngle / kDegree);
        operation_->setCurrentIndex(g->operation == document::Operation::Join ? 1 : 0);
    }
    form->addRow(QStringLiteral("Plano"), base_);
    form->addRow(QStringLiteral("Módulo"), module_);
    form->addRow(QStringLiteral("Dientes"), teeth_);
    form->addRow(QStringLiteral("Ángulo de presión"), pressure_);
    form->addRow(QStringLiteral("Ancho"), thickness_);
    form->addRow(QStringLiteral("Agujero central"), bore_);
    form->addRow(QStringLiteral("Ángulo de hélice"), helix_);
    form->addRow(QStringLiteral("Operación"), operation_);
    for (QComboBox* box : {base_, operation_}) {
        connect(box, &QComboBox::currentIndexChanged, this, [this] { updatePreview(); });
    }
    connect(teeth_, &QSpinBox::valueChanged, this, [this] { updatePreview(); });
    for (QDoubleSpinBox* box : {module_, pressure_, thickness_, bore_, helix_}) {
        connect(box, &QDoubleSpinBox::valueChanged, this, [this] { updatePreview(); });
    }
}

std::unique_ptr<document::Feature> GearTool::build() const
{
    auto f = std::make_unique<document::GearFeature>();
    if (original_) {
        f->setName(original_->name());
    }
    const int base = base_->currentIndex();
    if (base < 3) {
        f->base.origin = document::OriginPlane(base);
    } else {
        const document::FeatureId id = planeIds_[std::size_t(base - 3)];
        f->base.origin.reset();
        f->base.reference = id;
        f->base.lastPlane = before().planes.at(id);
    }
    f->spec.module = module_->value();
    f->spec.teeth = teeth_->value();
    f->spec.pressureAngle = pressure_->value() * kDegree;
    f->spec.thickness = thickness_->value();
    f->spec.bore = bore_->value();
    f->spec.helixAngle = helix_->value() * kDegree;
    f->operation = operation_->currentIndex() == 1 ? document::Operation::Join : document::Operation::NewBody;
    return f;
}

QString GearTool::inputsText() const
{
    return QStringLiteral("%1 dientes, módulo %2: Ø primitivo %3 mm")
        .arg(teeth_->value())
        .arg(module_->value())
        .arg(module_->value() * teeth_->value());
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
    count_ = new QSpinBox();
    count_->setRange(2, 200);
    count_->setValue(3);
    count_->setSuffix(QStringLiteral(" copias en total"));
    count_->setKeyboardTracking(false);
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
        count_->setValue(p->count);
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
    connect(count_, &QSpinBox::valueChanged, this, [this] { updatePreview(); });
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
    f->count = count_->value();
    f->spacing = spacing_->value();
    f->angle = angle_->value() * kDegree;
    f->operation = operation_->currentIndex() == 1 ? document::Operation::NewBody : document::Operation::Join;
    return f;
}

QString PatternTool::inputsText() const
{
    if (bodyBox_->count() == 0) {
        return QStringLiteral("No hay cuerpos que repetir");
    }
    return QStringLiteral("Patrón %1 de «%2»: %3 en total")
        .arg(layout_->currentIndex() == 1 ? QStringLiteral("circular") : QStringLiteral("lineal"))
        .arg(bodyBox_->currentText())
        .arg(count_->value());
}

} // namespace nuf::app
