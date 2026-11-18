#include "FeatureTools.hpp"
#include "Theme.hpp"

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
#include <QScrollArea>
#include <QSpinBox>
#include <QEvent>
#include <QLineEdit>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <numbers>
#include "FeatureToolsCommon.hpp"

namespace nuf::app {

namespace {
QLineEdit* lineOf(QDoubleSpinBox* box)
{
    return box->findChild<QLineEdit*>(QString(), Qt::FindDirectChildrenOnly);
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
    viewport_.hideDimensionArrow();
    viewport_.setAdditiveSelection(false);
    if (panel_) {
        panel_->deleteLater();
    }
}

FeatureTool::PrimaryField FeatureTool::primaryField() const
{
    static const std::vector<std::pair<std::string, QString>> labels = {
        {"distance", QStringLiteral("Distancia")}, {"angle", QStringLiteral("Ángulo")}, {"diameter", QStringLiteral("Diámetro")},
        {"size", QStringLiteral("Tamaño")}, {"thickness", QStringLiteral("Espesor")}, {"depth", QStringLiteral("Profundidad")},
        {"width", QStringLiteral("Ancho")}, {"height", QStringLiteral("Alto")}, {"module", QStringLiteral("Módulo")},
        {"radius", QStringLiteral("Radio")}, {"length", QStringLiteral("Largo")}, {"pitch", QStringLiteral("Paso")}};
    for (const auto& [key, box] : fields_) {
        if (key == "x" || key == "y" || key == "z" || key == "rotation" || box == nullptr) {
            continue;
        }
        QString label = QString::fromStdString(key);
        for (const auto& [known, text] : labels) {
            if (known == key) {
                label = text;
            }
        }
        return {label, box};
    }
    return {};
}

void FeatureTool::start()
{
    // El contenido va en un área con desplazamiento (así no empuja el tamaño mínimo de la ventana y, si no cabe, se
    // desplaza en vez de cortarse: Fase 20, R2) y los botones Aceptar y Cancelar quedan fijos abajo, siempre a la vista.
    panel_ = new QWidget();
    auto* outer = new QVBoxLayout(panel_);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    auto* scroll = new QScrollArea();
    scroll->setObjectName(QStringLiteral("toolScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->viewport()->setAutoFillBackground(false);
    auto* content = new QWidget();
    content->setAutoFillBackground(false);
    scroll->setWidget(content);
    outer->addWidget(scroll, 1);
    auto* layout = new QVBoxLayout(content);
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
    for (const auto& [key, box] : arrowBoxes_) {
        box->installEventFilter(this);
        if (lineOf(box) != nullptr) {
            lineOf(box)->installEventFilter(this);
        }
    }
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
    layout->addStretch();
    auto* buttons = new QHBoxLayout();
    buttons->setContentsMargins(11, 6, 11, 11);
    okButton_ = new QPushButton(QStringLiteral("Aceptar"));
    okButton_->setDefault(true);
    auto* cancel = new QPushButton(QStringLiteral("Cancelar"));
    buttons->addStretch();
    buttons->addWidget(cancel);
    buttons->addWidget(okButton_);
    outer->addLayout(buttons);
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
    // El panel aún no se mostró: si algún cuadro nace con el foco (Extruir), su flecha sale cuando ya lo tiene.
    QTimer::singleShot(150, this, [this] { refreshArrow(); });
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

void FeatureTool::stopPreview()
{
    previewRun_.cancel();
    emit computingChanged(false);
    if (previewTimer_ != nullptr) {
        previewTimer_->stop();
    }
    updating_ = true;  // updatePreview() ya no hace nada
    viewport_.hideDimensionArrow();
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

void FeatureTool::createPlacement(QFormLayout* form, Placement& p, const document::SketchAttachment* original, double x,
                                  double y)
{
    p.base = combo({QStringLiteral("Plano XY"), QStringLiteral("Plano XZ"), QStringLiteral("Plano YZ")});
    for (const auto& [id, plane] : before().planes) {
        const document::Feature* f = document_.findFeature(id);
        p.base->addItem(f ? QString::fromStdString(f->name()) : QStringLiteral("Plano"));
        p.planeIds.push_back(id);
    }
    p.x = lengthBox(x);
    field("x", p.x);
    p.y = lengthBox(y);
    field("y", p.y);
    int at = 0;
    if (original != nullptr) {
        if (original->origin) {
            at = int(*original->origin);
        } else if (original->reference != 0) {
            const auto it = std::ranges::find(p.planeIds, original->reference);
            at = it != p.planeIds.end() ? 3 + int(it - p.planeIds.begin()) : 0;
        } else if (!original->face.empty()) {
            p.face = original->face;
            p.base->addItem(QStringLiteral("Cara elegida"));
            at = p.base->count() - 1;
        }
    }
    p.base->setCurrentIndex(at);
    form->addRow(QStringLiteral("Plano o cara"), p.base);
    form->addRow(QStringLiteral("Posición X"), p.x);
    form->addRow(QStringLiteral("Posición Y"), p.y);
    connect(p.base, &QComboBox::currentIndexChanged, this, [this] { updatePreview(); });
    for (QDoubleSpinBox* box : {p.x, p.y}) {
        connect(box, &QDoubleSpinBox::valueChanged, this, [this] { updatePreview(); });
    }
}

bool FeatureTool::placementSelection(Placement& p, const render::ModelSelection& selection)
{
    for (const render::ModelSelection::Face& f : selection.faces) {
        const auto hit = before().findFace(f.ref);
        if (!hit) {
            continue;  // una cara de la vista previa que no existe antes
        }
        const geometry::Shape& shape = before().bodies[hit->body].shape;
        const auto plane = shape.facePlane(hit->face);
        if (!plane) {
            emit message(QStringLiteral("Elige una cara plana"));
            continue;
        }
        p.face = f.ref;
        if (p.base->count() == 3 + int(p.planeIds.size())) {
            p.base->addItem(QStringLiteral("Cara elegida"));
        }
        // Se pone en el centro de la cara; la posición se puede cambiar después.
        const sketch::Vec2 center = plane->toLocal(shape.faceCenter(hit->face));
        updating_ = true;
        p.base->setCurrentIndex(p.base->count() - 1);
        p.x->setValue(center.x);
        p.y->setValue(center.y);
        updating_ = false;
        viewport_.clearModelSelection();
        return true;
    }
    if (!selection.empty()) {
        viewport_.clearModelSelection();
    }
    return false;
}

bool FeatureTool::placementOnFace(const Placement& p) const
{
    return p.base != nullptr && p.base->currentIndex() >= 3 + int(p.planeIds.size());
}

document::SketchAttachment FeatureTool::placementAttachment(const Placement& p) const
{
    document::SketchAttachment at;
    const int index = p.base->currentIndex();
    if (index < 3) {
        at.origin = document::OriginPlane(index);
    } else if (index < 3 + int(p.planeIds.size())) {
        const document::FeatureId id = p.planeIds[std::size_t(index - 3)];
        at.origin.reset();
        at.reference = id;
        at.lastPlane = before().planes.at(id);
    } else {
        at.origin.reset();
        at.reference = 0;
        at.face = p.face;
        if (const auto hit = before().findFace(p.face)) {
            if (const auto plane = before().bodies[hit->body].shape.facePlane(hit->face)) {
                at.lastPlane = *plane;
            }
        }
    }
    return at;
}

void FeatureTool::updatePreview()
{
    if (updating_ || !panel_) {
        return;
    }
    inputsLabel_->setText(inputsText());
    if (const int delay = previewDelayMs(); delay > 0 && firstPreviewDone_ && !previewDue_) {
        // Se espera a que se deje de teclear o de mover el cuadro.
        if (previewTimer_ == nullptr) {
            previewTimer_ = new QTimer(this);
            previewTimer_->setSingleShot(true);
            connect(previewTimer_, &QTimer::timeout, this, [this] {
                previewDue_ = true;
                updatePreview();
                previewDue_ = false;
            });
        }
        statusLabel_->setText(QStringLiteral("<span style='color:%1'>Calculando…</span>").arg(toneColor(Tone::Muted)));
        previewTimer_->start(delay);
        return;
    }
    firstPreviewDone_ = true;
    if (previewTimer_ != nullptr) {
        previewTimer_->stop();
    }
    std::unique_ptr<document::Feature> feature = make();
    if (!feature) {
        previewRun_.cancel();
        emit computingChanged(false);
        viewport_.showBodies(before().bodies);
        showPlanes(viewport_, document_, before(), nullptr);
        viewport_.clearToolPreview();
        viewport_.hideDistanceHandle();
        statusLabel_->clear();
        okButton_->setEnabled(false);
        return;
    }
    feature->setId(id_);

    // El cálculo (una rosca, un engranaje, una booleana grande) va en un hilo aparte sobre copias; mientras
    // tanto la vista se queda como estaba y se puede seguir moviendo, girando o cambiando el valor (lo
    // nuevo cancela lo anterior).
    struct Calculation {
        std::shared_ptr<document::Feature> feature;
        document::Preview result;
        geometry::Shape tool;  // lo que se quita (rojo translúcido), calculado también en el hilo
    };
    auto calculation = std::make_shared<Calculation>();
    calculation->feature = std::move(feature);
    okButton_->setEnabled(false);  // hasta saber si lo que se pide sale bien
    QTimer::singleShot(300, this, [this] {
        if (previewRun_.running() && statusLabel_ != nullptr) {
            statusLabel_->setText(QStringLiteral("<span style='color:%1'>Calculando…</span>").arg(toneColor(Tone::Muted)));
        }
    });
    emit computingChanged(true);
    const bool removes = removesMaterial();
    previewRun_.start(
        [calculation, removes, state = before(), lookup = document_.snapshotLookup()] {
            calculation->result = document::preview(state, *calculation->feature, lookup);
            if (removes && calculation->result.status.health != document::Health::Error) {
                // El sólido que se quita se construye aquí y no al terminar, en el hilo de la interfaz.
                try {
                    const document::Feature& previewed = *calculation->feature;
                    if (const auto* e = dynamic_cast<const document::ExtrudeFeature*>(&previewed)) {
                        calculation->tool = e->tool(state);
                    } else if (const auto* r = dynamic_cast<const document::RevolveFeature*>(&previewed)) {
                        calculation->tool = r->tool(state);
                    } else if (const auto* h = dynamic_cast<const document::HoleFeature*>(&previewed)) {
                        calculation->tool = h->tool(state);
                    }
                } catch (...) {
                    // Ya lo informa el estado.
                }
            }
        },
        [this, calculation] {
            emit computingChanged(false);
            if (panel_ && !updating_) {
                showPreview(*calculation->feature, calculation->result, calculation->tool);
            }
        });
}

void FeatureTool::showPreview(const document::Feature& feature, const document::Preview& result, const geometry::Shape& tool)
{
    viewport_.showBodies(result.state.bodies);
    showPlanes(viewport_, document_, result.state, &feature);

    // Lo que se quita, en rojo translúcido encima del resultado (el sólido ya viene calculado por el hilo de trabajo).
    viewport_.clearToolPreview();
    if (!tool.isNull() && result.status.health != document::Health::Error) {
        viewport_.showToolPreview(tool, true);
    }

    const QString text = QString::fromStdString(result.status.message);
    switch (result.status.health) {
    case document::Health::Error:
        statusLabel_->setText(QStringLiteral("<span style='color:%1'>%2</span>").arg(toneColor(Tone::Error), text.toHtmlEscaped()));
        okButton_->setEnabled(false);
        break;
    case document::Health::Warning:
        statusLabel_->setText(QStringLiteral("<span style='color:%1'>%2</span>").arg(toneColor(Tone::Warning), text.toHtmlEscaped()));
        okButton_->setEnabled(true);
        break;
    default:
        statusLabel_->clear();
        okButton_->setEnabled(true);
        break;
    }
    afterPreview(before());
    refreshArrow();
}

std::optional<sketch::Vec3> FeatureTool::bodyCenter(const std::string& key) const
{
    for (const document::Body& body : before().bodies) {
        if (body.key == key && !body.shape.isNull()) {
            const geometry::Box box = body.shape.bounds();
            return sketch::Vec3{(box.min.x + box.max.x) / 2.0, (box.min.y + box.max.y) / 2.0, (box.min.z + box.max.z) / 2.0};
        }
    }
    return std::nullopt;
}

std::optional<FeatureTool::Arrow> FeatureTool::placementArrow(const Placement& placement, const std::string& key) const
{
    const sketch::Plane plane = document::attachmentPlane(before(), placementAttachment(placement));
    const double x = placement.x->value();
    if (key == "x") {
        return arrowAlong(plane.origin, plane.xAxis, x);
    }
    if (key == "y") {
        return arrowAlong(plane.toWorld({x, 0.0}), plane.yAxis, placement.y->value());
    }
    return std::nullopt;
}

double FeatureTool::arcRadius() const
{
    double extent = 0.0;
    for (const document::Body& body : before().bodies) {
        if (!body.shape.isNull()) {
            const geometry::Box box = body.shape.bounds();
            extent = std::max({extent, box.max.x - box.min.x, box.max.y - box.min.y, box.max.z - box.min.z});
        }
    }
    return std::max(15.0, 0.4 * extent);
}

sketch::Vec3 FeatureTool::perpendicularPoint(const sketch::Vec3& center, const sketch::Vec3& axis, double radius)
{
    // Cualquier vector perpendicular al eje: el eje cruzado con el menos alineado de los ejes del modelo.
    const sketch::Vec3 other = std::abs(axis.x) < 0.9 ? sketch::Vec3{1.0, 0.0, 0.0} : sketch::Vec3{0.0, 1.0, 0.0};
    sketch::Vec3 v{axis.y * other.z - axis.z * other.y, axis.z * other.x - axis.x * other.z, axis.x * other.y - axis.y * other.x};
    const double length = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    return moved(center, {v.x / length, v.y / length, v.z / length}, radius);
}

FeatureTool::Arrow FeatureTool::arrowAlong(const sketch::Vec3& from, const sketch::Vec3& dir, double length)
{
    return {from, moved(from, dir, length)};
}

// --- Flecha de la medida que se edita -----------------------------------------------

bool FeatureTool::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::FocusIn || event->type() == QEvent::FocusOut) {
        for (const auto& [key, box] : arrowBoxes_) {
            if (watched != box && watched != lineOf(box)) {
                continue;
            }
            if (event->type() == QEvent::FocusIn) {
                focusedKey_ = key;
            } else if (focusedKey_ == key) {
                focusedKey_.clear();
            }
            // Al pasar de un cuadro a otro el foco sale de uno y entra en el otro: se decide cuando ya asentó.
            QTimer::singleShot(0, this, [this] { refreshArrow(); });
            break;
        }
    }
    return QObject::eventFilter(watched, event);
}

void FeatureTool::refreshArrow()
{
    if (!panel_) {
        return;
    }
    // Lo que tiene el foco ahora manda (el cuadro enfocado puede haber cambiado sin que llegara su evento).
    std::string key;
    for (const auto& [name, box] : arrowBoxes_) {
        if (box->hasFocus() || (lineOf(box) != nullptr && lineOf(box)->hasFocus())) {
            key = name;
            break;
        }
    }
    focusedKey_ = key;
    if (!key.empty() && !updating_) {
        try {
            if (const auto arrow = dimensionArrow(key)) {
                viewport_.showDimensionArrow(arrow->first, arrow->second);
                return;
            }
            if (const auto arc = dimensionArc(key)) {
                viewport_.showDimensionArc(arc->center, arc->axis, arc->start, arc->angle);
                return;
            }
        } catch (const std::exception&) {
            // Sin datos para dibujarla: se quita.
        }
    }
    viewport_.hideDimensionArrow();
}

} // namespace nuf::app
