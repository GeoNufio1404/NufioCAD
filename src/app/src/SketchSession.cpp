#include "SketchSession.hpp"

#include <nuf/render/Viewport.hpp>
#include <nuf/sketch/Annotations.hpp>
#include <nuf/sketch/FromSelection.hpp>
#include <nuf/sketch/Pick.hpp>

#include <QGuiApplication>
#include <QInputDialog>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace nuf::app {

namespace {

// Distancia, en píxeles, a la que el cursor se ajusta a un punto.
constexpr double kSnapRadiusPx = 8.0;

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
    solveAndShow();
}

SketchSession::~SketchSession()
{
    viewport_.setSketchToolActive(false);
    viewport_.endSketch();
}

void SketchSession::setTool(Tool tool)
{
    switch (tool) {
    case Tool::None: tool_.reset(); break;
    case Tool::Line: tool_ = sketch::makeLineTool(); break;
    case Tool::Rectangle: tool_ = sketch::makeRectangleTool(); break;
    case Tool::Circle: tool_ = sketch::makeCircleTool(); break;
    case Tool::Arc: tool_ = sketch::makeArcTool(); break;
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
    if (hovered.entity != hovered_.entity || hovered.part != hovered_.part || hoveredConstraint != hoveredConstraint_) {
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
            const auto same = [&](sketch::Ref r) { return r.entity == picked.entity && r.part == picked.part; };
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
    showPreview();
    emit hintChanged(QString::fromStdString(tool_->hint()));
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
    // El primer clic ya colocó el punto; el doble clic termina la cadena.
    if (tool_->cancel()) {
        showPreview();
        emit hintChanged(QString::fromStdString(tool_->hint()));
    }
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

void SketchSession::showPreview()
{
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
