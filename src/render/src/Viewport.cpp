#include <nuf/render/Viewport.hpp>

#include <AIS_AnimationCamera.hxx>
#include <AIS_InteractiveContext.hxx>
#include <AIS_Shape.hxx>
#include <AIS_ViewController.hxx>
#include <AIS_ViewCube.hxx>
#include <Aspect_DisplayConnection.hxx>
#include <Aspect_NeutralWindow.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <Bnd_Box.hxx>
#include <Font_FontMgr.hxx>
#include <Graphic3d_ArrayOfPoints.hxx>
#include <Graphic3d_ArrayOfPolylines.hxx>
#include <Graphic3d_ArrayOfSegments.hxx>
#include <Graphic3d_AspectLine3d.hxx>
#include <Graphic3d_AspectMarker3d.hxx>
#include <Graphic3d_Camera.hxx>
#include <OpenGl_Context.hxx>
#include <OpenGl_FrameBuffer.hxx>
#include <OpenGl_GraphicDriver.hxx>
#include <Prs3d_DatumAspect.hxx>
#include <Prs3d_LineAspect.hxx>
#include <Prs3d_ShadingAspect.hxx>
#include <SelectMgr_Filter.hxx>
#include <StdSelect_BRepOwner.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopoDS.hxx>
#include <V3d_View.hxx>
#include <V3d_Viewer.hxx>
#include <gp_Ax2.hxx>
#include <gp_Ax3.hxx>
#include <gp_Pln.hxx>

#include <QFontDatabase>
#include <QMouseEvent>
#include <QStringList>
#include <QSurfaceFormat>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <map>
#include <numbers>
#include <optional>
#include <tuple>
#include <utility>
#include <vector>

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

// Ventana nativa que OCCT asocia a la vista. En Linux se usa la ventana
// principal y no winId() del viewport: eso lo convertiría en una ventana X11
// propia que Qt alterna con la principal (al maximizar o desacoplar paneles),
// y OCCT se quedaría con una que ya no existe al cerrar la app.
Aspect_Drawable nativeDrawable(QWidget& widget)
{
#ifdef _WIN32
    (void)widget;
    return reinterpret_cast<Aspect_Drawable>(WindowFromDC(wglGetCurrentDC()));
#else
    return static_cast<Aspect_Drawable>(widget.window()->winId());
#endif
}

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

gp_Pnt toPnt(const sketch::Vec3& v) { return gp_Pnt(v.x, v.y, v.z); }
gp_Dir toDir(const sketch::Vec3& v) { return gp_Dir(v.x, v.y, v.z); }
sketch::Vec3 toVec3(const gp_XYZ& v) { return {v.X(), v.Y(), v.Z()}; }

gp_Ax3 toAx3(const sketch::Plane& plane)
{
    return gp_Ax3(toPnt(plane.origin), toDir(plane.normal), toDir(plane.xAxis));
}

// Durante la elección del plano del sketch solo se aceptan caras planas de la
// pieza; lo que no es una forma (planos de origen, ViewCube) pasa siempre.
class SketchPlaneFilter final : public SelectMgr_Filter {
    DEFINE_STANDARD_RTTI_INLINE(SketchPlaneFilter, SelectMgr_Filter)

public:
    Standard_Boolean IsOk(const Handle(SelectMgr_EntityOwner)& owner) const override
    {
        Handle(StdSelect_BRepOwner) brep = Handle(StdSelect_BRepOwner)::DownCast(owner);
        if (brep.IsNull() || !brep->HasShape()) {
            return Standard_True;
        }
        const TopoDS_Shape& shape = brep->Shape();
        return shape.ShapeType() == TopAbs_FACE
               && BRepAdaptor_Surface(TopoDS::Face(shape)).GetType() == GeomAbs_Plane;
    }
};

// Rejilla del sketch sobre su plano: líneas finas cada «step», más marcadas
// cada 10 y los ejes X (rojo) e Y (verde) del sketch. Cubre un cuadrado
// alrededor del centro de la vista; Viewport ajusta paso y región al zoom.
class SketchGrid final : public AIS_InteractiveObject {
    DEFINE_STANDARD_RTTI_INLINE(SketchGrid, AIS_InteractiveObject)

public:
    gp_Ax3 plane;
    double step = 10.0;
    double centerX = 0.0;
    double centerY = 0.0;
    double half = 100.0;
    Quantity_Color minorColor;
    Quantity_Color majorColor;

    void Compute(const Handle(PrsMgr_PresentationManager)& /*manager*/,
                 const Handle(Prs3d_Presentation)& prs, const Standard_Integer mode) override
    {
        if (mode != 0) {
            return;
        }
        const gp_Pnt origin = plane.Location();
        const gp_Vec xDir(plane.XDirection());
        const gp_Vec yDir(plane.YDirection());
        const auto at = [&](double x, double y) { return origin.Translated(xDir * x + yDir * y); };

        const int lines = int(std::lround(half / step));
        const int capacity = 4 * (2 * lines + 1);
        Handle(Graphic3d_ArrayOfSegments) minor = new Graphic3d_ArrayOfSegments(capacity);
        Handle(Graphic3d_ArrayOfSegments) major = new Graphic3d_ArrayOfSegments(capacity);
        const double x0 = centerX - half;
        const double x1 = centerX + half;
        const double y0 = centerY - half;
        const double y1 = centerY + half;
        for (int i = -lines; i <= lines; ++i) {
            // Índice global de la línea: múltiplo de 10 = línea marcada; 0 = eje.
            const long long kx = std::llround(centerX / step) + i;
            const long long ky = std::llround(centerY / step) + i;
            const double x = double(kx) * step;
            const double y = double(ky) * step;
            if (kx != 0) {
                Handle(Graphic3d_ArrayOfSegments)& array = kx % 10 == 0 ? major : minor;
                array->AddVertex(at(x, y0));
                array->AddVertex(at(x, y1));
            }
            if (ky != 0) {
                Handle(Graphic3d_ArrayOfSegments)& array = ky % 10 == 0 ? major : minor;
                array->AddVertex(at(x0, y));
                array->AddVertex(at(x1, y));
            }
        }
        addLines(prs, minor, minorColor, 1.0);
        addLines(prs, major, majorColor, 1.0);

        if (y0 <= 0.0 && 0.0 <= y1) {
            Handle(Graphic3d_ArrayOfSegments) axis = new Graphic3d_ArrayOfSegments(2);
            axis->AddVertex(at(x0, 0.0));
            axis->AddVertex(at(x1, 0.0));
            addLines(prs, axis, rgb(0xE5, 0x48, 0x4D), 1.5);
        }
        if (x0 <= 0.0 && 0.0 <= x1) {
            Handle(Graphic3d_ArrayOfSegments) axis = new Graphic3d_ArrayOfSegments(2);
            axis->AddVertex(at(0.0, y0));
            axis->AddVertex(at(0.0, y1));
            addLines(prs, axis, rgb(0x2F, 0x9E, 0x44), 1.5);
        }
    }

    void ComputeSelection(const Handle(SelectMgr_Selection)& /*selection*/,
                          const Standard_Integer /*mode*/) override
    {
        // La rejilla no se selecciona.
    }

private:
    static void addLines(const Handle(Prs3d_Presentation)& prs, const Handle(Graphic3d_ArrayOfSegments)& lines,
                         const Quantity_Color& color, double width)
    {
        if (lines->VertexNumber() == 0) {
            return;
        }
        Handle(Graphic3d_Group) group = prs->NewGroup();
        group->SetGroupPrimitivesAspect(new Graphic3d_AspectLine3d(color, Aspect_TOL_SOLID, width));
        group->AddPrimitiveArray(lines);
    }
};

// Estilo de trazo de la geometría del sketch.
struct SketchStroke {
    Quantity_Color color;
    double width = 2.0;
    Aspect_TypeOfLine type = Aspect_TOL_SOLID;

    auto key() const
    {
        return std::make_tuple(color.Red(), color.Green(), color.Blue(), width, int(type));
    }
};

// Curvas y puntos del sketch en su plano, agrupados por estilo. No se
// selecciona: los clics los interpreta la herramienta activa.
class SketchDrawing final : public AIS_InteractiveObject {
    DEFINE_STANDARD_RTTI_INLINE(SketchDrawing, AIS_InteractiveObject)

public:
    gp_Ax3 plane;

    void clear()
    {
        curves_.clear();
        points_.clear();
    }

    void addGeometry(const sketch::Geometry& geometry, const SketchStroke& stroke)
    {
        std::vector<sketch::Vec2> polyline;
        if (const auto* line = std::get_if<sketch::Line>(&geometry)) {
            polyline = {line->start, line->end};
        } else if (const auto* circle = std::get_if<sketch::Circle>(&geometry)) {
            polyline = arcPoints(circle->center, circle->radius, 0.0, 2.0 * std::numbers::pi);
        } else if (const auto* arc = std::get_if<sketch::Arc>(&geometry)) {
            double end = arc->endAngle;
            while (end <= arc->startAngle) {
                end += 2.0 * std::numbers::pi;
            }
            polyline = arcPoints(arc->center, arc->radius, arc->startAngle, end);
        } else if (const auto* point = std::get_if<sketch::Point>(&geometry)) {
            addPoint(point->position, stroke.color, 1.0);
        }
        if (!polyline.empty()) {
            curves_.push_back({std::move(polyline), stroke});
        }
    }

    void addPoint(sketch::Vec2 p, const Quantity_Color& color, double scale)
    {
        points_.push_back({p, color, scale});
    }

    void Compute(const Handle(PrsMgr_PresentationManager)& /*manager*/,
                 const Handle(Prs3d_Presentation)& prs, const Standard_Integer mode) override
    {
        if (mode != 0) {
            return;
        }
        // Una matriz de polilíneas por estilo.
        std::map<decltype(SketchStroke{}.key()), std::vector<const Curve*>> groups;
        for (const Curve& curve : curves_) {
            groups[curve.stroke.key()].push_back(&curve);
        }
        for (const auto& [key, curves] : groups) {
            int vertices = 0;
            for (const Curve* curve : curves) {
                vertices += int(curve->points.size());
            }
            Handle(Graphic3d_ArrayOfPolylines) array = new Graphic3d_ArrayOfPolylines(vertices, int(curves.size()));
            for (const Curve* curve : curves) {
                array->AddBound(int(curve->points.size()));
                for (const sketch::Vec2& p : curve->points) {
                    array->AddVertex(at(p));
                }
            }
            const SketchStroke& stroke = curves.front()->stroke;
            Handle(Graphic3d_Group) group = prs->NewGroup();
            group->SetGroupPrimitivesAspect(new Graphic3d_AspectLine3d(stroke.color, stroke.type, stroke.width));
            group->AddPrimitiveArray(array);
        }
        for (const Marker& marker : points_) {
            Handle(Graphic3d_ArrayOfPoints) array = new Graphic3d_ArrayOfPoints(1);
            array->AddVertex(at(marker.position));
            Handle(Graphic3d_Group) group = prs->NewGroup();
            group->SetGroupPrimitivesAspect(new Graphic3d_AspectMarker3d(Aspect_TOM_BALL, marker.color, marker.scale));
            group->AddPrimitiveArray(array);
        }
    }

    void ComputeSelection(const Handle(SelectMgr_Selection)& /*selection*/,
                          const Standard_Integer /*mode*/) override
    {}

private:
    struct Curve {
        std::vector<sketch::Vec2> points;
        SketchStroke stroke;
    };
    struct Marker {
        sketch::Vec2 position;
        Quantity_Color color;
        double scale;
    };

    gp_Pnt at(sketch::Vec2 p) const
    {
        return plane.Location().Translated(gp_Vec(plane.XDirection()) * p.x + gp_Vec(plane.YDirection()) * p.y);
    }

    // Un vértice cada 4° como máximo (y al menos 8 tramos).
    static std::vector<sketch::Vec2> arcPoints(sketch::Vec2 c, double r, double from, double to)
    {
        const int segments = std::max(8, int(std::ceil((to - from) / (4.0 * std::numbers::pi / 180.0))));
        std::vector<sketch::Vec2> points;
        points.reserve(segments + 1);
        for (int i = 0; i <= segments; ++i) {
            const double t = from + (to - from) * i / segments;
            points.push_back({c.x + r * std::cos(t), c.y + r * std::sin(t)});
        }
        return points;
    }

    std::vector<Curve> curves_;
    std::vector<Marker> points_;
};

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

        planeFilter = new SketchPlaneFilter();
        grid = new SketchGrid();
        // La geometría del sketch se ve aunque la pieza la tape (por ejemplo,
        // un sketch en el plano XY bajo la pieza).
        drawing = new SketchDrawing();
        drawing->SetZLayer(Graphic3d_ZLayerId_Topmost);
        preview = new SketchDrawing();
        preview->SetZLayer(Graphic3d_ZLayerId_Topmost);
    }

    enum class Mode { Model, PickPlane, Sketch };

    void applyTheme(bool dark)
    {
        darkTheme = dark;
        grid->minorColor = dark ? rgb(0x41, 0x46, 0x4E) : rgb(0xCC, 0xD1, 0xD8);
        grid->majorColor = dark ? rgb(0x63, 0x6B, 0x76) : rgb(0x95, 0x9E, 0xAA);
        if (context->IsDisplayed(grid)) {
            context->Redisplay(grid, false);
        }
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

    // --- Modo sketch -------------------------------------------------------

    void pickSketchPlane()
    {
        if (mode != Mode::Model) {
            return;
        }
        if (originPlanes.empty()) {
            createOriginPlanes();
        }
        context->ClearSelected(false);
        for (const OriginPlane& origin : originPlanes) {
            context->Display(origin.shape, AIS_Shaded, 0, false);
        }
        context->Deactivate(part, AIS_Shape::SelectionMode(TopAbs_EDGE));
        context->AddFilter(planeFilter);
        mode = Mode::PickPlane;
        emit widget_.selectionChanged(QString());
    }

    void cancelPickSketchPlane()
    {
        if (mode != Mode::PickPlane) {
            return;
        }
        context->ClearSelected(false);
        for (const OriginPlane& origin : originPlanes) {
            context->Erase(origin.shape, false);
        }
        context->RemoveFilter(planeFilter);
        context->Activate(part, AIS_Shape::SelectionMode(TopAbs_EDGE));
        mode = Mode::Model;
    }

    void beginSketch(const sketch::Plane& plane)
    {
        cancelPickSketchPlane();
        if (mode == Mode::Sketch) {
            endSketch();
        }
        context->ClearSelected(false);
        context->Deactivate(part);
        sketchPlane = plane;
        grid->plane = toAx3(plane);
        updateGrid(true);
        context->Display(grid, 0, -1, false);
        for (const Handle(SketchDrawing)& d : {drawing, preview}) {
            d->plane = toAx3(plane);
            d->clear();
            d->SetToUpdate();
            context->Display(d, 0, -1, false);
        }
        mode = Mode::Sketch;
        lookAtSketch();
        emit widget_.selectionChanged(QString());
    }

    void endSketch()
    {
        if (mode != Mode::Sketch) {
            return;
        }
        context->Erase(grid, false);
        context->Erase(drawing, false);
        context->Erase(preview, false);
        context->Activate(part, AIS_Shape::SelectionMode(TopAbs_FACE));
        context->Activate(part, AIS_Shape::SelectionMode(TopAbs_EDGE));
        mode = Mode::Model;
        emit widget_.sketchCursorMoved(false, 0.0, 0.0);
    }

    // Anima la cámara hasta mirar el plano de frente, con el eje Y del sketch
    // hacia arriba, sin cambiar el zoom ni alejarse del punto que se miraba.
    void lookAtSketch()
    {
        if (mode != Mode::Sketch) {
            return;
        }
        const Handle(Graphic3d_Camera)& current = view->Camera();
        Handle(Graphic3d_Camera) start = new Graphic3d_Camera();
        start->Copy(current);
        Handle(Graphic3d_Camera) end = new Graphic3d_Camera();
        end->Copy(current);

        const sketch::Vec2 c = sketchPlane.toLocal(toVec3(current->Center().XYZ()));
        const gp_Pnt center = toPnt(sketchPlane.toWorld(c));
        const gp_Vec normal(toDir(sketchPlane.normal));
        end->SetEyeAndCenter(center.Translated(normal * current->Distance()), center);
        end->SetUp(toDir(sketchPlane.yAxis));

        const Handle(AIS_AnimationCamera)& animation = ViewAnimation();
        animation->Stop();
        animation->SetView(view);
        animation->SetCameraStart(start);
        animation->SetCameraEnd(end);
        animation->SetOwnDuration(0.4);
        animation->StartTimer(0.0, 1.0, true, false);
    }

    // Punto del plano del sketch bajo el píxel, si el rayo lo corta.
    std::optional<sketch::Vec2> sketchPointAt(const Graphic3d_Vec2i& pixel) const
    {
        Standard_Real x = 0, y = 0, z = 0, dx = 0, dy = 0, dz = 0;
        view->ConvertWithProj(pixel.x(), pixel.y(), x, y, z, dx, dy, dz);
        const gp_XYZ from(x, y, z);
        const gp_XYZ dir(dx, dy, dz);
        const gp_XYZ normal = toDir(sketchPlane.normal).XYZ();
        const double facing = normal.Dot(dir);
        if (std::abs(facing) < 1e-9) {
            return std::nullopt;
        }
        const double t = normal.Dot(toPnt(sketchPlane.origin).XYZ() - from) / facing;
        return sketchPlane.toLocal(toVec3(from + dir * t));
    }

    // Colores de la geometría del sketch (como en Fusion: azul = aún libre).
    SketchStroke freeStroke() const { return {darkTheme ? rgb(0x6C, 0xA6, 0xFF) : rgb(0x1F, 0x6F, 0xEB)}; }
    SketchStroke fixedStroke() const { return {darkTheme ? rgb(0xE6, 0xE9, 0xED) : rgb(0x1F, 0x23, 0x29)}; }
    SketchStroke constructionStroke() const { return {rgb(0xF2, 0xA3, 0x4B), 1.5, Aspect_TOL_DASH}; }

    void showSketch(const sketch::Sketch& sketch, const std::vector<sketch::EntityId>& underconstrained)
    {
        drawing->clear();
        for (const sketch::Entity& e : sketch.entities()) {
            if (e.reference) {
                continue;
            }
            const bool loose = std::ranges::find(underconstrained, e.id) != underconstrained.end();
            const SketchStroke stroke = e.construction ? constructionStroke() : (loose ? freeStroke() : fixedStroke());
            drawing->addGeometry(e.geometry, stroke);
            // Extremos y centros como puntos.
            const auto mark = [&](sketch::Part which) {
                if (const auto p = sketch.point({e.id, which})) {
                    drawing->addPoint(*p, stroke.color, 1.6);
                }
            };
            if (std::holds_alternative<sketch::Line>(e.geometry) || std::holds_alternative<sketch::Arc>(e.geometry)) {
                mark(sketch::Part::Start);
                mark(sketch::Part::End);
            }
            if (std::holds_alternative<sketch::Circle>(e.geometry) || std::holds_alternative<sketch::Arc>(e.geometry)) {
                mark(sketch::Part::Center);
            }
        }
        context->Redisplay(drawing, false);
    }

    void showSketchPreview(const std::vector<sketch::Geometry>& geometry, std::optional<sketch::Vec2> snapPoint)
    {
        preview->clear();
        SketchStroke stroke = freeStroke();
        stroke.width = 1.5;
        for (const sketch::Geometry& g : geometry) {
            preview->addGeometry(g, stroke);
        }
        if (snapPoint) {
            preview->addPoint(*snapPoint, rgb(0xFF, 0xC9, 0x40), 3.0);
        }
        context->Redisplay(preview, false);
    }

    double pixelSize() const
    {
        Standard_Real width = 0, height = 0;
        view->Size(width, height);
        Standard_Integer pixelsWide = 0, pixelsHigh = 0;
        view->Window()->Size(pixelsWide, pixelsHigh);
        return width / std::max(pixelsWide, 1);
    }

    double gridStep() const { return grid->step; }

    Handle(V3d_Viewer) viewer;
    Handle(V3d_View) view;
    Handle(AIS_InteractiveContext) context;
    Handle(AIS_ViewCube) viewCube;
    Mode mode = Mode::Model;
    bool sketchToolActive = false;
    bool leftPressed = false;
    bool ignoreNextRelease = false;
    QPointF pressPosition;

protected:
    // Pide otro cuadro mientras haya animaciones (ViewCube, inercia).
    void handleViewRedraw(const Handle(AIS_InteractiveContext)& ctx,
                          const Handle(V3d_View)& v) override
    {
        // La cámara ya está actualizada para este cuadro: se ajusta la rejilla
        // antes de dibujar.
        if (mode == Mode::Sketch) {
            updateGrid(false);
        }
        AIS_ViewController::handleViewRedraw(ctx, v);
        if (toAskNextFrame()) {
            widget_.update();
        }
    }

    void OnSelectionChanged(const Handle(AIS_InteractiveContext)& /*ctx*/,
                            const Handle(V3d_View)& /*v*/) override
    {
        if (mode == Mode::PickPlane) {
            pickedSketchPlane();
        } else {
            describeSelection();
        }
    }

private:
    void configureNavigation()
    {
        // Izquierdo: clic selecciona y arrastre orbita. Shift + izquierdo: clic
        // agrega/quita de la selección y arrastre selecciona por rectángulo.
        // Derecho: arrastre desplaza; el clic queda libre para el menú rápido.
        // Botón central: también desplaza; Shift + central orbita (Fusion 360).
        // Botón + modificadores, como clave del mapa (enums distintos en OCCT).
        const auto key = [](unsigned int button, unsigned int modifiers = 0) { return button | modifiers; };
        AIS_MouseGestureMap& gestures = ChangeMouseGestureMap();
        gestures.Clear();
        gestures.Bind(key(Aspect_VKeyMouse_LeftButton), AIS_MouseGesture_RotateOrbit);
        gestures.Bind(key(Aspect_VKeyMouse_LeftButton, Aspect_VKeyFlags_SHIFT), AIS_MouseGesture_SelectRectangle);
        gestures.Bind(key(Aspect_VKeyMouse_RightButton), AIS_MouseGesture_Pan);
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
        for (const auto& [datumPart, color] : axes) {
            datum->TextAspect(datumPart)->Aspect()->SetFont(font);
            datum->TextAspect(datumPart)->SetColor(color);
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

    struct OriginPlane {
        Handle(AIS_Shape) shape;
        sketch::Plane plane;
        QString name;
    };

    // Planos XY, XZ e YZ como cuadrados translúcidos en el cuadrante positivo,
    // con un tamaño proporcional a la pieza (como en Fusion 360).
    void createOriginPlanes()
    {
        Bnd_Box box;
        BRepBndLib::Add(part->Shape(), box);
        double size = 20.0;
        if (!box.IsVoid()) {
            size = std::max(size, 0.6 * std::sqrt(box.SquareExtent()) / std::sqrt(3.0));
        }
        const std::pair<sketch::Plane, QString> planes[] = {
            {sketch::Plane::xy(), QStringLiteral("Plano XY")},
            {sketch::Plane::xz(), QStringLiteral("Plano XZ")},
            {sketch::Plane::yz(), QStringLiteral("Plano YZ")}};
        for (const auto& [plane, name] : planes) {
            const TopoDS_Face face = BRepBuilderAPI_MakeFace(gp_Pln(toAx3(plane)), 0.0, size, 0.0, size);
            Handle(AIS_Shape) shape = new AIS_Shape(face);
            shape->SetColor(rgb(0xF2, 0xA3, 0x4B));
            shape->SetTransparency(0.6);
            // Por encima de la pieza: suelen atravesarla y quedarían ocultos.
            shape->SetZLayer(Graphic3d_ZLayerId_Topmost);
            originPlanes.push_back({shape, plane, name});
        }
    }

    void pickedSketchPlane()
    {
        context->InitSelected();
        if (!context->MoreSelected()) {
            return;
        }
        std::optional<std::pair<sketch::Plane, QString>> picked;
        const Handle(AIS_InteractiveObject) object = context->SelectedInteractive();
        for (const OriginPlane& origin : originPlanes) {
            if (origin.shape == object) {
                picked.emplace(origin.plane, origin.name);
            }
        }
        if (!picked && context->HasSelectedShape() && context->SelectedShape().ShapeType() == TopAbs_FACE) {
            const TopoDS_Face face = TopoDS::Face(context->SelectedShape());
            const BRepAdaptor_Surface surface(face);
            if (surface.GetType() == GeomAbs_Plane) {
                const gp_Pln pln = surface.Plane();
                gp_Dir normal = pln.Axis().Direction();
                if (face.Orientation() == TopAbs_REVERSED) {
                    normal.Reverse();
                }
                picked.emplace(sketch::Plane::fromPointAndNormal(toVec3(pln.Location().XYZ()), toVec3(normal.XYZ())),
                               QStringLiteral("Cara plana"));
            }
        }
        context->ClearSelected(false);
        if (!picked) {
            return;
        }
        // Se avisa fuera del procesado de eventos de OCCT: quien recibe la
        // señal cambia el contexto (beginSketch).
        QMetaObject::invokeMethod(
            &widget_, [widget = &widget_, plane = picked->first, name = picked->second] {
                emit widget->sketchPlanePicked(plane, name);
            },
            Qt::QueuedConnection);
    }

    // Paso de la rejilla según el zoom: la menor potencia de 10 cuyas líneas
    // queden a 7 px o más. Región centrada en lo que se mira.
    void updateGrid(bool force)
    {
        Standard_Real width = 0, height = 0;
        view->Size(width, height);
        const double size = std::max({width, height, 1e-3});
        constexpr double kMinSpacingPx = 7.0;
        const double step = std::pow(10.0, std::ceil(std::log10(kMinSpacingPx * pixelSize())));
        const double block = 10.0 * step;
        const sketch::Vec2 c = sketchPlane.toLocal(toVec3(view->Camera()->Center().XYZ()));
        const double cx = std::round(c.x / block) * block;
        const double cy = std::round(c.y / block) * block;
        const double half = std::ceil(1.5 * size / block) * block;
        if (!force && step == grid->step && cx == grid->centerX && cy == grid->centerY && half == grid->half) {
            return;
        }
        grid->step = step;
        grid->centerX = cx;
        grid->centerY = cy;
        grid->half = half;
        if (context->IsDisplayed(grid)) {
            context->Redisplay(grid, false);
        } else {
            grid->SetToUpdate(); // si no, Display reutiliza la presentación vieja
        }
    }

    void showDemoPart()
    {
        part = new AIS_Shape(makeDemoPart());
        part->SetMaterial(Graphic3d_MaterialAspect(Graphic3d_NameOfMaterial_Plastified));
        part->SetColor(rgb(0xB7, 0xBE, 0xC7));
        // El modo de selección va en Display: con -1 la pieza queda como no
        // seleccionable y un Activate posterior no lo revierte.
        context->Display(part, AIS_Shaded, AIS_Shape::SelectionMode(TopAbs_FACE), false);
        context->Activate(part, AIS_Shape::SelectionMode(TopAbs_EDGE));
    }

    Viewport& widget_;
    Handle(AIS_Shape) part;
    std::vector<OriginPlane> originPlanes;
    Handle(SketchPlaneFilter) planeFilter;
    Handle(SketchGrid) grid;
    Handle(SketchDrawing) drawing;
    Handle(SketchDrawing) preview;
    sketch::Plane sketchPlane;
    bool darkTheme = true;
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

bool Viewport::isSketching() const { return impl_->mode == Impl::Mode::Sketch; }

void Viewport::lookAtSketch()
{
    impl_->lookAtSketch();
    update();
}

void Viewport::showSketch(const sketch::Sketch& sketch, const std::vector<sketch::EntityId>& underconstrained)
{
    if (isSketching()) {
        impl_->showSketch(sketch, underconstrained);
        update();
    }
}

void Viewport::showSketchPreview(const std::vector<sketch::Geometry>& geometry, std::optional<sketch::Vec2> snapPoint)
{
    if (isSketching()) {
        impl_->showSketchPreview(geometry, snapPoint);
        update();
    }
}

void Viewport::setSketchToolActive(bool active)
{
    impl_->sketchToolActive = active;
    impl_->leftPressed = false;
    setCursor(active ? Qt::CrossCursor : Qt::ArrowCursor);
}

double Viewport::sketchPixelSize() const { return impl_->pixelSize(); }

double Viewport::sketchGridStep() const { return impl_->gridStep(); }

Qt::MouseButtons Viewport::navigationButtons(Qt::MouseButtons buttons) const
{
    if (impl_->sketchToolActive && isSketching()) {
        buttons &= ~Qt::MouseButtons(Qt::LeftButton);
    }
    return buttons;
}

void Viewport::initializeGL()
{
    const qreal ratio = devicePixelRatioF();
    const Graphic3d_Vec2i viewSize(int(width() * ratio), int(height() * ratio));

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
    if (event->button() == Qt::LeftButton && navigationButtons(Qt::LeftButton) == Qt::NoButton) {
        impl_->leftPressed = true;
        impl_->pressPosition = event->position();
    }
    const qreal ratio = devicePixelRatioF();
    const Graphic3d_Vec2i point(int(event->position().x() * ratio), int(event->position().y() * ratio));
    if (impl_->UpdateMouseButtons(point, toOcctButtons(navigationButtons(event->buttons())),
                                  toOcctModifiers(event->modifiers()), false)) {
        update();
    }
}

void Viewport::mouseReleaseEvent(QMouseEvent* event)
{
    const qreal ratio = devicePixelRatioF();
    const Graphic3d_Vec2i point(int(event->position().x() * ratio), int(event->position().y() * ratio));
    if (event->button() == Qt::LeftButton && impl_->leftPressed) {
        impl_->leftPressed = false;
        // Un clic, no un arrastre; tras un doble clic se ignora la suelta.
        const QPointF moved = event->position() - impl_->pressPosition;
        if (impl_->ignoreNextRelease) {
            impl_->ignoreNextRelease = false;
        } else if (moved.manhattanLength() <= 5.0) {
            if (const auto p = impl_->sketchPointAt(point)) {
                emit sketchClicked(p->x, p->y);
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
    if (event->button() == Qt::LeftButton && navigationButtons(Qt::LeftButton) == Qt::NoButton) {
        impl_->leftPressed = true;
        impl_->pressPosition = event->position();
        impl_->ignoreNextRelease = true;
        emit sketchDoubleClicked();
        return;
    }
    QOpenGLWidget::mouseDoubleClickEvent(event);
}

void Viewport::mouseMoveEvent(QMouseEvent* event)
{
    const qreal ratio = devicePixelRatioF();
    const Graphic3d_Vec2i point(int(event->position().x() * ratio), int(event->position().y() * ratio));
    if (impl_->UpdateMousePosition(point, toOcctButtons(navigationButtons(event->buttons())),
                                   toOcctModifiers(event->modifiers()), false)) {
        update();
    }
    if (isSketching()) {
        const auto p = impl_->sketchPointAt(point);
        emit sketchCursorMoved(p.has_value(), p ? p->x : 0.0, p ? p->y : 0.0);
    }
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
    const Graphic3d_Vec2i point(int(event->position().x() * ratio), int(event->position().y() * ratio));
    // angleDelta viene en octavos de grado (120 por paso de rueda).
    if (impl_->UpdateZoom(Aspect_ScrollDelta(point, event->angleDelta().y() / 8.0))) {
        update();
    }
}

} // namespace nuf::render
