#include "SketchSession.hpp"

#include <nuf/render/Viewport.hpp>
#include <nuf/sketch/Pick.hpp>

#include <algorithm>

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
    if (tool_ && !hovered_.empty()) {
        hovered_ = {};
        showSketch();
    }
    showPreview();
    emit toolChanged(tool);
    emit hintChanged(tool_ ? QString::fromStdString(tool_->hint()) : QString());
}

bool SketchSession::cancel()
{
    if (!tool_) {
        if (selection_.empty()) {
            return false;
        }
        selection_.clear();
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
    // Sin herramienta, se resalta lo que se seleccionaría con un clic.
    const sketch::Ref hovered =
        (!tool_ && hasPoint) ? sketch::pick(sketch_, {x, y}, kSnapRadiusPx * viewport_.sketchPixelSize())
                             : sketch::Ref{};
    if (hovered.entity != hovered_.entity || hovered.part != hovered_.part) {
        hovered_ = hovered;
        showSketch();
    }
    showPreview();
}

void SketchSession::clicked(double x, double y, Qt::KeyboardModifiers modifiers)
{
    if (!tool_) {
        // Clic: selecciona; Shift+clic: agrega o quita; clic en vacío: limpia.
        const sketch::Ref picked = sketch::pick(sketch_, {x, y}, kSnapRadiusPx * viewport_.sketchPixelSize());
        const auto same = [&](sketch::Ref r) { return r.entity == picked.entity && r.part == picked.part; };
        if (modifiers & Qt::ShiftModifier) {
            if (!picked.empty() && std::erase_if(selection_, same) == 0) {
                selection_.push_back(picked);
            }
        } else {
            selection_.clear();
            if (!picked.empty()) {
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
    if (ids.empty()) {
        return;
    }
    remember(sketch_);
    for (sketch::EntityId id : ids) {
        sketch_.removeEntity(id);
    }
    selection_.clear();
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
    if (std::erase_if(selection_, gone) > 0) {
        emit selectionChanged();
    }
    if (gone(hovered_)) {
        hovered_ = {};
    }
}

void SketchSession::doubleClicked()
{
    // El primer clic ya colocó el punto; el doble clic termina la cadena.
    if (tool_ && tool_->cancel()) {
        showPreview();
        emit hintChanged(QString::fromStdString(tool_->hint()));
    }
}

void SketchSession::showSketch()
{
    viewport_.showSketch(sketch_, underconstrained_, {selection_, hovered_});
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
