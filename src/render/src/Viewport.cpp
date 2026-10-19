#include <nuf/render/Viewport.hpp>

#include <AIS_InteractiveContext.hxx>
#include <AIS_Shape.hxx>
#include <AIS_ViewController.hxx>
#include <AIS_ViewCube.hxx>
#include <Aspect_DisplayConnection.hxx>
#include <Aspect_NeutralWindow.hxx>
#include <Font_FontMgr.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <OpenGl_Context.hxx>
#include <OpenGl_FrameBuffer.hxx>
#include <OpenGl_GraphicDriver.hxx>
#include <Prs3d_DatumAspect.hxx>
#include <Prs3d_LineAspect.hxx>
#include <Prs3d_ShadingAspect.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <V3d_View.hxx>
#include <V3d_Viewer.hxx>
#include <gp_Ax2.hxx>

#include <QFontDatabase>
#include <QMouseEvent>
#include <QStringList>
#include <QSurfaceFormat>
#include <QWheelEvent>

#include <utility>

#ifdef _WIN32
#include <windows.h>
#endif

namespace nuf::render {

namespace {

// OpenGL core (sin pipeline fijo), como recomienda OCCT para Qt.
constexpr bool kCoreProfile = true;

// El framebuffer de QOpenGLWidget no es sRGB: se le indica a OCCT al enlazarlo.
class QtFrameBuffer final : public OpenGl_FrameBuffer {
    DEFINE_STANDARD_RTTI_INLINE(QtFrameBuffer, OpenGl_FrameBuffer)

public:
    void BindBuffer(const Handle(OpenGl_Context)& ctx) override
    {
        OpenGl_FrameBuffer::BindBuffer(ctx);
        ctx->SetFrameBufferSRGB(true, false);
    }
    void BindDrawBuffer(const Handle(OpenGl_Context)& ctx) override
    {
        OpenGl_FrameBuffer::BindDrawBuffer(ctx);
        ctx->SetFrameBufferSRGB(true, false);
    }
    void BindReadBuffer(const Handle(OpenGl_Context)& ctx) override
    {
        OpenGl_FrameBuffer::BindReadBuffer(ctx);
    }
};

Aspect_VKeyMouse toOcctButtons(Qt::MouseButtons buttons)
{
    Aspect_VKeyMouse result = Aspect_VKeyMouse_NONE;
    if (buttons & Qt::LeftButton) {
        result |= Aspect_VKeyMouse_LeftButton;
    }
    if (buttons & Qt::MiddleButton) {
        result |= Aspect_VKeyMouse_MiddleButton;
    }
    if (buttons & Qt::RightButton) {
        result |= Aspect_VKeyMouse_RightButton;
    }
    return result;
}

Aspect_VKeyFlags toOcctModifiers(Qt::KeyboardModifiers modifiers)
{
    Aspect_VKeyFlags result = Aspect_VKeyFlags_NONE;
    if (modifiers & Qt::ShiftModifier) {
        result |= Aspect_VKeyFlags_SHIFT;
    }
    if (modifiers & Qt::ControlModifier) {
        result |= Aspect_VKeyFlags_CTRL;
    }
    if (modifiers & Qt::AltModifier) {
        result |= Aspect_VKeyFlags_ALT;
    }
    return result;
}

// OCCT busca "sans-serif" con una lista fija de familias (DejaVu, Arial…)
// que no siempre está instalada (Fedora 44 usa Noto/Adwaita). Se prefiere la
// fuente de la interfaz de Qt y luego alternativas comunes.
TCollection_AsciiString pickLabelFont()
{
    const QStringList candidates{QFontDatabase::systemFont(QFontDatabase::GeneralFont).family(),
                                 QStringLiteral("Segoe UI"),     QStringLiteral("Arial"),
                                 QStringLiteral("Noto Sans"),    QStringLiteral("Liberation Sans"),
                                 QStringLiteral("DejaVu Sans")};
    const Handle(Font_FontMgr) fonts = Font_FontMgr::GetInstance();
    for (const QString& family : candidates) {
        const TCollection_AsciiString name(family.toUtf8().constData());
        Font_FontAspect aspect = Font_FA_Regular;
        if (!fonts->FindFont(name, Font_StrictLevel_Strict, aspect, false).IsNull()) {
            return name;
        }
    }
    return TCollection_AsciiString("sans-serif");
}

Quantity_Color rgb(int r, int g, int b)
{
    return Quantity_Color(r / 255.0, g / 255.0, b / 255.0, Quantity_TOC_sRGB);
}

// Pieza de muestra (como "soporte_sensor" del mockup) para probar la
// navegación y la selección. En la Fase 3 la reemplaza el resultado del
// documento.
TopoDS_Shape makeDemoPart()
{
    const TopoDS_Shape base = BRepPrimAPI_MakeBox(gp_Pnt(-30, -20, 0), 60, 40, 8).Shape();
    const TopoDS_Shape boss = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0, 0, 8), gp::DZ()), 10, 22).Shape();
    const TopoDS_Shape hole = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0, 0, -1), gp::DZ()), 4, 32).Shape();
    const TopoDS_Shape body = BRepAlgoAPI_Fuse(base, boss).Shape();
    return BRepAlgoAPI_Cut(body, hole).Shape();
}

} // namespace

class Viewport::Impl final : public AIS_ViewController {
public:
    explicit Impl(Viewport& widget)
        : widget_(widget)
    {
        Handle(Aspect_DisplayConnection) display = new Aspect_DisplayConnection();
        Handle(OpenGl_GraphicDriver) driver = new OpenGl_GraphicDriver(display, false);
        // QOpenGLWidget hace el intercambio de buffers y compone la ventana.
        driver->ChangeOptions().buffersNoSwap = true;
        driver->ChangeOptions().buffersOpaqueAlpha = true;
        driver->ChangeOptions().useSystemBuffer = false;
        driver->ChangeOptions().ffpEnable = !kCoreProfile;

        viewer = new V3d_Viewer(driver);
        viewer->SetDefaultLights();
        viewer->SetLightOn();

        context = new AIS_InteractiveContext(viewer);
        configureHighlight();

        view = viewer->CreateView();
        view->SetImmediateUpdate(false);

        configureNavigation();
        createViewCube();
        showDemoPart();
    }

    void applyTheme(bool dark)
    {
        if (dark) {
            view->SetBgGradientColors(rgb(0x3A, 0x3F, 0x47), rgb(0x1E, 0x21, 0x26),
                                      Aspect_GradientFillMethod_Vertical, false);
        } else {
            view->SetBgGradientColors(rgb(0xF7, 0xF8, 0xFA), rgb(0xC9, 0xCF, 0xD6),
                                      Aspect_GradientFillMethod_Vertical, false);
        }
        viewCube->SetTextColor(dark ? rgb(0xE6, 0xE9, 0xED) : rgb(0x1F, 0x23, 0x29));
        viewCube->SetBoxColor(dark ? rgb(0x4A, 0x50, 0x59) : rgb(0xE6, 0xE9, 0xED));
        context->Redisplay(viewCube, false);
        view->Invalidate();
    }

    void describeSelection()
    {
        int faces = 0;
        int edges = 0;
        for (context->InitSelected(); context->MoreSelected(); context->NextSelected()) {
            if (!context->HasSelectedShape()) {
                continue;
            }
            switch (context->SelectedShape().ShapeType()) {
            case TopAbs_FACE: ++faces; break;
            case TopAbs_EDGE: ++edges; break;
            default: break;
            }
        }

        QStringList parts;
        if (faces == 1) {
            parts << QStringLiteral("1 cara");
        } else if (faces > 1) {
            parts << QStringLiteral("%1 caras").arg(faces);
        }
        if (edges == 1) {
            parts << QStringLiteral("1 arista");
        } else if (edges > 1) {
            parts << QStringLiteral("%1 aristas").arg(edges);
        }
        emit widget_.selectionChanged(parts.isEmpty() ? QString()
                                                      : QStringLiteral("Seleccionado: ") + parts.join(QStringLiteral(", ")));
    }

    Handle(V3d_Viewer) viewer;
    Handle(V3d_View) view;
    Handle(AIS_InteractiveContext) context;
    Handle(AIS_ViewCube) viewCube;

protected:
    // Pide otro cuadro mientras haya animaciones (ViewCube, inercia).
    void handleViewRedraw(const Handle(AIS_InteractiveContext)& ctx,
                          const Handle(V3d_View)& v) override
    {
        AIS_ViewController::handleViewRedraw(ctx, v);
        if (toAskNextFrame()) {
            widget_.update();
        }
    }

    void OnSelectionChanged(const Handle(AIS_InteractiveContext)& /*ctx*/,
                            const Handle(V3d_View)& /*v*/) override
    {
        describeSelection();
    }

private:
    void configureNavigation()
    {
        // Fusion 360: botón central desplaza, Shift + central orbita y el
        // izquierdo selecciona (clic) o selecciona por rectángulo (arrastre).
        // Botón + modificadores, como clave del mapa (enums distintos en OCCT).
        const auto key = [](unsigned int button, unsigned int modifiers = 0) { return button | modifiers; };
        AIS_MouseGestureMap& gestures = ChangeMouseGestureMap();
        gestures.Clear();
        gestures.Bind(key(Aspect_VKeyMouse_LeftButton), AIS_MouseGesture_SelectRectangle);
        gestures.Bind(key(Aspect_VKeyMouse_LeftButton, Aspect_VKeyFlags_SHIFT), AIS_MouseGesture_SelectRectangle);
        gestures.Bind(key(Aspect_VKeyMouse_LeftButton, Aspect_VKeyFlags_CTRL), AIS_MouseGesture_SelectRectangle);
        gestures.Bind(key(Aspect_VKeyMouse_MiddleButton), AIS_MouseGesture_Pan);
        gestures.Bind(key(Aspect_VKeyMouse_MiddleButton, Aspect_VKeyFlags_SHIFT), AIS_MouseGesture_RotateOrbit);
    }

    void configureHighlight()
    {
        const Handle(Prs3d_Drawer)& drawer = context->DefaultDrawer();
        drawer->SetFaceBoundaryDraw(true);
        drawer->SetFaceBoundaryAspect(new Prs3d_LineAspect(rgb(0x1F, 0x23, 0x29), Aspect_TOL_SOLID, 1.0));

        // Resaltado con el azul de la interfaz: claro al pasar, intenso al
        // seleccionar. Sombreado para rellenar la cara (no solo su contorno).
        for (auto type : {Prs3d_TypeOfHighlight_Dynamic, Prs3d_TypeOfHighlight_LocalDynamic}) {
            context->HighlightStyle(type)->SetColor(rgb(0x6C, 0xA6, 0xFF));
            context->HighlightStyle(type)->SetDisplayMode(AIS_Shaded);
        }
        for (auto type : {Prs3d_TypeOfHighlight_Selected, Prs3d_TypeOfHighlight_LocalSelected}) {
            context->HighlightStyle(type)->SetColor(rgb(0x1F, 0x6F, 0xEB));
            context->HighlightStyle(type)->SetDisplayMode(AIS_Shaded);
        }
    }

    void createViewCube()
    {
        viewCube = new AIS_ViewCube();
        viewCube->SetViewAnimation(ViewAnimation());
        viewCube->SetFixedAnimationLoop(false);
        viewCube->SetAutoStartAnimation(true);
        viewCube->SetFitSelected(false); // encuadra toda la pieza, no la selección
        // El resaltado del cubo usa su aspecto de sombreado (cian por defecto).
        const Handle(Prs3d_Drawer)& cubeHover = viewCube->DynamicHilightAttributes();
        if (!cubeHover.IsNull() && cubeHover->HasOwnShadingAspect()) {
            cubeHover->ShadingAspect()->SetColor(rgb(0x6C, 0xA6, 0xFF));
        }
        viewCube->SetSize(55);
        const TCollection_AsciiString font = pickLabelFont();
        viewCube->SetFont(font);
        viewCube->SetFontHeight(10);
        // Letras de los ejes con la misma fuente y los colores habituales en CAD.
        // El ViewCube las toma del dibujante por defecto del contexto.
        context->DefaultDrawer()->TextAspect()->Aspect()->SetFont(font);
        const Handle(Prs3d_DatumAspect)& datum = context->DefaultDrawer()->DatumAspect();
        const std::pair<Prs3d_DatumParts, Quantity_Color> axes[] = {
            {Prs3d_DatumParts_XAxis, rgb(0xE5, 0x48, 0x4D)},
            {Prs3d_DatumParts_YAxis, rgb(0x2F, 0x9E, 0x44)},
            {Prs3d_DatumParts_ZAxis, rgb(0x3B, 0x82, 0xF6)}};
        for (const auto& [part, color] : axes) {
            datum->TextAspect(part)->Aspect()->SetFont(font);
            datum->TextAspect(part)->SetColor(color);
        }
        viewCube->SetTransformPersistence(
            new Graphic3d_TransformPers(Graphic3d_TMF_TriedronPers, Aspect_TOTP_RIGHT_UPPER, Graphic3d_Vec2i(85, 85)));
        // Eje Z hacia arriba, como en los slicers de impresión 3D.
        viewCube->SetBoxSideLabel(V3d_Xpos, "DERECHA");
        viewCube->SetBoxSideLabel(V3d_Xneg, "IZQUIERDA");
        viewCube->SetBoxSideLabel(V3d_Ypos, "ATRÁS");
        viewCube->SetBoxSideLabel(V3d_Yneg, "FRENTE");
        viewCube->SetBoxSideLabel(V3d_Zpos, "ARRIBA");
        viewCube->SetBoxSideLabel(V3d_Zneg, "ABAJO");
    }

    void showDemoPart()
    {
        Handle(AIS_Shape) part = new AIS_Shape(makeDemoPart());
        part->SetMaterial(Graphic3d_MaterialAspect(Graphic3d_NameOfMaterial_Plastified));
        part->SetColor(rgb(0xB7, 0xBE, 0xC7));
        // El modo de selección va en Display: con -1 la pieza queda como no
        // seleccionable y un Activate posterior no lo revierte.
        context->Display(part, AIS_Shaded, AIS_Shape::SelectionMode(TopAbs_FACE), false);
        context->Activate(part, AIS_Shape::SelectionMode(TopAbs_EDGE));
    }

    Viewport& widget_;
};

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
    impl_->view->FitAll(0.05, false);
    update();
}

void Viewport::resetView()
{
    impl_->view->SetProj(V3d_TypeOfOrientation_Zup_AxoRight, false);
    fitAll();
}

void Viewport::initializeGL()
{
    const qreal ratio = devicePixelRatioF();
    const Graphic3d_Vec2i viewSize(int(width() * ratio), int(height() * ratio));

#ifdef _WIN32
    const auto nativeWindow = reinterpret_cast<Aspect_Drawable>(WindowFromDC(wglGetCurrentDC()));
#else
    const auto nativeWindow = static_cast<Aspect_Drawable>(winId());
#endif

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
    Graphic3d_Vec2i oldSize;
    const Graphic3d_Vec2i newSize = frameBuffer->GetVPSize();
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

void Viewport::mousePressEvent(QMouseEvent* event)
{
    const qreal ratio = devicePixelRatioF();
    const Graphic3d_Vec2i point(int(event->position().x() * ratio), int(event->position().y() * ratio));
    if (impl_->UpdateMouseButtons(point, toOcctButtons(event->buttons()), toOcctModifiers(event->modifiers()),
                                  false)) {
        update();
    }
}

void Viewport::mouseReleaseEvent(QMouseEvent* event)
{
    const qreal ratio = devicePixelRatioF();
    const Graphic3d_Vec2i point(int(event->position().x() * ratio), int(event->position().y() * ratio));
    if (impl_->UpdateMouseButtons(point, toOcctButtons(event->buttons()), toOcctModifiers(event->modifiers()),
                                  false)) {
        update();
    }
}

void Viewport::mouseMoveEvent(QMouseEvent* event)
{
    const qreal ratio = devicePixelRatioF();
    const Graphic3d_Vec2i point(int(event->position().x() * ratio), int(event->position().y() * ratio));
    if (impl_->UpdateMousePosition(point, toOcctButtons(event->buttons()), toOcctModifiers(event->modifiers()),
                                   false)) {
        update();
    }
}

void Viewport::wheelEvent(QWheelEvent* event)
{
    const qreal ratio = devicePixelRatioF();
    const Graphic3d_Vec2i point(int(event->position().x() * ratio), int(event->position().y() * ratio));
    // angleDelta viene en octavos de grado (120 por paso de rueda).
    if (impl_->UpdateZoom(Aspect_ScrollDelta(point, event->angleDelta().y() / 8.0))) {
        update();
    }
}

} // namespace nuf::render
