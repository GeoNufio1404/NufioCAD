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
    track("taper", taper1_);
    target1_ = new QPushButton(QStringLiteral("Elegir cara de destino"));
    extent2_ = extentCombo();
    distance2_ = lengthBox(1.0);
    field("distance2", distance2_);
    distance2_->setMinimum(0.0);
    taper2_ = angleBox(0.0, 89.0);
    track("taper2", taper2_);
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
                emit message(QStringLiteral("Haz clic en la cara (plana, cilíndrica, esférica o toroidal) hasta la que llega la extrusión"));
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

std::optional<FeatureTool::Arc> ExtrudeTool::dimensionArc(const std::string& key) const
{
    // La conicidad: el ángulo entre la normal (lo que subiría recto) y la pared inclinada.
    if (key != "taper" && key != "taper2") {
        return std::nullopt;
    }
    const auto base = baseOf();
    if (!base) {
        return std::nullopt;
    }
    const sketch::Vec3 n = base->second;
    const sketch::Vec3 start = moved(base->first, n, offset_->value());
    const double radius = arcRadius();
    const bool second = key == "taper2";
    const sketch::Vec3 dir = second ? sketch::Vec3{-n.x, -n.y, -n.z} : n;
    // Un vector del plano de la base: la pared se inclina hacia él.
    const sketch::Vec3 sideways = normalized(minus(perpendicularPoint(start, n, 1.0), start));
    return Arc{start, normalized(cross(dir, sideways)), moved(start, dir, radius), (second ? taper2_ : taper1_)->value() * kDegree};
}

std::optional<std::pair<sketch::Vec3, sketch::Vec3>> ExtrudeTool::baseOf() const
{
    // La misma base que la flecha de arrastre: centro del primer perfil (o de la cara) y su normal.
    const document::ModelState& state = before();
    std::optional<std::pair<sketch::Vec3, sketch::Vec3>> base;
    if (!profiles_.empty()) {
        const auto out = state.sketches.find(profiles_.front().sketch);
        if (out != state.sketches.end()) {
            if (const auto profile = document::resolveProfile(sketch::findProfiles(*out->second.sketch), profiles_.front())) {
                base = std::pair{out->second.plane.toWorld(profileCenter(*profile)), out->second.plane.normal};
            }
        }
    } else if (const auto hit = state.findFace(face_)) {
        const geometry::Shape& shape = state.bodies[hit->body].shape;
        if (const auto plane = shape.facePlane(hit->face)) {
            base = std::pair{shape.faceCenter(hit->face), plane->normal};
        }
    }
    return base;
}

std::optional<FeatureTool::Arrow> ExtrudeTool::dimensionArrow(const std::string& key) const
{
    const auto base = baseOf();
    if (!base) {
        return std::nullopt;
    }
    const sketch::Vec3 n = base->second;
    const sketch::Vec3 start = moved(base->first, n, offset_->value());
    if (key == "startOffset") {
        return arrowAlong(base->first, n, offset_->value());
    }
    if (key == "distance") {
        if (direction_->currentIndex() == int(geometry::ExtrudeDirection::Symmetric)) {
            const double half = distance1_->value() / 2.0;
            return Arrow{moved(start, n, -half), moved(start, n, half)};
        }
        return arrowAlong(start, n, distance1_->value());
    }
    if (key == "distance2") {
        return arrowAlong(start, sketch::Vec3{-n.x, -n.y, -n.z}, distance2_->value());
    }
    return std::nullopt;
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

std::optional<FeatureTool::Arc> RevolveTool::dimensionArc(const std::string& key) const
{
    if ((key != "angle" && key != "angle2") || profiles_.empty() || extent_->currentIndex() == int(geometry::RevolveExtent::Full)) {
        return std::nullopt;
    }
    const std::unique_ptr<document::Feature> built = build();
    const auto* revolve = dynamic_cast<const document::RevolveFeature*>(built.get());
    if (revolve == nullptr) {
        return std::nullopt;
    }
    geometry::Axis axis;
    try {
        axis = document::axisOf(before(), revolve->axis);
    } catch (const std::exception&) {
        return std::nullopt;
    }
    // El arco pasa por el centro del primer perfil, que es lo que gira.
    const auto out = before().sketches.find(profiles_.front().sketch);
    if (out == before().sketches.end()) {
        return std::nullopt;
    }
    const auto profile = document::resolveProfile(sketch::findProfiles(*out->second.sketch), profiles_.front());
    if (!profile) {
        return std::nullopt;
    }
    const sketch::Vec3 direction = normalized(axis.direction);
    const sketch::Vec3 point = out->second.plane.toWorld(profileCenter(*profile));
    const sketch::Vec3 center = moved(axis.point, direction, dot(minus(point, axis.point), direction));
    const sketch::Vec3 start = std::sqrt(dot(minus(point, center), minus(point, center))) > 1e-6
                                   ? point
                                   : perpendicularPoint(center, direction, arcRadius());
    const double angle1 = revolve->spec.angle1;
    if (key == "angle2") {
        if (revolve->spec.direction != geometry::ExtrudeDirection::Two) {
            return std::nullopt;
        }
        return Arc{center, direction, start, -revolve->spec.angle2};
    }
    const bool symmetric = revolve->spec.direction == geometry::ExtrudeDirection::Symmetric;
    return Arc{center, direction, symmetric ? rotatedAbout(start, center, direction, -angle1 / 2.0) : start, angle1};
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
    viewport_.setEdgeAssist(true);  // zona de captura más ancha y la arista cercana marcada en naranja
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
    if (style_ == Style::Fillet) {
        // Radio variable: de un radio en un extremo de la arista a otro radio en el otro (O2).
        const auto* e = dynamic_cast<const document::EdgeFeature*>(original_.get());
        variable_ = new QCheckBox(QStringLiteral("Radio variable (de un extremo al otro)"));
        size2_ = lengthBox(e != nullptr ? e->size2 : 2.0);
        size2_->setMinimum(0.01);
        field("size2", size2_);
        variable_->setChecked(e != nullptr && e->variable);
        form->addRow(QString(), variable_);
        form->addRow(QStringLiteral("Radio del otro extremo"), size2_);
        middle_ = lengthBox(e != nullptr ? e->middle : 0.0);
        middle_->setMinimum(0.0);
        middle_->setToolTip(QStringLiteral("Radio a mitad de la arista; 0 deja el cambio recto de un extremo al otro."));
        field("middle", middle_);
        form->addRow(QStringLiteral("Radio en el centro (0 = ninguno)"), middle_);
        form->setRowVisible(size2_, variable_->isChecked());
        form->setRowVisible(middle_, variable_->isChecked());
        connect(variable_, &QCheckBox::toggled, this, [this, form] {
            form->setRowVisible(size2_, variable_->isChecked());
            form->setRowVisible(middle_, variable_->isChecked());
            updatePreview();
        });
        connect(size2_, &QDoubleSpinBox::valueChanged, this, [this] { updatePreview(); });
        connect(middle_, &QDoubleSpinBox::valueChanged, this, [this] { updatePreview(); });
    }
    if (style_ == Style::Chamfer) {
        // Chaflán con una distancia por los dos lados, con dos distancias o con distancia y ángulo (O2).
        const auto* e = dynamic_cast<const document::EdgeFeature*>(original_.get());
        auto* kind = combo({QStringLiteral("Igual por los dos lados"), QStringLiteral("Dos distancias"), QStringLiteral("Distancia y ángulo")});
        chamferKind_ = kind;
        size2_ = lengthBox(e != nullptr ? e->size2 : 1.0);
        size2_->setMinimum(0.01);
        field("size2", size2_);
        angle_ = angleBox(e != nullptr ? e->angle / kDegree : 45.0, 89.0);
        angle_->setMinimum(1.0);
        field("angle", angle_);
        flip_ = new QCheckBox(QStringLiteral("Medir desde la otra cara"));
        flip_->setToolTip(QStringLiteral("La primera distancia va sobre una de las dos caras de la arista; esto la pasa a la otra"));
        if (e != nullptr) {
            kind->setCurrentIndex(e->style == geometry::ChamferStyle::TwoDistances ? 1 : e->style == geometry::ChamferStyle::DistanceAngle ? 2 : 0);
            flip_->setChecked(e->flip);
        }
        form->insertRow(0, QStringLiteral("Tipo"), kind);
        const auto distanceRow = form->rowCount();
        (void)distanceRow;
        form->addRow(QStringLiteral("Segunda distancia"), size2_);
        form->addRow(QStringLiteral("Ángulo"), angle_);
        form->addRow(QString(), flip_);
        const auto showKind = [this, form] {
            const int k = chamferKind_->currentIndex();
            form->setRowVisible(size2_, k == 1);
            form->setRowVisible(angle_, k == 2);
            form->setRowVisible(flip_, k != 0);
        };
        showKind();
        connect(kind, &QComboBox::currentIndexChanged, this, [this, showKind] {
            showKind();
            updatePreview();
        });
        connect(size2_, &QDoubleSpinBox::valueChanged, this, [this] { updatePreview(); });
        connect(angle_, &QDoubleSpinBox::valueChanged, this, [this] { updatePreview(); });
        connect(flip_, &QCheckBox::toggled, this, [this] { updatePreview(); });
    }
    auto* clear = new QPushButton(QStringLiteral("Quitar selección"));
    form->addRow(QString(), clear);
    connect(clear, &QPushButton::clicked, this, [this] {
        edges_.clear();
        pickedFaces_.clear();
        faceEdges_.clear();
        viewport_.clearModelSelection();
        updatePreview();
    });
}

void EdgeTool::selectionChanged(const render::ModelSelection& selection)
{
    // La lista de aristas es de la herramienta, no del visor (que redibuja el
    // resultado en cada cambio y pierde su selección): cada clic en una arista
    // la agrega o la quita, y el visor se deja sin selección.
    // Un clic en una cara la elige (o la quita si ya estaba): con una sola, sus aristas; con dos o más, las aristas que
    // comparten, que es donde se unen (O2).
    bool facesChanged = false;
    for (const render::ModelSelection::Face& face : selection.faces) {
        if (!before().findFace(face.ref)) {
            continue;
        }
        const auto it = std::ranges::find(pickedFaces_, face.ref);
        if (it == pickedFaces_.end()) {
            pickedFaces_.push_back(face.ref);
        } else {
            pickedFaces_.erase(it);
        }
        facesChanged = true;
    }
    if (facesChanged) {
        applyFaces();
    }
    for (const render::ModelSelection::Edge& edge : selection.edges) {
        if (!before().findEdge(edge.ref)) {
            continue;  // una arista del resultado de la vista previa, no del cuerpo
        }
        const auto it = std::ranges::find(edges_, edge.ref);
        if (it == edges_.end()) {
            edges_.push_back(edge.ref);
        } else {
            edges_.erase(it);
            std::erase(faceEdges_, edge.ref);  // la quitó a mano: que rehacer las caras no la devuelva
        }
    }
    if (!selection.empty()) {
        viewport_.clearModelSelection();
    }
}

void EdgeTool::applyFaces()
{
    // Primero se quitan las que sumaron las caras la vez anterior.
    for (const geometry::ElementRef& ref : faceEdges_) {
        std::erase(edges_, ref);
    }
    faceEdges_.clear();
    // Por cuerpo, cuántas de las caras elegidas toca cada arista.
    std::map<std::size_t, std::vector<int>> facesOfBody;
    for (const geometry::ElementRef& face : pickedFaces_) {
        if (const auto hit = before().findFace(face)) {
            facesOfBody[hit->body].push_back(hit->face);
        }
    }
    const int needed = pickedFaces_.size() >= 2 ? 2 : 1;
    for (const auto& [body, faces] : facesOfBody) {
        const geometry::Shape& shape = before().bodies[body].shape;
        std::map<int, int> touches;
        for (const int face : faces) {
            const std::vector<int> around = shape.faceEdges(face);
            for (const int edge : std::set<int>(around.begin(), around.end())) {  // una arista con costura no cuenta doble
                ++touches[edge];
            }
        }
        for (const auto& [edge, count] : touches) {
            if (count < needed) {
                continue;
            }
            const geometry::ElementRef ref = shape.edgeRef(edge);
            if (std::ranges::find(edges_, ref) == edges_.end()) {
                edges_.push_back(ref);
                faceEdges_.push_back(ref);
            }
        }
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
    if (style_ == Style::Fillet && variable_ != nullptr) {
        f->variable = variable_->isChecked();
        f->size2 = size2_->value();
        f->middle = middle_ != nullptr ? middle_->value() : 0.0;
    }
    if (style_ == Style::Chamfer && chamferKind_ != nullptr) {
        const int k = chamferKind_->currentIndex();
        f->style = k == 1 ? geometry::ChamferStyle::TwoDistances : k == 2 ? geometry::ChamferStyle::DistanceAngle : geometry::ChamferStyle::Equal;
        f->size2 = size2_->value();
        f->angle = angle_->value() * kDegree;
        f->flip = flip_->isChecked();
    }
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
        return QStringLiteral("Elige las aristas (se marcan en naranja al acercar el cursor; varias con clics sucesivos), una cara "
                              "para elegir todas las suyas, o dos caras para elegir la arista donde se unen");
    }
    return edges_.size() == 1 ? QStringLiteral("1 arista")
                              : QStringLiteral("%1 aristas (dos caras elegen la arista donde se unen)").arg(edges_.size());
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
    closed_ = new QCheckBox(QStringLiteral("Vaciado cerrado (hueco por dentro, sin abrir caras)"));
    closed_->setToolTip(QStringLiteral("El cuerpo queda con una cavidad cerrada de paredes del espesor dado. Basta un clic en una cara para elegir el cuerpo. Solo hacia dentro"));
    if (const auto* s = dynamic_cast<const document::ShellFeature*>(original_.get())) {
        closed_->setChecked(s->closed);
    }
    form->addRow(QStringLiteral("Espesor"), thickness_);
    form->addRow(QStringLiteral("Dirección"), direction_);
    form->addRow(QString(), closed_);
    auto* clear = new QPushButton(QStringLiteral("Quitar selección"));
    form->addRow(QString(), clear);
    connect(thickness_, &QDoubleSpinBox::valueChanged, this, [this] { updatePreview(); });
    connect(direction_, &QComboBox::currentIndexChanged, this, [this] { updatePreview(); });
    connect(closed_, &QCheckBox::toggled, this, [this, form] {
        form->setRowVisible(direction_, !closed_->isChecked());
        updatePreview();
    });
    form->setRowVisible(direction_, !closed_->isChecked());
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
    f->closed = closed_->isChecked();
    f->outward = !f->closed && direction_->currentIndex() == 1;
    return f;
}

QString ShellTool::inputsText() const
{
    if (faces_.empty()) {
        return closed_ != nullptr && closed_->isChecked() ? QStringLiteral("Haz clic en una cara del cuerpo que quieres vaciar por dentro")
                                                          : QStringLiteral("Elige las caras que quedan abiertas (varias con clics sucesivos)");
    }
    if (closed_ != nullptr && closed_->isChecked()) {
        return QStringLiteral("Cuerpo elegido: se vacía por dentro sin abrir ninguna cara");
    }
    return faces_.size() == 1 ? QStringLiteral("1 cara abierta") : QStringLiteral("%1 caras abiertas").arg(faces_.size());
}

// --- Agujeros ----------------------------------------------------------------------

HoleTool::HoleTool(document::Document& document, render::Viewport& viewport,
                   std::optional<document::FeatureId> editing)
    : FeatureTool(document, viewport, editing)
{
    if (const auto* h = dynamic_cast<const document::HoleFeature*>(original_.get())) {
        sketch_ = h->onFace ? 0 : h->sketch;
        faceWanted_ = h->onFace;
    }
    placeAtClick_ = true;
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
    // «Clic en una cara»: pone un agujero justo donde se hace clic, sin dibujar un sketch antes (es la primera opción).
    sketchBox_->insertItem(0, QStringLiteral("Clic en una cara…"), qulonglong(0));
    if (sketch_ == 0 && !faceWanted_ && sketchBox_->count() > 1) {
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
    {
        const auto* h = dynamic_cast<const document::HoleFeature*>(original_.get());
        const bool on = h != nullptr && h->onFace;
        createPlacement(form, placement_, on ? &h->base : nullptr, on ? h->x : 0.0, on ? h->y : 0.0);
    }
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
        updateVisibility();
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
    for (QWidget* w : {static_cast<QWidget*>(placement_.base), static_cast<QWidget*>(placement_.x), static_cast<QWidget*>(placement_.y)}) {
        form_->setRowVisible(w, faceMode());
    }
}

void HoleTool::selectionChanged(const render::ModelSelection& selection)
{
    // En el modo «clic en una cara», la cara y el punto del clic dan el agujero; si no, el sketch se elige en la lista.
    if (faceMode() && placementSelection(placement_, selection)) {
        updatePreview();
    }
}

std::unique_ptr<document::Feature> HoleTool::build() const
{
    const bool onFace = faceMode();
    if (sketch_ == 0 && !onFace) {
        return nullptr;
    }
    auto f = std::make_unique<document::HoleFeature>();
    if (original_) {
        f->setName(original_->name());
    }
    f->sketch = onFace ? 0 : sketch_;
    if (onFace) {
        f->onFace = true;
        f->base = placementAttachment(placement_);
        f->x = placement_.x->value();
        f->y = placement_.y->value();
    }
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
    if (faceMode()) {
        return placementOnFace(placement_) ? QStringLiteral("Agujero donde hiciste clic en la cara (puedes mover su posición abajo)")
                                           : QStringLiteral("Haz clic en la cara plana donde va el agujero");
    }
    if (sketchBox_->count() <= 1) {
        return QStringLiteral("Haz clic en una cara, o dibuja un sketch con círculos (o puntos) donde van los agujeros");
    }
    return QStringLiteral("Agujeros en los círculos de «%1»").arg(sketchBox_->currentText());
}

} // namespace nuf::app

namespace nuf::app {

std::optional<FeatureTool::Arrow> EdgeTool::dimensionArrow(const std::string& key) const
{
    // Radio o distancia, la segunda (o el radio del final) y el del medio: todas salen de la arista hacia dentro.
    const QDoubleSpinBox* measure = key == "size" ? size_ : key == "size2" ? size2_ : key == "middle" ? middle_ : nullptr;
    if (measure == nullptr || edges_.empty()) {
        return std::nullopt;
    }
    const auto hit = before().findEdge(edges_.front());
    if (!hit) {
        return std::nullopt;
    }
    const geometry::Shape& shape = before().bodies[hit->body].shape;
    const auto line = shape.edgeLine(hit->edge);
    if (!line) {
        return std::nullopt;  // arista curva: sin flecha
    }
    // Desde el punto de la arista hacia dentro del cuerpo, perpendicular a la arista.
    const geometry::Box box = shape.bounds();
    sketch::Vec3 v{(box.min.x + box.max.x) / 2.0 - line->first.x, (box.min.y + box.max.y) / 2.0 - line->first.y,
                   (box.min.z + box.max.z) / 2.0 - line->first.z};
    const sketch::Vec3 d = line->second;
    const double along = v.x * d.x + v.y * d.y + v.z * d.z;
    v = {v.x - d.x * along, v.y - d.y * along, v.z - d.z * along};
    const double length = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    if (length < 1e-9) {
        return std::nullopt;
    }
    return arrowAlong(line->first, {v.x / length, v.y / length, v.z / length}, measure->value());
}

std::optional<FeatureTool::Arrow> ShellTool::dimensionArrow(const std::string& key) const
{
    if (key != "thickness" || faces_.empty()) {
        return std::nullopt;
    }
    const auto hit = before().findFace(faces_.front());
    if (!hit) {
        return std::nullopt;
    }
    const geometry::Shape& shape = before().bodies[hit->body].shape;
    const auto plane = shape.facePlane(hit->face);
    if (!plane) {
        return std::nullopt;
    }
    // Desde la cara abierta hacia dentro de la pared (o hacia fuera).
    const double sign = direction_->currentIndex() == 1 ? 1.0 : -1.0;
    return arrowAlong(shape.faceCenter(hit->face), plane->normal, sign * thickness_->value());
}

std::optional<FeatureTool::Arrow> HoleTool::dimensionArrow(const std::string& key) const
{
    if (faceMode()) {
        // Sobre una cara: el agujero entra por el punto elegido, hacia dentro de la pieza.
        if (key == "x" || key == "y") {
            return placementArrow(placement_, key);
        }
        const sketch::Plane face = document::attachmentPlane(before(), placementAttachment(placement_));
        const sketch::Vec3 c = face.toWorld({placement_.x->value(), placement_.y->value()});
        const double side = flip_->isChecked() ? 1.0 : -1.0;
        const auto across = [&](const QDoubleSpinBox* box) {
            return arrowAlong(moved(c, face.xAxis, -box->value() / 2.0), face.xAxis, box->value());
        };
        if (key == "diameter") {
            return across(diameter_);
        }
        if (key == "headDiameter") {
            return across(headDiameter_);
        }
        if (key == "depth" || key == "headDepth") {
            return arrowAlong(c, face.normal, side * (key == "depth" ? depth_ : headDepth_)->value());
        }
        return std::nullopt;
    }
    const auto out = before().sketches.find(sketch_);
    if (out == before().sketches.end()) {
        return std::nullopt;
    }
    // El primer agujero del sketch (círculo o punto).
    std::optional<sketch::Vec2> center;
    for (const sketch::Entity& e : out->second.sketch->entities()) {
        if (e.reference) {
            continue;
        }
        if (const auto* circle = std::get_if<sketch::Circle>(&e.geometry); circle && !e.construction) {
            center = circle->center;
            break;
        }
        if (const auto* point = std::get_if<sketch::Point>(&e.geometry)) {
            center = point->position;
            break;
        }
    }
    if (!center) {
        return std::nullopt;
    }
    const sketch::Plane& plane = out->second.plane;
    const sketch::Vec3 c = plane.toWorld(*center);
    const sketch::Vec3 down = flip_->isChecked() ? plane.normal : sketch::Vec3{-plane.normal.x, -plane.normal.y, -plane.normal.z};
    if (key == "diameter") {
        return arrowAlong(moved(c, plane.xAxis, -diameter_->value() / 2.0), plane.xAxis, diameter_->value());
    }
    if (key == "depth") {
        return arrowAlong(c, down, depth_->value());
    }
    if (key == "headDiameter") {
        return arrowAlong(moved(c, plane.xAxis, -headDiameter_->value() / 2.0), plane.xAxis, headDiameter_->value());
    }
    if (key == "headDepth") {
        return arrowAlong(c, down, headDepth_->value());
    }
    return std::nullopt;
}

std::optional<FeatureTool::Arc> HoleTool::dimensionArc(const std::string& key) const
{
    // El ángulo del cono del avellanado: el que forman las dos generatrices opuestas, con el vértice del cono en el eje
    // del agujero (por debajo del borde, a la profundidad donde el cono se cerraría).
    if (key != "headAngle") {
        return std::nullopt;
    }
    const auto out = before().sketches.find(sketch_);
    if (out == before().sketches.end()) {
        return std::nullopt;
    }
    std::optional<sketch::Vec2> center;
    for (const sketch::Entity& e : out->second.sketch->entities()) {
        if (e.reference) {
            continue;
        }
        if (const auto* circle = std::get_if<sketch::Circle>(&e.geometry); circle && !e.construction) {
            center = circle->center;
            break;
        }
        if (const auto* point = std::get_if<sketch::Point>(&e.geometry)) {
            center = point->position;
            break;
        }
    }
    if (!center) {
        return std::nullopt;
    }
    const sketch::Plane& plane = out->second.plane;
    const sketch::Vec3 c = plane.toWorld(*center);
    const sketch::Vec3 up = flip_->isChecked() ? sketch::Vec3{-plane.normal.x, -plane.normal.y, -plane.normal.z} : plane.normal;
    const double angle = headAngle_->value() * kDegree;
    const double half = 0.5 * angle;
    const double depthToApex = (headDiameter_->value() / 2.0) / std::tan(half);
    const sketch::Vec3 apex = moved(c, up, -depthToApex);
    // Las dos generatrices parten del vértice hacia el borde del cono: a la izquierda, −sen(½)·x + cos(½)·arriba.
    const sketch::Vec3 left{-std::sin(half) * plane.xAxis.x + std::cos(half) * up.x,
                            -std::sin(half) * plane.xAxis.y + std::cos(half) * up.y,
                            -std::sin(half) * plane.xAxis.z + std::cos(half) * up.z};
    const sketch::Vec3 axis{up.y * plane.xAxis.z - up.z * plane.xAxis.y, up.z * plane.xAxis.x - up.x * plane.xAxis.z,
                            up.x * plane.xAxis.y - up.y * plane.xAxis.x};
    const double radius = std::min(arcRadius(), 0.9 * depthToApex / std::cos(half));
    return Arc{apex, axis, {apex.x + radius * left.x, apex.y + radius * left.y, apex.z + radius * left.z}, angle};
}

} // namespace nuf::app
