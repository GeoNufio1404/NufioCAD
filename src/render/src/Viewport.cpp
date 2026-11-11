#include <QColor>
#include <QCursor>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include "ViewportImpl.hpp"

namespace nuf::render {

namespace {

// Cruz con núcleo blanco y contorno oscuro: se ve sobre el fondo gris, el
// claro y el oscuro (la del sistema es negra y se pierde en el gris).
QCursor crossCursor()
{
    constexpr int size = 33;
    constexpr int mid = size / 2;
    constexpr int gap = 3;  // hueco en el centro para ver el punto exacto
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    const auto cross = [&](const QPen& pen) {
        painter.setPen(pen);
        painter.drawLine(0, mid, mid - gap, mid);
        painter.drawLine(mid + gap, mid, size - 1, mid);
        painter.drawLine(mid, 0, mid, mid - gap);
        painter.drawLine(mid, mid + gap, mid, size - 1);
    };
    cross(QPen(QColor(0x10, 0x12, 0x16), 3, Qt::SolidLine, Qt::FlatCap));
    cross(QPen(QColor(0xFF, 0xFF, 0xFF), 1, Qt::SolidLine, Qt::FlatCap));
    painter.end();
    return QCursor(pixmap, mid, mid);
}

} // namespace

Viewport::Viewport(QWidget* parent)
    : QOpenGLWidget(parent)
    , impl_(std::make_unique<Impl>(*this))
{
    setMinimumSize(320, 240);
    setMouseTracking(true); // resaltado al pasar el cursor
    setFocusPolicy(Qt::StrongFocus);
    setUpdateBehavior(QOpenGLWidget::NoPartialUpdate);

    setFormat(defaultFormat());
}

QSurfaceFormat Viewport::defaultFormat()
{
    QSurfaceFormat format;
    format.setDepthBufferSize(24);   // sin depth/stencil OCCT advierte que la
    format.setStencilBufferSize(8);  // ventana está incompleta
    if (kCoreProfile) {
        format.setVersion(4, 5);
    }
    format.setProfile(kCoreProfile ? QSurfaceFormat::CoreProfile : QSurfaceFormat::CompatibilityProfile);
    return format;
}

Viewport::~Viewport()
{
    impl_->cancelMeshTasks();
    // Los recursos de OpenGL se liberan con el contexto activo.
    makeCurrent();
    Handle(Aspect_DisplayConnection) display = impl_->viewer->Driver()->GetDisplayConnection();
    impl_->context->RemoveAll(false);
    impl_->context.Nullify();
    impl_->view->Remove();
    impl_->view.Nullify();
    impl_->viewer.Nullify();
    doneCurrent();
}

void Viewport::fitAll()
{
    // Solo lo que es del modelo (las rejillas no cuentan).
    Bnd_Box box;
    for (const auto& body : impl_->bodies) {
        BRepBndLib::Add(geometry::occtShape(body.shape), box);
    }
    for (const auto& [id, shown] : impl_->finished) {
        for (const auto& [region, index] : shown.regions) {
            BRepBndLib::Add(region->Shape(), box);
        }
        BRepBndLib::Add(shown.curves->Shape(), box);
    }
    if (box.IsVoid()) {
        box.Update(-50.0, -50.0, -50.0, 50.0, 50.0, 50.0);
    }
    impl_->view->FitAll(box, 0.05, false);
    update();
}

void Viewport::resetView()
{
    impl_->view->SetProj(V3d_TypeOfOrientation_Zup_AxoRight, false);
    fitAll();
}

void Viewport::pickSketchPlane()
{
    impl_->pickSketchPlane();
    impl_->view->Invalidate();
    update();
}

void Viewport::cancelPickSketchPlane()
{
    impl_->cancelPickSketchPlane();
    impl_->view->Invalidate();
    update();
}

bool Viewport::isPickingSketchPlane() const { return impl_->mode == Impl::Mode::PickPlane; }

void Viewport::beginSketch(const sketch::Plane& plane)
{
    impl_->beginSketch(plane);
    impl_->view->Invalidate();
    update();
}

void Viewport::endSketch()
{
    impl_->endSketch();
    impl_->view->Invalidate();
    update();
}

void Viewport::hideFinishedSketch(document::FeatureId id)
{
    impl_->hideFinishedSketch(id);
    impl_->view->Invalidate();
    update();
}

void Viewport::showFinishedSketch(document::FeatureId id, const QString& name, const sketch::Sketch& sketch,
                                  const sketch::Plane& plane)
{
    impl_->showFinishedSketch(id, name, sketch, plane);
    impl_->view->Invalidate();
    update();
}

void Viewport::setGroundVisible(bool visible)
{
    impl_->groundShown = visible;
    impl_->updateGround(true);
    impl_->view->Invalidate();
    update();
}

void Viewport::setSketchShowAll(bool measures, bool constraints)
{
    impl_->measuresAll = measures;
    impl_->constraintsAll = constraints;
    impl_->rebuildSketch();
    impl_->view->Invalidate();
    update();
}

void Viewport::setSketchMeasuresVisible(bool visible)
{
    impl_->measuresVisible = visible;
    impl_->rebuildSketch();
    impl_->view->Invalidate();
    update();
}

void Viewport::setOriginVisible(bool visible)
{
    impl_->originShown = visible;
    if (impl_->mode != Impl::Mode::PickPlane) {
        impl_->showOrigin();
    }
    impl_->view->Invalidate();
    update();
}

void Viewport::showConstructionPlanes(const std::vector<ConstructionPlaneView>& planes)
{
    impl_->showConstructionPlanes(planes);
    impl_->view->Invalidate();
    update();
}

void Viewport::showConstructionAxes(const std::vector<ConstructionAxisView>& axes)
{
    impl_->showConstructionAxes(axes);
    impl_->view->Invalidate();
    update();
}

void Viewport::showBodies(const std::vector<document::Body>& bodies)
{
    impl_->showBodies(bodies);
    impl_->view->Invalidate();
    update();
}

void Viewport::setSectionPlane(const sketch::Vec3& origin, const sketch::Vec3& normal)
{
    impl_->setSection(origin, normal);
    impl_->view->Invalidate();
    update();
}

void Viewport::clearSectionPlane()
{
    impl_->clearSection();
    impl_->view->Invalidate();
    update();
}

void Viewport::showToolPreview(const geometry::Shape& tool, bool removes)
{
    impl_->showToolPreview(tool, removes);
    impl_->view->Invalidate();
    update();
}

void Viewport::showToolEdges(const geometry::Shape& body, const std::vector<int>& edges)
{
    impl_->showToolEdges(body, edges);
    impl_->view->Invalidate();
    update();
}

void Viewport::showToolFaces(const geometry::Shape& body, const std::vector<int>& faces)
{
    impl_->showToolFaces(body, faces);
    impl_->view->Invalidate();
    update();
}

void Viewport::clearToolPreview()
{
    impl_->clearToolPreview();
    impl_->view->Invalidate();
    update();
}

ModelSelection Viewport::modelSelection() const { return impl_->modelSelection(); }

void Viewport::clearModelSelection()
{
    impl_->context->ClearSelected(false);
    impl_->describeSelection();
    impl_->view->Invalidate();
    update();
}

void Viewport::selectProfiles(const std::vector<ModelSelection::Profile>& profiles)
{
    for (const ModelSelection::Profile& wanted : profiles) {
        const auto it = impl_->finished.find(wanted.sketch);
        if (it == impl_->finished.end()) {
            continue;
        }
        for (const auto& [region, index] : it->second.regions) {
            if (index == wanted.index && !impl_->context->IsSelected(region)) {
                impl_->context->AddOrRemoveSelected(region, false);
            }
        }
    }
    impl_->view->Invalidate();
    update();
}

void Viewport::setAdditiveSelection(bool on) { impl_->setAdditiveSelection(on); }

// Cambiar la ubicación de un objeto del contexto (SetLocation) o mostrarlo sin actualizar no marca la vista como
// cambiada: sin Invalidate() la pantalla seguía con el cuadro anterior y la sombra no se veía.
void Viewport::showGizmo(GizmoKind kind, const sketch::Vec3& origin)
{
    impl_->showGizmo(kind, origin);
    impl_->view->Invalidate();
    update();
}

void Viewport::hideGizmo()
{
    impl_->hideGizmo();
    impl_->view->Invalidate();
    update();
}

void Viewport::isolateGizmo(int axis)
{
    if (axis >= 0) {
        impl_->hideOtherGizmoShapes(axis);
    } else {
        impl_->restoreGizmoShapes();
    }
    impl_->view->Invalidate();
    update();
}

void Viewport::showGhost(const std::vector<geometry::Shape>& bodies)
{
    impl_->showGhost(bodies);
    impl_->view->Invalidate();
    update();
}

void Viewport::setGhostMotion(const sketch::Vec3& pivot, const sketch::Vec3& rotation, const sketch::Vec3& translation)
{
    impl_->setGhostMotion(pivot, rotation, translation);
    impl_->view->Invalidate();
    update();
}

void Viewport::hideGhost()
{
    impl_->hideGhost();
    impl_->view->Invalidate();
    update();
}

void Viewport::showAngleSweep(const sketch::Vec3& center, const sketch::Vec3& axis, const sketch::Vec3& start, double angle)
{
    impl_->showAngleSweep(center, axis, start, angle);
    impl_->view->Invalidate();
    update();
}

void Viewport::hideAngleSweep()
{
    impl_->hideAngleSweep();
    impl_->view->Invalidate();
    update();
}

void Viewport::showAlignmentLine(const sketch::Vec3& from, const sketch::Vec3& to)
{
    impl_->showAlignmentLine(from, to);
    impl_->view->Invalidate();
    update();
}

void Viewport::hideAlignmentLine()
{
    impl_->hideAlignmentLine();
    impl_->view->Invalidate();
    update();
}

void Viewport::showDistanceHandle(const sketch::Vec3& origin, const sketch::Vec3& direction, double distance)
{
    impl_->showDistanceHandle(origin, direction, distance);
    impl_->view->Invalidate();
    update();
}

void Viewport::showDimensionArrow(const sketch::Vec3& from, const sketch::Vec3& to)
{
    impl_->showDimensionArrow(from, to);
    impl_->view->Invalidate();
    update();
}

void Viewport::showDimensionArc(const sketch::Vec3& center, const sketch::Vec3& axis, const sketch::Vec3& start, double angle)
{
    impl_->showDimensionArc(center, axis, start, angle);
    impl_->view->Invalidate();
    update();
}

void Viewport::hideDimensionArrow()
{
    impl_->hideDimensionArrow();
    impl_->view->Invalidate();
    update();
}

void Viewport::hideDistanceHandle()
{
    impl_->hideDistanceHandle();
    impl_->view->Invalidate();
    update();
}

bool Viewport::isSketching() const { return impl_->mode == Impl::Mode::Sketch; }

void Viewport::setNavigationMode(NavigationMode mode)
{
    impl_->setNavigationMode(mode);
    switch (mode) {
    case NavigationMode::Orbit: setCursor(Qt::SizeAllCursor); break;
    case NavigationMode::Pan: setCursor(Qt::OpenHandCursor); break;
    case NavigationMode::Zoom: setCursor(Qt::SizeVerCursor); break;
    case NavigationMode::Default: unsetCursor(); break;
    }
}

void Viewport::lookAtSketch()
{
    impl_->lookAtSketch();
    update();
}

void Viewport::showSketch(const sketch::Sketch& sketch, const std::vector<sketch::EntityId>& underconstrained,
                          const SketchHighlight& highlight)
{
    if (isSketching()) {
        impl_->showSketch(sketch, underconstrained, highlight);
        update();
    }
}

void Viewport::showSketchPreview(const std::vector<sketch::Geometry>& geometry, std::optional<sketch::Vec2> snapPoint,
                                 const std::vector<std::pair<sketch::Vec2, sketch::Vec2>>& guides,
                                 const std::string& badge, sketch::Vec2 badgeAt, bool markerSquare,
                                 const std::vector<sketch::Vec2>& awake)
{
    if (isSketching()) {
        impl_->showSketchPreview(geometry, snapPoint, guides, badge, badgeAt, markerSquare, awake);
        update();
    }
}

void Viewport::setSketchToolActive(bool active)
{
    impl_->sketchToolActive = active;
    impl_->leftPressed = false;
    setCursor(active ? crossCursor() : QCursor(Qt::ArrowCursor));
}

void Viewport::setSketchProjectMode(bool on)
{
    if (isSketching()) {
        impl_->setProjectMode(on);
        impl_->view->Invalidate();
        update();
    }
}

std::vector<sketch::Geometry> Viewport::sketchSection() const
{
    return isSketching() ? impl_->section() : std::vector<sketch::Geometry>{};
}

std::vector<sketch::Geometry> Viewport::textOutline(const QString& text, double height, const QString& family) const
{
    return impl_->textOutline(text, height, family);
}

bool Viewport::showSketchImage(const QString& path, sketch::Vec2 corner, double width, double height)
{
    if (!isSketching()) {
        return false;
    }
    const bool shown = impl_->showImage(path, corner, width, height);
    impl_->view->Invalidate();
    update();
    return shown;
}

void Viewport::grabSketchDrag()
{
    if (impl_->leftPressed) {
        impl_->sketchDragging = true;
    }
}

double Viewport::sketchPixelSize() const { return impl_->pixelSize(); }

double Viewport::sketchGridStep() const { return impl_->gridStep(); }

Qt::MouseButtons Viewport::navigationButtons(Qt::MouseButtons buttons) const
{
    if (impl_->navMode != NavigationMode::Default) {
        return buttons;
    }
    if (((impl_->sketchToolActive || impl_->sketchDragging) && isSketching()) || impl_->handleDragging ||
        impl_->gizmoActive >= 0) {
        buttons &= ~Qt::MouseButtons(Qt::LeftButton);
    }
    return buttons;
}

void Viewport::initializeGL()
{
    const qreal ratio = devicePixelRatioF();
    const Vec2i viewSize(int(width() * ratio), int(height() * ratio));

    const Aspect_Drawable nativeWindow = nativeDrawable(*this);

    Handle(OpenGl_Context) glContext = new OpenGl_Context();
    if (!glContext->Init(kCoreProfile)) {
        qWarning("NufioCAD: OpenCASCADE no pudo usar el contexto OpenGL de Qt");
        return;
    }

    Handle(Aspect_NeutralWindow) neutralWindow = Handle(Aspect_NeutralWindow)::DownCast(impl_->view->Window());
    const bool firstTime = neutralWindow.IsNull();
    if (firstTime) {
        neutralWindow = new Aspect_NeutralWindow();
    }
    neutralWindow->SetNativeHandle(nativeWindow);
    neutralWindow->SetSize(viewSize.x(), viewSize.y());
    impl_->view->SetWindow(neutralWindow, glContext->RenderingContext());

    if (firstTime) {
        // El ViewCube se muestra cuando la vista ya tiene ventana.
        impl_->context->Display(impl_->viewCube, 0, 0, false);
        impl_->applyTheme(palette().color(QPalette::Window).lightness() < 128);
        resetView();
    }
}

void Viewport::paintGL()
{
    if (impl_->view->Window().IsNull()) {
        return;
    }
    // Qt cambió la ventana nativa (por ejemplo, al recrearla): se enlaza OCCT
    // de nuevo, como en el ejemplo oficial de OCCT para Qt.
    if (impl_->view->Window()->NativeHandle() != nativeDrawable(*this)) {
        initializeGL();
    }

    Handle(OpenGl_Context) glContext =
        Handle(OpenGl_GraphicDriver)::DownCast(impl_->viewer->Driver())->GetSharedContext();
    Handle(OpenGl_FrameBuffer) frameBuffer = glContext->DefaultFrameBuffer();
    if (frameBuffer.IsNull()) {
        frameBuffer = new QtFrameBuffer();
        glContext->SetDefaultFrameBuffer(frameBuffer);
    }
    if (!frameBuffer->InitWrapper(glContext)) {
        frameBuffer.Nullify();
        qWarning("NufioCAD: no se pudo envolver el framebuffer de Qt");
        return;
    }

    // Ajusta la vista si cambió el tamaño del widget.
    Vec2i oldSize;
    const Vec2i newSize = frameBuffer->GetVPSize();
    Handle(Aspect_NeutralWindow) neutralWindow = Handle(Aspect_NeutralWindow)::DownCast(impl_->view->Window());
    neutralWindow->Size(oldSize.x(), oldSize.y());
    if (newSize != oldSize) {
        neutralWindow->SetSize(newSize.x(), newSize.y());
        impl_->view->MustBeResized();
        impl_->view->Invalidate();
    }

    impl_->view->InvalidateImmediate();
    impl_->FlushViewEvents(impl_->context, impl_->view, true);
}

void Viewport::changeEvent(QEvent* event)
{
    QOpenGLWidget::changeEvent(event);
    if (event->type() == QEvent::PaletteChange && !impl_->view->Window().IsNull()) {
        impl_->applyTheme(palette().color(QPalette::Window).lightness() < 128);
        update();
    }
}

void Viewport::flushGizmoDrag()
{
    if (impl_->gizmoFlush != nullptr) {
        impl_->gizmoFlush->stop();
    }
    if (impl_->gizmoHasPending && impl_->gizmoActive >= 0) {
        impl_->gizmoHasPending = false;
        impl_->gizmoClock.restart();
        emit gizmoDragged(impl_->gizmoActive, impl_->gizmoPending);
    }
}

void Viewport::mousePressEvent(QMouseEvent* event)
{
    const qreal ratio = devicePixelRatioF();
    const Vec2i point(int(event->position().x() * ratio), int(event->position().y() * ratio));
    const bool navigating = impl_->navMode != NavigationMode::Default;
    if (event->button() == Qt::LeftButton && !navigating && !isSketching() && impl_->gizmoKind != GizmoKind::None) {
        // ¿Se agarra una flecha o un anillo?
        impl_->context->MoveTo(point.x(), point.y(), impl_->view, false);
        if (impl_->context->HasDetected()) {
            for (int k = 0; k < 3; ++k) {
                if (impl_->context->DetectedInteractive() == impl_->gizmoShapes[k] && impl_->startGizmoDrag(k, point)) {
                    emit gizmoPressed(k);
                    break;
                }
            }
        }
    }
    if (event->button() == Qt::LeftButton && !navigating && !isSketching() && !impl_->handle.IsNull()) {
        // ¿Se agarra la flecha de distancia?
        impl_->context->MoveTo(point.x(), point.y(), impl_->view, false);
        if (impl_->context->HasDetected() && impl_->context->DetectedInteractive() == impl_->handle) {
            if (const auto t = impl_->handleParameterAt(point)) {
                impl_->handleDragging = true;
                impl_->handleGrab = *t - impl_->handleDistance;
            }
        }
    }
    if (event->button() == Qt::LeftButton && !navigating && isSketching()) {
        impl_->leftPressed = true;
        impl_->pressPosition = event->position();
        impl_->pressSketchPoint = impl_->sketchPointAt(point);
        // Quien escucha puede llamar a grabSketchDrag() para arrastrar geometría.
        if (!impl_->sketchToolActive) {
            if (const auto p = impl_->sketchPointAt(point)) {
                emit sketchPressed(p->x, p->y);
            }
        }
    }
    if (impl_->UpdateMouseButtons(point, toOcctButtons(navigationButtons(event->buttons())),
                                  toOcctModifiers(event->modifiers()), false)) {
        update();
    }
}

void Viewport::mouseReleaseEvent(QMouseEvent* event)
{
    const qreal ratio = devicePixelRatioF();
    const Vec2i point(int(event->position().x() * ratio), int(event->position().y() * ratio));
    if (event->button() == Qt::LeftButton && impl_->gizmoActive >= 0) {
        flushGizmoDrag();  // lo último que se movió llega antes de soltar
        const int axis = impl_->gizmoActive;
        impl_->gizmoActive = -1;
        emit gizmoReleased(axis);
    } else if (event->button() == Qt::LeftButton && impl_->handleDragging) {
        impl_->handleDragging = false;
        emit distanceHandleReleased();
    } else if (event->button() == Qt::LeftButton && impl_->sketchDragging) {
        impl_->sketchDragging = false;
        impl_->leftPressed = false;
        const bool moved = (event->position() - impl_->pressPosition).manhattanLength() > 5.0;
        emit sketchDragFinished();
        // Sin moverse, cuenta como un clic (selección).
        if (!moved) {
            if (const auto p = impl_->sketchPointAt(point)) {
                emit sketchClicked(p->x, p->y, event->modifiers());
            }
        }
    } else if (event->button() == Qt::LeftButton && impl_->leftPressed) {
        impl_->leftPressed = false;
        // Un clic, no un arrastre; tras un doble clic se ignora la suelta.
        const QPointF moved = event->position() - impl_->pressPosition;
        if (impl_->ignoreNextRelease) {
            impl_->ignoreNextRelease = false;
        } else if (moved.manhattanLength() <= 5.0 && impl_->projectMode) {
            const auto [projected, source] = impl_->projectionAt(point);
            if (!projected.empty()) {
                emit sketchEdgeProjected(projected, source);
            }
        } else if (moved.manhattanLength() <= 5.0) {
            if (const auto p = impl_->sketchPointAt(point)) {
                emit sketchClicked(p->x, p->y, event->modifiers());
            }
        } else if (impl_->sketchToolActive && impl_->pressSketchPoint) {
            if (const auto p = impl_->sketchPointAt(point)) {
                emit sketchToolDragReleased(impl_->pressSketchPoint->x, impl_->pressSketchPoint->y, p->x, p->y);
            }
        } else if ((event->modifiers() & Qt::ShiftModifier) && impl_->pressSketchPoint) {
            // Shift + arrastre: OCCT dibuja el rectángulo; aquí se informa de él.
            if (const auto p = impl_->sketchPointAt(point)) {
                emit sketchBoxSelected(impl_->pressSketchPoint->x, impl_->pressSketchPoint->y, p->x, p->y);
            }
        }
    }
    if (impl_->UpdateMouseButtons(point, toOcctButtons(navigationButtons(event->buttons())),
                                  toOcctModifiers(event->modifiers()), false)) {
        update();
    }
}

void Viewport::mouseDoubleClickEvent(QMouseEvent* event)
{
    // Doble clic sobre un sketch terminado: volver a editarlo.
    if (event->button() == Qt::LeftButton && !isSketching() && impl_->mode == Impl::Mode::Model) {
        const qreal ratio = devicePixelRatioF();
        impl_->context->MoveTo(int(event->position().x() * ratio), int(event->position().y() * ratio), impl_->view,
                               false);
        if (impl_->context->HasDetected()) {
            if (const auto owner = impl_->finishedOwner(impl_->context->DetectedInteractive())) {
                emit finishedSketchActivated(owner->first);
                return;
            }
        }
    }
    if (event->button() == Qt::LeftButton && isSketching()) {
        impl_->leftPressed = true;
        impl_->pressPosition = event->position();
        impl_->ignoreNextRelease = true;
        const qreal ratio = devicePixelRatioF();
        if (const auto p = impl_->sketchPointAt(
                Vec2i(int(event->position().x() * ratio), int(event->position().y() * ratio)))) {
            emit sketchDoubleClicked(p->x, p->y);
        }
        return;
    }
    QOpenGLWidget::mouseDoubleClickEvent(event);
}

void Viewport::mouseMoveEvent(QMouseEvent* event)
{
    const qreal ratio = devicePixelRatioF();
    const Vec2i point(int(event->position().x() * ratio), int(event->position().y() * ratio));
    if (impl_->gizmoActive >= 0) {
        if (const auto amount = impl_->gizmoDragAmount(point)) {
            // El ratón manda eventos más deprisa de lo que se dibuja: se agrupan (a unos 40 por segundo) y vale el último.
            impl_->gizmoPending = *amount;
            impl_->gizmoHasPending = true;
            constexpr qint64 kInterval = 25;
            if (!impl_->gizmoClock.isValid() || impl_->gizmoClock.elapsed() >= kInterval) {
                flushGizmoDrag();
            } else if (impl_->gizmoFlush == nullptr || !impl_->gizmoFlush->isActive()) {
                if (impl_->gizmoFlush == nullptr) {
                    impl_->gizmoFlush = new QTimer(this);
                    impl_->gizmoFlush->setSingleShot(true);
                    connect(impl_->gizmoFlush, &QTimer::timeout, this, [this] { flushGizmoDrag(); });
                }
                impl_->gizmoFlush->start(int(kInterval - impl_->gizmoClock.elapsed()));
            }
        }
        return;
    }
    if (impl_->handleDragging) {
        if (const auto t = impl_->handleParameterAt(point)) {
            emit distanceHandleDragged(*t - impl_->handleGrab);
        }
        return;
    }
    if (impl_->UpdateMousePosition(point, toOcctButtons(navigationButtons(event->buttons())),
                                   toOcctModifiers(event->modifiers()), false)) {
        update();
    }
    if (isSketching()) {
        const auto p = impl_->sketchPointAt(point);
        emit sketchCursorMoved(p.has_value(), p ? p->x : 0.0, p ? p->y : 0.0);
        const bool moved = (event->position() - impl_->pressPosition).manhattanLength() > 5.0;
        if (impl_->sketchDragging && p && moved) {
            emit sketchDragged(p->x, p->y);
        }
        if (impl_->sketchToolActive && impl_->leftPressed && impl_->pressSketchPoint && p && moved) {
            emit sketchToolDragged(impl_->pressSketchPoint->x, impl_->pressSketchPoint->y, p->x, p->y);
        }
    }
}

void Viewport::keyPressEvent(QKeyEvent* event)
{
    if (isSketching() && (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)) {
        emit sketchEnterPressed();
        return;
    }
    const QString text = event->text();
    if (isSketching() && text.size() == 1
        && (text.front().isDigit() || text == QLatin1String(".") || text == QLatin1String(",")
            || text == QLatin1String("-"))) {
        emit sketchTyped(text);
        return;
    }
    QOpenGLWidget::keyPressEvent(event);
}

void Viewport::leaveEvent(QEvent* event)
{
    QOpenGLWidget::leaveEvent(event);
    if (isSketching()) {
        emit sketchCursorMoved(false, 0.0, 0.0);
    }
}

void Viewport::wheelEvent(QWheelEvent* event)
{
    const qreal ratio = devicePixelRatioF();
    const Vec2i point(int(event->position().x() * ratio), int(event->position().y() * ratio));
    // angleDelta viene en octavos de grado (120 por paso de rueda).
    if (impl_->UpdateZoom(Aspect_ScrollDelta(point, event->angleDelta().y() / 8.0))) {
        update();
    }
}

} // namespace nuf::render
