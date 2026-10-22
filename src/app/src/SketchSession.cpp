#include "SketchSession.hpp"

#include <nuf/render/Viewport.hpp>

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
    showPreview();
    emit toolChanged(tool);
    emit hintChanged(tool_ ? QString::fromStdString(tool_->hint()) : QString());
}

bool SketchSession::cancel()
{
    if (!tool_) {
        return false;
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
    showPreview();
}

void SketchSession::clicked(double x, double y)
{
    if (!tool_) {
        return;
    }
    const sketch::Snap s = snapAt({x, y});
    tool_->move(s);
    if (tool_->click(sketch_, s)) {
        solveAndShow();
    }
    showPreview();
    emit hintChanged(QString::fromStdString(tool_->hint()));
}

void SketchSession::doubleClicked()
{
    // El primer clic ya colocó el punto; el doble clic termina la cadena.
    if (tool_ && tool_->cancel()) {
        showPreview();
        emit hintChanged(QString::fromStdString(tool_->hint()));
    }
}

void SketchSession::solveAndShow()
{
    const sketch::SolveResult result = solver_->solve(sketch_);
    viewport_.showSketch(sketch_, result.underconstrained);

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
