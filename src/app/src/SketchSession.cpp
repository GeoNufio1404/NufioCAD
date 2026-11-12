#include "SketchSession.hpp"
#include "ExpressionSpinBox.hpp"
#include "Units.hpp"
#include "ValueInput.hpp"

#include <nuf/render/Viewport.hpp>
#include <nuf/sketch/Annotations.hpp>
#include <nuf/sketch/FromSelection.hpp>
#include <nuf/sketch/Pick.hpp>

#include <nuf/sketch/Dxf.hpp>
#include <nuf/sketch/Edit.hpp>

#include <QCursor>
#include <QFileDialog>
#include <QGuiApplication>
#include <QImage>
#include <QDialog>
#include <QEventLoop>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QScreen>
#include <QTimer>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFontComboBox>
#include <QFormLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>

#include <fstream>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace nuf::app {

namespace {

// Teclas del campo de valor sobre el lienzo: Enter acepta, Esc cancela; al cerrarse el recuadro (también al hacer
// clic fuera de él) termina la espera.
class InlineEditorKeys final : public QObject {
public:
    InlineEditorKeys(QWidget* popup, QEventLoop* loop, bool* accepted)
        : QObject(popup)
        , popup_(popup)
        , loop_(loop)
        , accepted_(accepted)
    {
    }

    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() == QEvent::KeyPress) {
            const int key = static_cast<QKeyEvent*>(event)->key();
            if (key == Qt::Key_Return || key == Qt::Key_Enter) {
                *accepted_ = true;
                popup_->close();
                return true;
            }
            if (key == Qt::Key_Escape) {
                popup_->close();
                return true;
            }
        } else if (event->type() == QEvent::Hide && watched == popup_) {
            loop_->quit();
        }
        return false;
    }

private:
    QWidget* popup_;
    QEventLoop* loop_;
    bool* accepted_;
};

QString dimensionCaption(sketch::ConstraintType type)
{
    switch (type) {
    case sketch::ConstraintType::Diameter: return QStringLiteral("Ø");
    case sketch::ConstraintType::Radius: return QStringLiteral("R");
    case sketch::ConstraintType::Angle: return QStringLiteral("Ángulo");
    case sketch::ConstraintType::MajorRadius: return QStringLiteral("Semieje mayor");
    case sketch::ConstraintType::MinorRadius: return QStringLiteral("Semieje menor");
    case sketch::ConstraintType::Distance: return QStringLiteral("Distancia");
    default: return QStringLiteral("Valor");
    }
}

} // namespace

namespace {

// Distancia, en píxeles, a la que el cursor se ajusta a un punto.
constexpr double kSnapRadiusPx = 8.0;
// A cuántos píxeles de la recta horizontal o vertical de un punto el cursor se
// pega a ella (alineación inferida, la «línea naranja»).
constexpr double kAlignRadiusPx = 6.0;
constexpr int kAwakeDwellMs = 300;  // lo que hay que esperar sobre un punto para despertarlo
constexpr double kAlignReleasePx = 10.0;  // histéresis: se suelta más lejos de lo que se pega

sketch::Geometry translated(sketch::Geometry geometry, sketch::Vec2 d)
{
    const auto move = [&](sketch::Vec2& p) {
        p.x += d.x;
        p.y += d.y;
    };
    if (auto* el = std::get_if<sketch::Ellipse>(&geometry)) {
        move(el->center);
    } else if (auto* ea = std::get_if<sketch::EllipseArc>(&geometry)) {
        move(ea->ellipse.center);
    } else if (auto* b = std::get_if<sketch::BSpline>(&geometry)) {
        for (sketch::Vec2& pole : b->poles) {
            move(pole);
        }
    }
    return geometry;
}

} // namespace

SketchSession::SketchSession(render::Viewport& viewport, const sketch::Plane& plane,
                             std::optional<sketch::Sketch> initial, QObject* parent)
    : QObject(parent)
    , viewport_(viewport)
    , sketch_(initial ? std::move(*initial) : sketch::Sketch{})
    , plane_(plane)
    , solver_(sketch::makePlaneGcsSolver())
{
    viewport_.beginSketch(plane_);
    connect(&viewport_, &render::Viewport::sketchCursorMoved, this, &SketchSession::cursorMoved);
    connect(&viewport_, &render::Viewport::sketchClicked, this, &SketchSession::clicked);
    connect(&viewport_, &render::Viewport::sketchDoubleClicked, this, &SketchSession::doubleClicked);
    connect(&viewport_, &render::Viewport::sketchPressed, this, &SketchSession::pressed);
    connect(&viewport_, &render::Viewport::sketchDragged, this, &SketchSession::dragged);
    connect(&viewport_, &render::Viewport::sketchDragFinished, this, &SketchSession::dragFinished);
    connect(&viewport_, &render::Viewport::sketchTyped, this, &SketchSession::typed);
    connect(&viewport_, &render::Viewport::sketchEnterPressed, this, &SketchSession::finishShape);
    connect(&viewport_, &render::Viewport::sketchToolDragged, this, &SketchSession::toolDragged);
    connect(&viewport_, &render::Viewport::sketchToolDragReleased, this, &SketchSession::toolDragReleased);
    connect(&viewport_, &render::Viewport::sketchEdgeProjected, this, &SketchSession::addFromModel);
    connect(&viewport_, &render::Viewport::sketchBoxSelected, this, &SketchSession::boxSelected);
    valueInput_ = new ValueInput(&viewport_);
    connect(valueInput_, &ValueInput::accepted, this, &SketchSession::applyTypedValues);
}

void SketchSession::start()
{
    solveAndShow();
}

SketchSession::~SketchSession()
{
    // Primero se desconecta: endSketch() emite señales que ya no deben
    // llegar a esta sesión a medio destruir.
    disconnect(&viewport_, nullptr, this, nullptr);
    viewport_.setSketchToolActive(false);
    viewport_.endSketch();
    delete valueInput_;
}

void SketchSession::setTool(Tool tool)
{
    // Las herramientas que actúan sobre la selección la necesitan ya.
    const bool needsSelection = tool == Tool::Mirror || tool == Tool::Move || tool == Tool::Copy
                                || tool == Tool::Rotate || tool == Tool::Scale || tool == Tool::PatternRectangular
                                || tool == Tool::PatternCircular;
    awake_.clear();
    const std::vector<sketch::EntityId> selected = selectedEntities();
    if (needsSelection && selected.empty()) {
        emit message(QStringLiteral("Selecciona primero lo que quieras transformar"));
        tool = Tool::None;
    }
    switch (tool) {
    case Tool::None: tool_.reset(); break;
    case Tool::Line: tool_ = sketch::makeLineTool(); break;
    case Tool::Rectangle: tool_ = sketch::makeRectangleTool(); break;
    case Tool::Circle: tool_ = sketch::makeCircleTool(); break;
    case Tool::Arc: tool_ = sketch::makeArcTool(); break;
    case Tool::Rectangle3Point: tool_ = sketch::makeThreePointRectangleTool(); break;
    case Tool::RectangleCenter: tool_ = sketch::makeCenterRectangleTool(); break;
    case Tool::Circle2Point: tool_ = sketch::makeTwoPointCircleTool(); break;
    case Tool::Circle3Point: tool_ = sketch::makeThreePointCircleTool(); break;
    case Tool::CircleTangent2: tool_ = sketch::makeTangentCircleTool(2); break;
    case Tool::CircleTangent3: tool_ = sketch::makeTangentCircleTool(3); break;
    case Tool::ArcCenter: tool_ = sketch::makeCenterArcTool(); break;
    case Tool::ArcTangent: tool_ = sketch::makeTangentArcTool(); break;
    case Tool::PolygonCircumscribed: tool_ = sketch::makePolygonTool(sketch::PolygonMode::Circumscribed); break;
    case Tool::PolygonInscribed: tool_ = sketch::makePolygonTool(sketch::PolygonMode::Inscribed); break;
    case Tool::PolygonEdge: tool_ = sketch::makePolygonTool(sketch::PolygonMode::Edge); break;
    case Tool::SlotCenterToCenter: tool_ = sketch::makeSlotTool(sketch::SlotMode::CenterToCenter); break;
    case Tool::SlotOverall: tool_ = sketch::makeSlotTool(sketch::SlotMode::Overall); break;
    case Tool::SlotCenterPoint: tool_ = sketch::makeSlotTool(sketch::SlotMode::CenterPoint); break;
    case Tool::SlotArc: tool_ = sketch::makeArcSlotTool(); break;
    case Tool::Ellipse: tool_ = sketch::makeEllipseTool(); break;
    case Tool::EllipseArc: tool_ = sketch::makeEllipseArcTool(); break;
    case Tool::SplineFit: tool_ = sketch::makeSplineTool(sketch::SplineMode::FitPoints); break;
    case Tool::SplineControl: tool_ = sketch::makeSplineTool(sketch::SplineMode::ControlPoints); break;
    case Tool::Conic: tool_ = sketch::makeConicTool(); break;
    case Tool::Trim: tool_ = sketch::makeCurveEditTool(sketch::CurveEdit::Trim); break;
    case Tool::Extend: tool_ = sketch::makeCurveEditTool(sketch::CurveEdit::Extend); break;
    case Tool::Break: tool_ = sketch::makeCurveEditTool(sketch::CurveEdit::Break); break;
    case Tool::Fillet: tool_ = sketch::makeFilletTool(false); break;
    case Tool::Chamfer: tool_ = sketch::makeFilletTool(true); break;
    case Tool::Offset: tool_ = sketch::makeOffsetTool(); break;
    case Tool::Mirror: tool_ = sketch::makeMirrorTool(sketch_, selected); break;
    case Tool::Move: tool_ = sketch::makeMoveTool(sketch_, selected, false); break;
    case Tool::Copy: tool_ = sketch::makeMoveTool(sketch_, selected, true); break;
    case Tool::Rotate: tool_ = sketch::makeRotateTool(sketch_, selected); break;
    case Tool::Scale: tool_ = sketch::makeScaleTool(sketch_, selected); break;
    case Tool::PatternRectangular: tool_ = sketch::makeRectangularPatternTool(sketch_, selected); break;
    case Tool::PatternCircular: tool_ = sketch::makeCircularPatternTool(sketch_, selected); break;
    case Tool::Project: tool_.reset(); break;  // lo resuelve el viewport (clic en aristas)
    case Tool::Text:
    case Tool::Image: tool_.reset(); break;   // un clic y diálogos (ver clicked)
    case Tool::Dimension:
        tool_.reset();  // un clic en lo que se acota (ver dimensionClicked)
        selection_.clear();
        selectedConstraints_.clear();
        showSketch();
        emit selectionChanged();
        break;
    }
    toolKind_ = tool;
    viewport_.setSketchProjectMode(tool == Tool::Project);
    const bool clickTool = tool == Tool::Project || tool == Tool::Text || tool == Tool::Image || tool == Tool::Dimension;
    viewport_.setSketchToolActive(tool_ != nullptr || clickTool);
    if (tool_ && cursor_) {
        tool_->move(*cursor_);
    }
    if (tool_ && (!hovered_.empty() || hoveredConstraint_ != 0)) {
        hovered_ = {};
        hoveredConstraint_ = 0;
        showSketch();
    }
    showPreview();
    emit toolChanged(tool);
    if (tool == Tool::Project) {
        emit hintChanged(QStringLiteral("Clic en aristas de la pieza para proyectarlas en el sketch · Esc para terminar"));
    } else if (tool == Tool::Text) {
        emit hintChanged(QStringLiteral("Clic donde empieza el texto (su línea base)"));
    } else if (tool == Tool::Image) {
        emit hintChanged(QStringLiteral("Clic donde va la esquina inferior izquierda de la imagen"));
    } else if (tool == Tool::Dimension) {
        emit hintChanged(QStringLiteral("Cota: clic en un círculo, arco o línea y escribe el valor · dos clics para una distancia o un ángulo · "
                                        "clic en una cota para editarla · Esc para terminar"));
    } else {
        emit hintChanged(tool_ ? QString::fromStdString(tool_->hint()) : QString());
    }
}

bool SketchSession::cancel()
{
    if (toolKind_ == Tool::Dimension) {
        if (hasSelection()) {
            selection_.clear();
            selectedConstraints_.clear();
            showSketch();
            emit selectionChanged();
        } else {
            setTool(Tool::None);
        }
        return true;
    }
    if (toolKind_ == Tool::Project || toolKind_ == Tool::Text || toolKind_ == Tool::Image) {
        setTool(Tool::None);
        return true;
    }
    if (valueInput_->isTyping()) {
        valueInput_->clear();
        viewport_.setFocus();
        return true;
    }
    if (!tool_) {
        if (!hasSelection()) {
            return false;
        }
        selection_.clear();
        selectedConstraints_.clear();
        showSketch();
        emit selectionChanged();
        return true;
    }
    if (tool_->cancel()) {
        showPreview();
        emit hintChanged(QString::fromStdString(tool_->hint()));
    } else {
        setTool(Tool::None);
    }
    return true;
}

sketch::Snap SketchSession::snapAt(sketch::Vec2 point) const
{
    sketch::SnapOptions options;
    options.tolerance = kSnapRadiusPx * viewport_.sketchPixelSize();
    options.gridStep = viewport_.sketchGridStep();
    // Con Ctrl se dibuja libre: sin snap ni restricciones automáticas.
    options.enabled = !(QGuiApplication::keyboardModifiers() & Qt::ControlModifier);
    // Mayús suprime las inferencias (como en Onshape). Solo con una
    // herramienta de dibujo: sin ella, Mayús es para seleccionar.
    if (tool_ && !(QGuiApplication::keyboardModifiers() & Qt::ShiftModifier)) {
        options.alignTolerance = kAlignRadiusPx * viewport_.sketchPixelSize();
        options.alignRelease = kAlignReleasePx * viewport_.sketchPixelSize();
        if (cursor_) {
            options.stickyX = cursor_->alignX;
            options.stickyY = cursor_->alignY;
        }
        options.alignPoints.push_back({{0.0, 0.0}, sketch::Ref::of(sketch::Sketch::kOrigin)});
        for (const sketch::Ref& ref : awake_) {
            if (const auto position = sketch_.point(ref)) {
                options.alignPoints.push_back({*position, ref});
            }
        }
    }
    return sketch::snap(sketch_, point, options);
}

void SketchSession::cursorMoved(bool hasPoint, double x, double y)
{
    if (!hasPoint) {
        cursor_.reset();
    } else {
        cursor_ = snapAt({x, y});
        if (tool_) {
            tool_->move(*cursor_);
            // Quedarse un momento (0,3 s) sobre un punto lo «despierta»: luego se
            // puede alinear con él desde lejos. Se recuerdan los dos últimos, y
            // pasar de largo no despierta nada.
            const bool overPoint = (cursor_->kind == sketch::SnapKind::Point || cursor_->kind == sketch::SnapKind::Center)
                                   && cursor_->ref.entity != sketch::Sketch::kOrigin
                                   && std::ranges::find(awake_, cursor_->ref) == awake_.end();
            if (!overPoint) {
                dwellRef_ = {};
            } else if (!(dwellRef_ == cursor_->ref)) {
                dwellRef_ = cursor_->ref;
                QTimer::singleShot(kAwakeDwellMs, this, [this, ref = cursor_->ref] {
                    if (tool_ && cursor_ && dwellRef_ == ref && cursor_->ref == ref
                        && std::ranges::find(awake_, ref) == awake_.end()) {
                        awake_.push_back(ref);
                        if (awake_.size() > 2) {
                            awake_.erase(awake_.begin());
                        }
                        showPreview();
                    }
                });
            }
        }
    }
    // Sin herramienta, se resalta lo que se seleccionaría con un clic (los
    // iconos y cotas antes que la geometría que tienen debajo).
    sketch::ConstraintId hoveredConstraint = 0;
    sketch::Ref hovered;
    if (!tool_ && hasPoint) {
        hoveredConstraint = annotationAt({x, y});
        if (hoveredConstraint == 0) {
            hovered = sketch::pick(sketch_, {x, y}, kSnapRadiusPx * viewport_.sketchPixelSize());
        }
    }
    if (hovered != hovered_ || hoveredConstraint != hoveredConstraint_) {
        hovered_ = hovered;
        hoveredConstraint_ = hoveredConstraint;
        showSketch();
    }
    showPreview();
}

void SketchSession::clicked(double x, double y, Qt::KeyboardModifiers modifiers)
{
    if (toolKind_ == Tool::Text) {
        placeText({x, y});
        return;
    }
    if (toolKind_ == Tool::Image) {
        placeImage({x, y});
        return;
    }
    if (toolKind_ == Tool::Dimension) {
        dimensionClicked({x, y});
        return;
    }
    if (!tool_) {
        // Clic: selecciona; Shift+clic: agrega o quita; clic en vacío: limpia.
        const bool add = modifiers & Qt::ShiftModifier;
        if (!add) {
            selection_.clear();
            selectedConstraints_.clear();
        }
        if (const sketch::ConstraintId constraint = annotationAt({x, y})) {
            if (std::erase(selectedConstraints_, constraint) == 0) {
                selectedConstraints_.push_back(constraint);
            }
        } else {
            const sketch::Ref picked = sketch::pick(sketch_, {x, y}, kSnapRadiusPx * viewport_.sketchPixelSize());
            const auto same = [&](sketch::Ref r) { return r == picked; };
            if (!picked.empty() && std::erase_if(selection_, same) == 0) {
                selection_.push_back(picked);
            }
        }
        showSketch();
        emit selectionChanged();
        return;
    }
    const sketch::Snap s = snapAt({x, y});
    tool_->move(s);
    sketch::Sketch before = sketch_;
    if (tool_->click(sketch_, s)) {
        remember(std::move(before));
        solveAndShow();
    }
    afterToolChange();
}

void SketchSession::addFromModel(const std::vector<sketch::Geometry>& geometry, const geometry::ElementRef& source)
{
    if (geometry.empty()) {
        return;
    }
    // Bloqueada y vinculada a la arista original: se actualiza con ella al
    // recalcular el modelo (desbloquearla rompe el vínculo).
    remember(sketch_);
    for (std::size_t i = 0; i < geometry.size(); ++i) {
        sketch::Entity* e = sketch_.entity(sketch_.addGeometry(geometry[i]));
        e->locked = true;
        if (!source.empty()) {
            e->link = sketch::ModelLink{source.name, source.occurrence, int(i)};
        }
    }
    solveAndShow();
}

void SketchSession::importDxf(const QString& path)
{
    std::ifstream in(path.toStdString());
    if (!in) {
        emit message(QStringLiteral("No se pudo abrir %1").arg(path));
        return;
    }
    const sketch::DxfDrawing drawing = sketch::readDxf(in);
    if (drawing.geometry.empty()) {
        emit message(QStringLiteral("El DXF no tiene geometría que se pueda importar"));
        return;
    }
    remember(sketch_);
    const auto created = sketch::importDrawing(sketch_, drawing);
    solveAndShow();
    QString text = QStringLiteral("Importadas %1 entidades").arg(created.size());
    if (!drawing.skipped.empty()) {
        QStringList skipped;
        for (const std::string& type : drawing.skipped) {
            skipped << QString::fromStdString(type);
        }
        text += QStringLiteral(" (se omitieron: %1)").arg(skipped.join(QStringLiteral(", ")));
    }
    emit message(text);
}

void SketchSession::placeText(sketch::Vec2 at)
{
    // Un solo diálogo con el texto, la fuente y la altura; las letras se ven
    // en el sketch mientras se escribe, para saber cómo quedarán.
    QDialog dialog(&viewport_);
    dialog.setWindowTitle(QStringLiteral("Texto"));
    auto* form = new QFormLayout(&dialog);
    auto* textEdit = new QLineEdit(&dialog);
    auto* fontBox = new QFontComboBox(&dialog);
    fontBox->setFontFilters(QFontComboBox::ScalableFonts);
    if (!textFamily_.isEmpty()) {
        fontBox->setCurrentFont(QFont(textFamily_));
    }
    auto* heightBox = new ExpressionSpinBox(&dialog);
    heightBox->setRange(0.1, 1000.0);
    heightBox->setDecimals(2);
    heightBox->setLength(true);
    heightBox->setValue(textHeight_);
    auto* size = new QLabel(&dialog);
    form->addRow(QStringLiteral("Texto"), textEdit);
    form->addRow(QStringLiteral("Fuente"), fontBox);
    form->addRow(QStringLiteral("Altura"), heightBox);
    form->addRow(QStringLiteral("Tamaño"), size);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    std::vector<sketch::Geometry> outline;
    const auto refresh = [&] {
        outline = textEdit->text().trimmed().isEmpty()
                      ? std::vector<sketch::Geometry>{}
                      : viewport_.textOutline(textEdit->text(), heightBox->value(), fontBox->currentFont().family());
        // Vista previa en su sitio y su tamaño total, como referencia.
        std::vector<sketch::Geometry> placed;
        double minX = 1e300, minY = 1e300, maxX = -1e300, maxY = -1e300;
        const sketch::Similarity move{{}, 0.0, 1.0, at};
        for (const sketch::Geometry& g : outline) {
            placed.push_back(sketch::transformed(g, move));
            for (const sketch::Vec2& p : sketch::polyline(placed.back())) {
                minX = std::min(minX, p.x);
                maxX = std::max(maxX, p.x);
                minY = std::min(minY, p.y);
                maxY = std::max(maxY, p.y);
            }
        }
        viewport_.showSketchPreview(placed, at);
        size->setText(placed.empty() ? QStringLiteral("—")
                                     : QStringLiteral("%1 × %2 mm")
                                           .arg(QLocale().toString(maxX - minX, 'f', 1),
                                                QLocale().toString(maxY - minY, 'f', 1)));
    };
    connect(textEdit, &QLineEdit::textChanged, &dialog, refresh);
    connect(fontBox, &QFontComboBox::currentFontChanged, &dialog, refresh);
    connect(heightBox, &QDoubleSpinBox::valueChanged, &dialog, refresh);
    // Junto al cursor, sin tapar el punto donde empieza el texto.
    dialog.adjustSize();
    dialog.move(QCursor::pos() + QPoint(40, 40));
    refresh();
    const bool accepted = dialog.exec() == QDialog::Accepted;
    viewport_.showSketchPreview({}, std::nullopt);
    if (!accepted || outline.empty()) {
        if (accepted) {
            emit message(QStringLiteral("No se pudo crear el texto con esa fuente"));
        }
        return;
    }
    textHeight_ = heightBox->value();
    textFamily_ = fontBox->currentFont().family();
    // Las letras quedan como curvas unidas y fijas (se mueven juntas con la
    // herramienta Mover; se pueden extruir o grabar).
    remember(sketch_);
    for (sketch::EntityId id : sketch::importDrawing(sketch_, {outline, {}}, at)) {
        sketch_.entity(id)->locked = true;
    }
    solveAndShow();
}

void SketchSession::placeImage(sketch::Vec2 at)
{
    const QString path = QFileDialog::getOpenFileName(&viewport_, QStringLiteral("Imagen de referencia"), QString(),
                                                      QStringLiteral("Imágenes (*.png *.jpg *.jpeg *.bmp)"));
    if (path.isEmpty()) {
        return;
    }
    const QImage image(path);
    if (image.isNull()) {
        emit message(QStringLiteral("No se pudo leer la imagen"));
        return;
    }
    bool ok = false;
    const double shownWidth = QInputDialog::getDouble(
        &viewport_, QStringLiteral("Imagen de referencia"), QStringLiteral("Ancho (%1):").arg(Units::instance().unit()),
        Units::instance().toDisplay(100.0), Units::instance().toDisplay(0.1), Units::instance().toDisplay(100000.0),
        2 + Units::instance().extraDecimals(), &ok);
    if (!ok) {
        return;
    }
    const double width = Units::instance().fromDisplay(shownWidth);
    const double height = width * image.height() / std::max(1, image.width());
    viewport_.showSketchImage(path, at, width, height);
    setTool(Tool::None);
}

void SketchSession::boxSelected(double x0, double y0, double x1, double y1)
{
    // Ventana: entra lo que queda dentro del todo.
    const double minX = std::min(x0, x1), maxX = std::max(x0, x1);
    const double minY = std::min(y0, y1), maxY = std::max(y0, y1);
    const auto inside = [&](sketch::Vec2 p) { return p.x >= minX && p.x <= maxX && p.y >= minY && p.y <= maxY; };
    for (const sketch::Entity& e : sketch_.entities()) {
        if (e.reference) {
            continue;
        }
        bool all = true;
        if (const auto* point = std::get_if<sketch::Point>(&e.geometry)) {
            all = inside(point->position);
        } else {
            for (const sketch::Vec2& p : sketch::polyline(e.geometry)) {
                all = all && inside(p);
            }
        }
        if (all && std::ranges::find(selection_, sketch::Ref::of(e.id)) == selection_.end()) {
            selection_.push_back(sketch::Ref::of(e.id));
        }
    }
    showSketch();
    emit selectionChanged();
}

void SketchSession::intersectModel()
{
    const std::vector<sketch::Geometry> section = viewport_.sketchSection();
    if (section.empty()) {
        emit message(QStringLiteral("El plano del sketch no corta la pieza"));
        return;
    }
    addFromModel(section, {});  // la sección no tiene una arista de origen
}

void SketchSession::afterToolChange()
{
    // Las herramientas de una sola vez (sobre la selección) se cierran solas.
    if (tool_ && tool_->done()) {
        selection_.clear();
        emit selectionChanged();
        setTool(Tool::None);
        showSketch();
        return;
    }
    showPreview();
    if (tool_) {
        emit hintChanged(QString::fromStdString(tool_->hint()));
    }
}

std::vector<sketch::EntityId> SketchSession::selectedEntities() const
{
    std::vector<sketch::EntityId> ids;
    for (const sketch::Ref& r : selection_) {
        const sketch::Entity* e = sketch_.entity(r.entity);
        if (e && !e->reference && std::ranges::find(ids, r.entity) == ids.end()) {
            ids.push_back(r.entity);
        }
    }
    return ids;
}

void SketchSession::deleteSelection()
{
    // Un extremo seleccionado borra su entidad. El origen y los ejes no se borran.
    const std::vector<sketch::EntityId> ids = selectedEntities();
    if (ids.empty() && selectedConstraints_.empty()) {
        return;
    }
    remember(sketch_);
    for (sketch::ConstraintId id : selectedConstraints_) {
        sketch_.removeConstraint(id);
    }
    for (sketch::EntityId id : ids) {
        sketch_.removeEntity(id);
    }
    selection_.clear();
    selectedConstraints_.clear();
    emit selectionChanged();
    solveAndShow();
}

void SketchSession::toggleLock()
{
    const std::vector<sketch::EntityId> ids = selectedEntities();
    if (ids.empty()) {
        return;
    }
    remember(sketch_);
    for (sketch::EntityId id : ids) {
        sketch::Entity* e = sketch_.entity(id);
        e->locked = !e->locked;
        if (!e->locked) {
            e->link.reset();
        }
    }
    solveAndShow();
}

void SketchSession::toggleConstruction()
{
    const std::vector<sketch::EntityId> ids = selectedEntities();
    if (ids.empty()) {
        return;
    }
    remember(sketch_);
    for (sketch::EntityId id : ids) {
        sketch::Entity* e = sketch_.entity(id);
        e->construction = !e->construction;
    }
    showSketch();
}

void SketchSession::remember(sketch::Sketch before)
{
    undo_.push_back(std::move(before));
    redo_.clear();
    emit historyChanged();
}

void SketchSession::undo()
{
    if (undo_.empty()) {
        return;
    }
    // Lo que la herramienta tenga a medias puede apuntar a geometría que desaparece.
    if (tool_) {
        tool_->cancel();
    }
    redo_.push_back(std::move(sketch_));
    sketch_ = std::move(undo_.back());
    undo_.pop_back();
    pruneSelection();
    solveAndShow();
    showPreview();
    emit historyChanged();
}

void SketchSession::redo()
{
    if (redo_.empty()) {
        return;
    }
    if (tool_) {
        tool_->cancel();
    }
    undo_.push_back(std::move(sketch_));
    sketch_ = std::move(redo_.back());
    redo_.pop_back();
    pruneSelection();
    solveAndShow();
    showPreview();
    emit historyChanged();
}

void SketchSession::pruneSelection()
{
    const auto gone = [this](const sketch::Ref& r) { return sketch_.entity(r.entity) == nullptr; };
    const auto goneConstraint = [this](sketch::ConstraintId id) { return sketch_.constraint(id) == nullptr; };
    if (std::erase_if(selection_, gone) + std::erase_if(selectedConstraints_, goneConstraint) > 0) {
        emit selectionChanged();
    }
    std::erase_if(pinnedConstraints_, goneConstraint);
    if (goneConstraint(hoveredConstraint_)) {
        hoveredConstraint_ = 0;
    }
    if (gone(hovered_)) {
        hovered_ = {};
    }
}

void SketchSession::doubleClicked(double x, double y)
{
    if (!tool_) {
        // Doble clic sobre una cota: editar su valor.
        if (const sketch::ConstraintId id = annotationAt({x, y})) {
            const sketch::Constraint* c = sketch_.constraint(id);
            if (c && sketch::isDimensional(c->type)) {
                editDimension(id);
            }
        }
        return;
    }
    // El primer clic ya colocó el punto; el doble clic termina la figura
    // (splines) o la cadena (líneas).
    finishShape();
}

void SketchSession::finishShape()
{
    if (!tool_) {
        return;
    }
    sketch::Sketch before = sketch_;
    if (tool_->finish(sketch_)) {
        remember(std::move(before));
        solveAndShow();
    } else {
        tool_->cancel();
    }
    showPreview();
    emit hintChanged(QString::fromStdString(tool_->hint()));
}

void SketchSession::toolDragged(double x0, double y0, double x, double y)
{
    if (tool_ && tool_->dragMove(snapAt({x0, y0}), snapAt({x, y}))) {
        showPreview();
    }
}

void SketchSession::toolDragReleased(double x0, double y0, double x, double y)
{
    if (!tool_) {
        return;
    }
    sketch::Sketch before = sketch_;
    if (tool_->dragRelease(sketch_, snapAt({x0, y0}), snapAt({x, y}))) {
        remember(std::move(before));
        solveAndShow();
    }
    showPreview();
    emit hintChanged(QString::fromStdString(tool_->hint()));
}

void SketchSession::pressed(double x, double y)
{
    if (tool_ || annotationAt({x, y}) != 0) {
        return;
    }
    const sketch::Ref what = sketch::pick(sketch_, {x, y}, kSnapRadiusPx * viewport_.sketchPixelSize());
    const sketch::Entity* e = sketch_.entity(what.entity);
    if (!e || e->reference) {
        return;  // en vacío (o sobre el origen y los ejes) el arrastre orbita
    }
    drag_ = Drag{what, {x, y}, sketch_, false};
    viewport_.grabSketchDrag();
}

void SketchSession::dragged(double x, double y)
{
    if (!drag_) {
        return;
    }
    // Se parte siempre del sketch original: así el arrastre no acumula error.
    const sketch::Sketch& start = drag_->before;
    const sketch::Vec2 delta{x - drag_->grabbedAt.x, y - drag_->grabbedAt.y};
    std::vector<sketch::DragTarget> targets;
    const sketch::Entity* e = start.entity(drag_->what.entity);
    if (start.point(drag_->what)) {
        const sketch::Vec2 p = *start.point(drag_->what);
        targets.push_back({drag_->what, {p.x + delta.x, p.y + delta.y}});
    } else if (const auto* line = std::get_if<sketch::Line>(&e->geometry)) {
        // La línea entera se desplaza.
        targets.push_back({sketch::Ref::start(e->id), {line->start.x + delta.x, line->start.y + delta.y}});
        targets.push_back({sketch::Ref::end(e->id), {line->end.x + delta.x, line->end.y + delta.y}});
    } else if (!std::holds_alternative<sketch::Circle>(e->geometry) && !std::holds_alternative<sketch::Arc>(e->geometry)) {
        // Elipses y splines: se desplazan enteras y después se ajustan a sus
        // restricciones (si no hay solución, se quedan donde estaban).
        sketch::Sketch moved = start;
        moved.entity(e->id)->geometry = translated(e->geometry, delta);
        const sketch::SolveResult result = solver_->solve(moved);
        if (result.status == sketch::SolveStatus::Solved) {
            sketch_ = std::move(moved);
            drag_->moved = true;
            underconstrained_ = result.underconstrained;
            showSketch();
        }
        return;
    } else {
        // Contorno de un círculo o arco: cambia el radio.
        targets.push_back({drag_->what, {x, y}});
    }

    sketch::Sketch moved = start;
    const sketch::SolveResult result = solver_->drag(moved, targets);
    if (result.status != sketch::SolveStatus::Solved) {
        return;  // se queda en la última posición válida
    }
    sketch_ = std::move(moved);
    drag_->moved = true;
    underconstrained_ = result.underconstrained;
    showSketch();
}

void SketchSession::dragFinished()
{
    if (!drag_) {
        return;
    }
    if (drag_->moved) {
        remember(std::move(drag_->before));
        solveAndShow();
    }
    drag_.reset();
}

sketch::ConstraintId SketchSession::annotationAt(sketch::Vec2 point) const
{
    return sketch::pickAnnotation(viewport_.sketchAnnotations(sketch_), point, viewport_.sketchPixelSize());
}

namespace {

bool contains(const std::vector<sketch::ConstraintId>& ids, sketch::ConstraintId id)
{
    return std::ranges::find(ids, id) != ids.end();
}

QString constraintName(sketch::ConstraintType type)
{
    using T = sketch::ConstraintType;
    switch (type) {
    case T::Coincident: return QStringLiteral("Coincidente");
    case T::Horizontal: return QStringLiteral("Horizontal");
    case T::Vertical: return QStringLiteral("Vertical");
    case T::Parallel: return QStringLiteral("Paralela");
    case T::Perpendicular: return QStringLiteral("Perpendicular");
    case T::Tangent: return QStringLiteral("Tangente");
    case T::Equal: return QStringLiteral("Igual");
    case T::Collinear: return QStringLiteral("Colineal");
    case T::Concentric: return QStringLiteral("Concéntrica");
    case T::Midpoint: return QStringLiteral("Punto medio");
    case T::Symmetric: return QStringLiteral("Simetría");
    case T::Fix: return QStringLiteral("Fijar");
    case T::Curvature: return QStringLiteral("Curvatura (G2)");
    default: return QStringLiteral("Restricción");
    }
}

} // namespace

void SketchSession::applyConstraint(sketch::ConstraintType type)
{
    const std::vector<sketch::Constraint> constraints = sketch::constraintsFromSelection(type, sketch_, selection_);
    if (constraints.empty()) {
        emit message(QStringLiteral("%1: la selección no sirve para esta restricción").arg(constraintName(type)));
        return;
    }
    sketch::Sketch before = sketch_;
    std::vector<sketch::ConstraintId> added;
    for (const sketch::Constraint& c : constraints) {
        added.push_back(sketch_.addConstraint(c));
    }
    const sketch::SolveResult result = solver_->solve(sketch_);
    const bool redundant = std::ranges::any_of(added, [&](auto id) { return contains(result.redundant, id); });
    if (result.status != sketch::SolveStatus::Solved || redundant) {
        sketch_ = std::move(before);
        emit message(redundant ? QStringLiteral("%1: ya se cumple por otras restricciones").arg(constraintName(type))
                               : QStringLiteral("%1: entra en conflicto con otras restricciones").arg(constraintName(type)));
        solveAndShow();
        return;
    }
    remember(std::move(before));
    pinnedConstraints_.insert(pinnedConstraints_.end(), added.begin(), added.end());
    selection_.clear();
    emit selectionChanged();
    solveAndShow();
}

bool SketchSession::askDimensionValue(sketch::Constraint& dimension)
{
    const bool angle = dimension.type == sketch::ConstraintType::Angle;
    const Units& units = Units::instance();
    const double shown = angle ? std::abs(dimension.value) * 180.0 / std::numbers::pi : units.toDisplay(std::abs(dimension.value));
    // El campo sale sobre el lienzo, junto al cursor (que está sobre lo que se acota o sobre su cota), como en
    // Onshape: se escribe y Enter acepta; Esc o un clic fuera lo cancelan. Acepta un número o una expresión con
    // parámetros («grosor * 2»); en las longitudes el valor del campo está siempre en milímetros y se muestra en la
    // unidad elegida.
    QFrame popup(&viewport_, Qt::Popup);
    popup.setFrameShape(QFrame::StyledPanel);
    popup.setAutoFillBackground(true);
    auto* row = new QHBoxLayout(&popup);
    row->setContentsMargins(8, 4, 8, 4);
    row->setSpacing(8);
    auto* box = new ExpressionSpinBox(&popup);
    box->setDecimals(3);
    if (angle) {
        box->setRange(0.0, 360.0);
        setUnit(box, QStringLiteral("°"));
        box->setValue(shown);
    } else {
        box->setRange(1e-6, 1e6);
        box->setDecimals(3);
        box->setLength(true);
        box->setValue(std::abs(dimension.value));
    }
    if (!dimension.expression.empty()) {
        box->setExpression(QString::fromStdString(dimension.expression));
    }
    box->setMinimumWidth(120);
    box->setToolTip(QStringLiteral("Un número o una expresión con parámetros (por ejemplo «grosor * 2») · Enter acepta · Esc cancela"));
    row->addWidget(new QLabel(dimensionCaption(dimension.type), &popup));
    row->addWidget(box);

    QEventLoop loop;
    bool accepted = false;
    auto* keys = new InlineEditorKeys(&popup, &loop, &accepted);
    popup.installEventFilter(keys);
    box->installEventFilter(keys);
    if (QLineEdit* edit = box->findChild<QLineEdit*>()) {
        edit->installEventFilter(keys);
    }
    popup.adjustSize();
    QPoint at = QCursor::pos() + QPoint(16, 16);
    if (const QScreen* screen = QGuiApplication::screenAt(QCursor::pos())) {
        const QRect area = screen->availableGeometry();
        at.setX(std::clamp(at.x(), area.left(), std::max(area.left(), area.right() - popup.width())));
        at.setY(std::clamp(at.y(), area.top(), std::max(area.top(), area.bottom() - popup.height())));
    }
    popup.move(at);
    popup.show();
    box->setFocus();
    box->selectAll();
    loop.exec();
    if (!accepted) {
        return false;
    }
    box->interpretText();
    const double value = box->value();
    // Se conserva el signo (sentido) de la medida actual.
    const double sign = dimension.value < 0.0 ? -1.0 : 1.0;
    dimension.value = sign * (angle ? value * std::numbers::pi / 180.0 : value);
    dimension.expression = box->expression().toStdString();
    return true;
}

bool SketchSession::addDimension()
{
    if (selection_.empty() && toolKind_ != Tool::Dimension) {
        // Sin nada elegido: se entra en el modo Cota y se hace clic en lo que se quiere acotar.
        setTool(Tool::Dimension);
        return false;
    }
    std::optional<sketch::Constraint> dimension = sketch::dimensionFromSelection(sketch_, selection_);
    if (!dimension) {
        emit message(QStringLiteral("Cota: selecciona una línea, un círculo, un arco, dos puntos, "
                                    "un punto y una línea, o dos líneas"));
        return false;
    }
    if (!askDimensionValue(*dimension)) {
        return false;
    }
    sketch::Sketch before = sketch_;
    const sketch::ConstraintId id = sketch_.addConstraint(*dimension);
    sketch::SolveResult result = solver_->solve(sketch_);
    if (result.status != sketch::SolveStatus::Solved || contains(result.redundant, id)) {
        // La medida ya la fijan otras restricciones: cota de referencia.
        sketch_ = before;
        dimension->driving = false;
        dimension->expression.clear();
        sketch_.addConstraint(*dimension);
        emit message(QStringLiteral("La medida ya está determinada: se añadió como cota de referencia"));
    }
    remember(std::move(before));
    selection_.clear();
    emit selectionChanged();
    solveAndShow();
    return true;
}

void SketchSession::dimensionClicked(sketch::Vec2 point)
{
    // Un clic sobre una cota existente: se edita su valor.
    if (const sketch::ConstraintId id = annotationAt(point)) {
        const sketch::Constraint* c = sketch_.constraint(id);
        if (c != nullptr && sketch::isDimensional(c->type)) {
            editDimension(id);
        }
        return;
    }
    const sketch::Ref picked = sketch::pick(sketch_, point, kSnapRadiusPx * viewport_.sketchPixelSize());
    if (picked.empty()) {
        // Clic en el vacío: se empieza de nuevo.
        selection_.clear();
        showSketch();
        emit selectionChanged();
        return;
    }
    if (std::erase_if(selection_, [&](sketch::Ref r) { return r == picked; }) == 0) {
        selection_.push_back(picked);
    }
    showSketch();
    emit selectionChanged();
    if (selection_.empty()) {
        return;
    }
    if (!sketch::dimensionFromSelection(sketch_, selection_)) {
        if (selection_.size() == 1) {
            emit message(QStringLiteral("Elige otro elemento para acotar la distancia o el ángulo (un punto con otro punto o con una línea; dos líneas)"));
            return;
        }
        // Esos elementos no se pueden acotar juntos: se empieza de nuevo con el último.
        selection_ = {picked};
        showSketch();
        emit selectionChanged();
        if (!sketch::dimensionFromSelection(sketch_, selection_)) {
            emit message(QStringLiteral("Esos elementos no se pueden acotar juntos"));
            return;
        }
    }
    const bool single = selection_.size() == 1;
    const bool created = addDimension();
    if (!created && single && selection_.size() == 1 && sketch_.entity(selection_[0].entity) != nullptr &&
        !std::holds_alternative<sketch::Line>(sketch_.entity(selection_[0].entity)->geometry)) {
        // Se canceló en un círculo, arco o elipse: no hay con qué emparejarlo, se empieza de nuevo. (Una línea
        // se deja elegida: un segundo clic en otra línea da el ángulo o la distancia entre las dos.)
        selection_.clear();
        showSketch();
        emit selectionChanged();
    }
}

void SketchSession::editDimension(sketch::ConstraintId id)
{
    sketch::Constraint edited = *sketch_.constraint(id);
    if (!edited.driving) {
        emit message(QStringLiteral("Es una cota de referencia: muestra la medida, no se edita"));
        return;
    }
    if (!askDimensionValue(edited)) {
        return;
    }
    sketch::Sketch before = sketch_;
    sketch_.constraint(id)->value = edited.value;
    sketch_.constraint(id)->expression = edited.expression;
    if (solver_->solve(sketch_).status != sketch::SolveStatus::Solved) {
        sketch_ = std::move(before);
        emit message(QStringLiteral("No se puede: con ese valor el sketch no tiene solución"));
        solveAndShow();
        return;
    }
    remember(std::move(before));
    solveAndShow();
}

void SketchSession::showSketch()
{
    viewport_.showSketch(sketch_, underconstrained_, {selection_, hovered_, selectedConstraints_, hoveredConstraint_, pinnedConstraints_});
}

void SketchSession::solveAndShow()
{
    const sketch::SolveResult result = solver_->solve(sketch_);
    underconstrained_ = result.underconstrained;
    showSketch();

    QString status;
    switch (result.status) {
    case sketch::SolveStatus::Conflicting:
        status = QStringLiteral("Restricciones en conflicto");
        break;
    case sketch::SolveStatus::Failed:
        status = QStringLiteral("No se pudo resolver el sketch");
        break;
    case sketch::SolveStatus::Solved:
        status = result.degreesOfFreedom == 0
                     ? QStringLiteral("Totalmente restringido")
                     : (result.degreesOfFreedom == 1
                            ? QStringLiteral("1 grado de libertad")
                            : QStringLiteral("%1 grados de libertad").arg(result.degreesOfFreedom));
        break;
    }
    emit statusChanged(status);
}

void SketchSession::typed(const QString& text)
{
    if (valueInput_->isVisible()) {
        valueInput_->startTyping(text);
    }
}

// Los campos de longitud se escriben en la unidad elegida; los demás (ángulos, cantidades, factores) no.
bool SketchSession::isLengthField(const QString& label)
{
    static const QStringList other = {QStringLiteral("Ángulo"), QStringLiteral("Lados"), QStringLiteral("Copias"),
                                      QStringLiteral("Columnas"), QStringLiteral("Filas"), QStringLiteral("Factor"),
                                      QStringLiteral("Rho")};
    return !other.contains(label);
}

void SketchSession::applyTypedValues()
{
    if (!tool_) {
        return;
    }
    sketch::Sketch before = sketch_;
    std::vector<std::optional<double>> typed = valueInput_->values();
    for (std::size_t i = 0; i < typed.size() && i < valueLabels_.size(); ++i) {
        if (typed[i] && isLengthField(valueLabels_[i])) {
            typed[i] = Units::instance().fromDisplay(*typed[i]);
        }
    }
    if (tool_->applyValues(sketch_, typed)) {
        // Corregir la figura que se acaba de crear no es otro paso: Deshacer
        // vuelve a antes de crearla.
        if (!tool_->amendedLast()) {
            remember(std::move(before));
        }
        solveAndShow();
    }
    valueInput_->clear();
    viewport_.setFocus();
    afterToolChange();
}

// Muestra el recuadro de valores junto al cursor si la herramienta admite
// valores en este paso.
void SketchSession::updateValueInput()
{
    const std::vector<std::string> fields = tool_ ? tool_->valueFields() : std::vector<std::string>{};
    if (fields.empty()) {
        valueInput_->clear();
        valueInput_->hide();
        return;
    }
    std::vector<QString> labels;
    for (const std::string& f : fields) {
        labels.push_back(QString::fromStdString(f));
    }
    valueLabels_ = labels;
    std::vector<double> current = tool_->currentValues();
    for (std::size_t i = 0; i < current.size() && i < labels.size(); ++i) {
        if (isLengthField(labels[i])) {
            current[i] = Units::instance().toDisplay(current[i]);
        }
    }
    valueInput_->setFields(labels, current);
    // Mientras se escribe, el recuadro se queda quieto.
    if (!valueInput_->isTyping()) {
        QPoint at = viewport_.mapFromGlobal(QCursor::pos()) + QPoint(18, 18);
        at.setX(std::clamp(at.x(), 0, std::max(0, viewport_.width() - valueInput_->width())));
        at.setY(std::clamp(at.y(), 0, std::max(0, viewport_.height() - valueInput_->height())));
        valueInput_->move(at);
    }
    valueInput_->show();
    valueInput_->raise();
}

void SketchSession::showPreview()
{
    updateValueInput();
    std::vector<sketch::Geometry> geometry;
    if (tool_) {
        geometry = tool_->preview().geometry;
    }
    // El marcador solo aparece si el cursor se ajustó a algo que no es la
    // rejilla (con herramienta activa).
    std::optional<sketch::Vec2> marker;
    if (tool_ && cursor_ && cursor_->kind != sketch::SnapKind::None && cursor_->kind != sketch::SnapKind::Grid) {
        marker = cursor_->point;
    }
    std::vector<std::pair<sketch::Vec2, sketch::Vec2>> guides;
    std::string badge;
    sketch::Vec2 badgeAt;
    if (tool_) {
        const sketch::Preview preview = tool_->preview();
        for (const sketch::Guide& guide : preview.guides) {
            guides.emplace_back(guide.from, guide.to);
        }
        badge = preview.badge;
        badgeAt = preview.badgeAt;
    }
    if (tool_ && cursor_) {
        for (const sketch::Guide& guide : cursor_->guides) {
            guides.emplace_back(guide.from, guide.to);
        }
    }
    const bool square = marker && cursor_->kind == sketch::SnapKind::Midpoint;
    // Los puntos despiertos (referencia de las alineaciones) llevan un anillo.
    std::vector<sketch::Vec2> awake;
    if (tool_) {
        for (const sketch::Ref& ref : awake_) {
            if (const auto position = sketch_.point(ref)) {
                awake.push_back(*position);
            }
        }
    }
    viewport_.showSketchPreview(geometry, marker, guides, badge, badgeAt, square, awake);
}

} // namespace nuf::app
