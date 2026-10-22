#include "SketchSession.hpp"
#include "ValueInput.hpp"

#include <nuf/render/Viewport.hpp>
#include <nuf/sketch/Annotations.hpp>
#include <nuf/sketch/FromSelection.hpp>
#include <nuf/sketch/Pick.hpp>

#include <QCursor>
#include <QGuiApplication>
#include <QInputDialog>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace nuf::app {

namespace {

// Distancia, en píxeles, a la que el cursor se ajusta a un punto.
constexpr double kSnapRadiusPx = 8.0;

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

SketchSession::SketchSession(render::Viewport& viewport, const sketch::Plane& plane, QObject* parent)
    : QObject(parent)
    , viewport_(viewport)
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
    valueInput_ = new ValueInput(&viewport_);
    connect(valueInput_, &ValueInput::accepted, this, &SketchSession::applyTypedValues);
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
    }
    toolKind_ = tool;
    viewport_.setSketchToolActive(tool_ != nullptr);
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
    emit hintChanged(tool_ ? QString::fromStdString(tool_->hint()) : QString());
}

bool SketchSession::cancel()
{
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
    const double px = viewport_.sketchPixelSize();
    return sketch::pickAnnotation(sketch::layoutAnnotations(sketch_, px), point, px);
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
    selection_.clear();
    emit selectionChanged();
    solveAndShow();
}

bool SketchSession::askDimensionValue(sketch::Constraint& dimension)
{
    const bool angle = dimension.type == sketch::ConstraintType::Angle;
    const double shown = angle ? std::abs(dimension.value) * 180.0 / std::numbers::pi : std::abs(dimension.value);
    bool ok = false;
    const double value = QInputDialog::getDouble(&viewport_, QStringLiteral("Cota"),
                                                 angle ? QStringLiteral("Ángulo (°):") : QStringLiteral("Valor (mm):"),
                                                 shown, angle ? 0.0 : 1e-6, angle ? 360.0 : 1e6, 3, &ok);
    if (!ok) {
        return false;
    }
    // Se conserva el signo (sentido) de la medida actual.
    const double sign = dimension.value < 0.0 ? -1.0 : 1.0;
    dimension.value = sign * (angle ? value * std::numbers::pi / 180.0 : value);
    return true;
}

void SketchSession::addDimension()
{
    std::optional<sketch::Constraint> dimension = sketch::dimensionFromSelection(sketch_, selection_);
    if (!dimension) {
        emit message(QStringLiteral("Cota: selecciona una línea, un círculo, un arco, dos puntos, "
                                    "un punto y una línea, o dos líneas"));
        return;
    }
    if (!askDimensionValue(*dimension)) {
        return;
    }
    sketch::Sketch before = sketch_;
    const sketch::ConstraintId id = sketch_.addConstraint(*dimension);
    sketch::SolveResult result = solver_->solve(sketch_);
    if (result.status != sketch::SolveStatus::Solved || contains(result.redundant, id)) {
        // La medida ya la fijan otras restricciones: cota de referencia.
        sketch_ = before;
        dimension->driving = false;
        sketch_.addConstraint(*dimension);
        emit message(QStringLiteral("La medida ya está determinada: se añadió como cota de referencia"));
    }
    remember(std::move(before));
    selection_.clear();
    emit selectionChanged();
    solveAndShow();
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
    viewport_.showSketch(sketch_, underconstrained_, {selection_, hovered_, selectedConstraints_, hoveredConstraint_});
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

void SketchSession::applyTypedValues()
{
    if (!tool_) {
        return;
    }
    sketch::Sketch before = sketch_;
    if (tool_->applyValues(sketch_, valueInput_->values())) {
        remember(std::move(before));
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
    valueInput_->setFields(labels, tool_->currentValues());
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
    viewport_.showSketchPreview(geometry, marker);
}

} // namespace nuf::app
