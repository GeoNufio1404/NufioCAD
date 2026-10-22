#include <nuf/render/Viewport.hpp>

// En Windows, antes que OCCT: OCCT 8 incluye <windows.h> con su propia
// configuración y después ya no estarían wglGetCurrentDC ni WindowFromDC.
#ifdef _WIN32
#include <windows.h>
#endif

#include <nuf/geometry/Occt.hpp>
#include <nuf/sketch/Annotations.hpp>
#include <nuf/sketch/Profiles.hpp>
#include <nuf/sketch/Tools.hpp>

#include <AIS_AnimationCamera.hxx>
#include <AIS_InteractiveContext.hxx>
#include <AIS_MouseGesture.hxx>
#include <AIS_Shape.hxx>
#include <AIS_TexturedShape.hxx>
#include <AIS_ViewController.hxx>
#include <AIS_ViewCube.hxx>
#include <Aspect_DisplayConnection.hxx>
#include <Aspect_NeutralWindow.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Section.hxx>
#include <BRep_Tool.hxx>
#include <StdPrs_BRepFont.hxx>
#include <StdPrs_BRepTextBuilder.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <NCollection_IndexedMap.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRep_Builder.hxx>
#include <GC_MakeArcOfCircle.hxx>
#include <GC_MakeArcOfEllipse.hxx>
#include <GeomConvert.hxx>
#include <GeomConvert_BSplineCurveToBezierCurve.hxx>
#include <Geom_BSplineCurve.hxx>
#include <Geom_BezierCurve.hxx>
#include <gp_Elips.hxx>
#include <TopoDS_Compound.hxx>
#include <gp_Circ.hxx>
#include <BRepPrimAPI_MakeCone.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <Bnd_Box.hxx>
#include <Font_FontMgr.hxx>
#include <Graphic3d_ArrayOfPoints.hxx>
#include <Graphic3d_ArrayOfPolylines.hxx>
#include <Graphic3d_ArrayOfSegments.hxx>
#include <Graphic3d_AspectLine3d.hxx>
#include <Graphic3d_AspectMarker3d.hxx>
#include <Graphic3d_AspectText3d.hxx>
#include <Graphic3d_Text.hxx>
#include <Graphic3d_Vec2.hxx>
#include <Graphic3d_ZLayerSettings.hxx>
#include <Image_PixMap.hxx>
#include <NCollection_Array1.hxx>
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
#include <QImage>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QStringList>
#include <QSurfaceFormat>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <numbers>
#include <optional>
#include <tuple>
#include <utility>
#include <vector>

namespace nuf::render {

namespace {

// Píxeles (OCCT 8 retira el alias Graphic3d_Vec2i).
using Vec2i = NCollection_Vec2<int>;

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

using geometry::profileFaces;
using geometry::projectEdge;
using geometry::sketchEdge;
using geometry::toAx3;
using geometry::toDir;
using geometry::toPnt;
using geometry::toVec3;

// Todas las caras de los perfiles, juntas (para sombrearlas).
TopoDS_Shape profileCompound(const sketch::Sketch& sketch, const gp_Ax3& plane)
{
    BRep_Builder builder;
    TopoDS_Compound compound;
    builder.MakeCompound(compound);
    for (const TopoDS_Face& face : profileFaces(sketch, plane)) {
        builder.Add(compound, face);
    }
    return compound;
}

// Durante la elección del plano del sketch solo se aceptan caras planas de la
// pieza; lo que no es una forma (planos de origen, ViewCube) pasa siempre.
class SketchPlaneFilter final : public SelectMgr_Filter {
    DEFINE_STANDARD_RTTI_INLINE(SketchPlaneFilter, SelectMgr_Filter)

public:
    bool IsOk(const Handle(SelectMgr_EntityOwner)& owner) const override
    {
        Handle(StdSelect_BRepOwner) brep = Handle(StdSelect_BRepOwner)::DownCast(owner);
        if (brep.IsNull() || !brep->HasShape()) {
            return true;
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
                 const Handle(Prs3d_Presentation)& prs, const int mode) override
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
                          const int /*mode*/) override
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
    TCollection_AsciiString font;

    void clear()
    {
        curves_.clear();
        points_.clear();
        texts_.clear();
    }

    // Texto centrado en p, sobre un recuadro del color «box».
    void addText(sketch::Vec2 p, const std::string& utf8, const Quantity_Color& color, const Quantity_Color& box)
    {
        texts_.push_back({p, utf8, color, box});
    }

    void addGeometry(const sketch::Geometry& geometry, const SketchStroke& stroke)
    {
        if (const auto* point = std::get_if<sketch::Point>(&geometry)) {
            addPoint(point->position, stroke.color, 1.0);
            return;
        }
        addPolyline(sketch::polyline(geometry), stroke);
    }

    void addPolyline(std::vector<sketch::Vec2> points, const SketchStroke& stroke)
    {
        if (points.size() >= 2) {
            curves_.push_back({std::move(points), stroke});
        }
    }

    void addPoint(sketch::Vec2 p, const Quantity_Color& color, double scale)
    {
        points_.push_back({p, color, scale});
    }

    void Compute(const Handle(PrsMgr_PresentationManager)& /*manager*/,
                 const Handle(Prs3d_Presentation)& prs, const int mode) override
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
        for (const Label& label : texts_) {
            Handle(Graphic3d_Group) group = prs->NewGroup();
            Handle(Graphic3d_AspectText3d) aspect = new Graphic3d_AspectText3d(
                label.color, font.ToCString(), 1.0, 0.0, Aspect_TOST_NORMAL, Aspect_TODT_SUBTITLE);
            aspect->SetColorSubTitle(label.box);
            group->SetGroupPrimitivesAspect(aspect);
            Handle(Graphic3d_Text) text = new Graphic3d_Text(13.0f);
            text->SetText(label.text.c_str());
            text->SetPosition(at(label.position));
            text->SetHorizontalAlignment(Graphic3d_HTA_CENTER);
            text->SetVerticalAlignment(Graphic3d_VTA_CENTER);
            group->AddText(text);
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
                          const int /*mode*/) override
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
    struct Label {
        sketch::Vec2 position;
        std::string text;
        Quantity_Color color;
        Quantity_Color box;
    };

    gp_Pnt at(sketch::Vec2 p) const
    {
        return plane.Location().Translated(gp_Vec(plane.XDirection()) * p.x + gp_Vec(plane.YDirection()) * p.y);
    }

    std::vector<Curve> curves_;
    std::vector<Marker> points_;
    std::vector<Label> texts_;
};

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

        planeFilter = new SketchPlaneFilter();
        grid = new SketchGrid();
        // Capa propia para las regiones (perfiles) de los sketches: respeta la
        // profundidad de la pieza (lo que está delante la tapa) y, por ser una
        // capa superior, al seleccionar gana a la pieza. Para que sobre una
        // cara en el mismo plano no se peleen, cada región se acerca a la
        // cámara con su propio desplazamiento (pullTowardCamera).
        Graphic3d_ZLayerSettings regionSettings;
        regionSettings.SetName("Regiones de sketch");
        regionSettings.SetClearDepth(false);
        regionSettings.SetEnableDepthTest(true);
        regionSettings.SetEnableDepthWrite(true);
        viewer->InsertLayerBefore(regionLayer, regionSettings, Graphic3d_ZLayerId_Topmost);
        // La geometría del sketch se ve aunque la pieza la tape (por ejemplo,
        // un sketch en el plano XY bajo la pieza).
        drawing = new SketchDrawing();
        drawing->SetZLayer(Graphic3d_ZLayerId_Topmost);
        drawing->font = pickLabelFont();
        preview = new SketchDrawing();
        preview->SetZLayer(Graphic3d_ZLayerId_Topmost);
        profiles = new AIS_Shape(TopoDS_Shape());
        profiles->SetZLayer(regionLayer);
        profiles->SetColor(rgb(0x6C, 0xA6, 0xFF));
        profiles->SetTransparency(0.82);
        profiles->Attributes()->SetFaceBoundaryDraw(false);
        pullTowardCamera(profiles);
    }

    // Las caras de la pieza ya se alejan un poco (factor 1); esto acerca la
    // región aunque esté inclinada respecto a la vista.
    static void pullTowardCamera(const Handle(AIS_Shape)& shape)
    {
        shape->Attributes()->ShadingAspect()->Aspect()->SetPolygonOffsets(Aspect_POM_Fill, -2.0f, -4.0f);
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
        int regions = 0;
        int curves = 0;
        QStringList sketchNames;
        for (context->InitSelected(); context->MoreSelected(); context->NextSelected()) {
            // Lo de un sketch terminado: perfiles (regiones) y curvas.
            if (const auto owner = finishedOwner(context->SelectedInteractive())) {
                ++(owner->second ? regions : curves);
                const QString& name = finished.at(owner->first).name;
                if (!sketchNames.contains(name)) {
                    sketchNames << name;
                }
                continue;
            }
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
        if (regions + curves > 0) {
            QStringList what;
            if (regions > 0) {
                what << (regions == 1 ? QStringLiteral("1 perfil") : QStringLiteral("%1 perfiles").arg(regions));
            }
            if (curves > 0) {
                what << (curves == 1 ? QStringLiteral("1 curva") : QStringLiteral("%1 curvas").arg(curves));
            }
            parts << QStringLiteral("%1 de %2").arg(what.join(QStringLiteral(" y ")), sketchNames.join(QStringLiteral(", ")));
        }
        emit widget_.selectionChanged(parts.isEmpty() ? QString()
                                                      : QStringLiteral("Seleccionado: ") + parts.join(QStringLiteral(", ")));
        emit widget_.modelSelectionChanged();
    }

    // --- Cuerpos del modelo ------------------------------------------------

    struct ShownBody {
        std::string key;
        geometry::Shape shape;
        Handle(AIS_Shape) ais;
    };

    void showBodies(const std::vector<document::Body>& list)
    {
        std::vector<ShownBody> next;
        for (const document::Body& body : list) {
            const auto old = std::ranges::find_if(bodies, [&](const ShownBody& b) {
                return b.key == body.key && b.shape.impl() == body.shape.impl();
            });
            if (old != bodies.end()) {
                next.push_back(*old);
                bodies.erase(old);
                continue;
            }
            Handle(AIS_Shape) ais = new AIS_Shape(geometry::occtShape(body.shape));
            ais->SetMaterial(Graphic3d_MaterialAspect(Graphic3d_NameOfMaterial_Plastified));
            ais->SetColor(rgb(0xB7, 0xBE, 0xC7));
            // El modo de selección va en Display: con -1 el cuerpo queda como
            // no seleccionable y un Activate posterior no lo revierte.
            context->Display(ais, AIS_Shaded, AIS_Shape::SelectionMode(TopAbs_FACE), false);
            next.push_back({body.key, body.shape, ais});
        }
        for (const ShownBody& gone : bodies) {
            context->Remove(gone.ais, false);
        }
        bodies = std::move(next);
        for (const ShownBody& b : bodies) {
            activateBody(b);
        }
    }

    // Modos de selección de un cuerpo según lo que se esté haciendo.
    void activateBody(const ShownBody& b)
    {
        context->Deactivate(b.ais);
        switch (mode) {
        case Mode::Model:
            context->Activate(b.ais, AIS_Shape::SelectionMode(TopAbs_FACE));
            context->Activate(b.ais, AIS_Shape::SelectionMode(TopAbs_EDGE));
            break;
        case Mode::PickPlane:
            context->Activate(b.ais, AIS_Shape::SelectionMode(TopAbs_FACE));
            break;
        case Mode::Sketch:
            if (projectMode) {
                context->Activate(b.ais, AIS_Shape::SelectionMode(TopAbs_EDGE));
            }
            break;
        }
    }

    void activateBodies()
    {
        for (const ShownBody& b : bodies) {
            activateBody(b);
        }
    }

    const ShownBody* bodyOf(const Handle(AIS_InteractiveObject)& object) const
    {
        const auto it = std::ranges::find_if(bodies, [&](const ShownBody& b) { return b.ais == object; });
        return it == bodies.end() ? nullptr : &*it;
    }

    // Todos los cuerpos en una forma (para cortar con el plano, medir…).
    TopoDS_Shape allBodies() const
    {
        BRep_Builder builder;
        TopoDS_Compound all;
        builder.MakeCompound(all);
        for (const ShownBody& b : bodies) {
            builder.Add(all, geometry::occtShape(b.shape));
        }
        return all;
    }

    void showToolPreview(const geometry::Shape& tool, bool removes)
    {
        clearToolPreview();
        if (tool.isNull()) {
            return;
        }
        toolPreview = new AIS_Shape(geometry::occtShape(tool));
        toolPreview->SetColor(removes ? rgb(0xE5, 0x48, 0x4D) : rgb(0x6C, 0xA6, 0xFF));
        toolPreview->SetTransparency(0.55);
        toolPreview->Attributes()->SetFaceBoundaryDraw(false);
        context->Display(toolPreview, AIS_Shaded, -1, false);
    }

    void clearToolPreview()
    {
        if (!toolPreview.IsNull()) {
            context->Remove(toolPreview, false);
            toolPreview.Nullify();
        }
    }

    ModelSelection modelSelection() const
    {
        ModelSelection result;
        for (context->InitSelected(); context->MoreSelected(); context->NextSelected()) {
            const Handle(AIS_InteractiveObject) object = context->SelectedInteractive();
            for (const auto& [id, shown] : finished) {
                for (std::size_t r = 0; r < shown.regions.size(); ++r) {
                    if (shown.regions[r].first == object) {
                        result.profiles.push_back({id, shown.regions[r].second});
                    }
                }
                if (object == shown.curves && context->HasSelectedShape()) {
                    for (const auto& [edge, entity] : shown.curveEdges) {
                        if (edge.IsSame(context->SelectedShape())) {
                            result.curves.push_back({id, entity});
                        }
                    }
                }
            }
            const ShownBody* body = bodyOf(object);
            if (body == nullptr || !context->HasSelectedShape()) {
                continue;
            }
            const TopoDS_Shape& picked = context->SelectedShape();
            if (picked.ShapeType() == TopAbs_FACE) {
                if (const auto index = geometry::faceIndexOf(body->shape, picked)) {
                    result.faces.push_back({body->key, body->shape.faceRef(*index)});
                }
            } else if (picked.ShapeType() == TopAbs_EDGE) {
                if (const auto index = geometry::edgeIndexOf(body->shape, picked)) {
                    result.edges.push_back({body->key, body->shape.edgeRef(*index)});
                }
            }
        }
        return result;
    }

    void setAdditiveSelection(bool on)
    {
        // Clic sin modificadores: alterna (como Shift) o sustituye.
        ChangeMouseSelectionSchemes().Bind(Aspect_VKeyMouse_LeftButton,
                                           on ? AIS_SelectionScheme_XOR : defaultClickScheme);
    }

    // --- Flecha de distancia -------------------------------------------------

    void showDistanceHandle(const sketch::Vec3& origin, const sketch::Vec3& direction, double distance)
    {
        hideDistanceHandle();
        handleOrigin = toPnt(origin);
        handleDirection = toDir(direction);
        handleDistance = distance;
        // Tamaño fijo en pantalla (aprox.): se recalcula al volver a mostrarla.
        const double size = 70.0 * pixelSize();
        const gp_Dir pointing = distance >= 0.0 ? handleDirection : handleDirection.Reversed();
        const gp_Pnt tip = handleOrigin.Translated(gp_Vec(handleDirection) * distance);
        const gp_Pnt base = tip.Translated(gp_Vec(pointing) * (0.15 * size));
        BRep_Builder builder;
        TopoDS_Compound arrow;
        builder.MakeCompound(arrow);
        builder.Add(arrow, BRepPrimAPI_MakeCylinder(gp_Ax2(base, pointing), 0.035 * size, 0.6 * size).Shape());
        builder.Add(arrow, BRepPrimAPI_MakeCone(gp_Ax2(base.Translated(gp_Vec(pointing) * (0.6 * size)), pointing),
                                                0.12 * size, 0.0, 0.3 * size)
                               .Shape());
        handle = new AIS_Shape(arrow);
        handle->SetColor(rgb(0xF2, 0xA3, 0x4B));
        handle->Attributes()->SetFaceBoundaryDraw(false);
        handle->SetZLayer(Graphic3d_ZLayerId_Topmost);
        context->Display(handle, AIS_Shaded, 0, false);
    }

    void hideDistanceHandle()
    {
        if (!handle.IsNull()) {
            context->Remove(handle, false);
            handle.Nullify();
        }
        handleDragging = false;
    }

    // Parámetro sobre el eje de la flecha del punto más cercano al rayo del píxel.
    std::optional<double> handleParameterAt(const Vec2i& pixel) const
    {
        double x = 0, y = 0, z = 0, dx = 0, dy = 0, dz = 0;
        view->ConvertWithProj(pixel.x(), pixel.y(), x, y, z, dx, dy, dz);
        const gp_Vec ray(dx, dy, dz);
        if (ray.Magnitude() < 1e-12) {
            return std::nullopt;
        }
        const gp_Vec v = ray.Normalized();
        const gp_Vec d(handleDirection);
        const gp_Vec w(handleOrigin, gp_Pnt(x, y, z));
        const double b = v.Dot(d);
        const double denominator = 1.0 - b * b;
        if (denominator < 1e-6) {
            return std::nullopt;  // mirando a lo largo del eje
        }
        return (w.Dot(d) - b * w.Dot(v)) / denominator;
    }

    // --- Modo sketch -------------------------------------------------------

    void pickSketchPlane()
    {
        if (mode != Mode::Model) {
            return;
        }
        createOriginPlanes();
        context->ClearSelected(false);
        for (const OriginPlane& origin : originPlanes) {
            context->Display(origin.shape, AIS_Shaded, 0, false);
            context->Activate(origin.shape, 0);
        }
        context->AddFilter(planeFilter);
        mode = Mode::PickPlane;
        activateBodies();
        emit widget_.selectionChanged(QString());
    }

    void cancelPickSketchPlane()
    {
        if (mode != Mode::PickPlane) {
            return;
        }
        context->ClearSelected(false);
        showOrigin();
        context->RemoveFilter(planeFilter);
        mode = Mode::Model;
        activateBodies();
    }

    void beginSketch(const sketch::Plane& plane)
    {
        cancelPickSketchPlane();
        if (mode == Mode::Sketch) {
            endSketch();
        }
        context->ClearSelected(false);
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
        activateBodies();
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
        context->Erase(profiles, false);
        shownSketch.reset();
        projectMode = false;
        for (const Handle(AIS_InteractiveObject)& image : images) {
            context->Remove(image, false);
        }
        images.clear();
        mode = Mode::Model;
        activateBodies();
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
    std::optional<sketch::Vec2> sketchPointAt(const Vec2i& pixel) const
    {
        double x = 0, y = 0, z = 0, dx = 0, dy = 0, dz = 0;
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

    static Quantity_Color selectedColor() { return rgb(0xFF, 0xAA, 0x33); }
    static Quantity_Color hoveredColor() { return rgb(0xFF, 0xD5, 0x8A); }

    void showSketch(const sketch::Sketch& sketch, const std::vector<sketch::EntityId>& underconstrained,
                    const SketchHighlight& highlight)
    {
        shownSketch = sketch;
        shownUnderconstrained = underconstrained;
        shownHighlight = highlight;
        rebuildSketch();

        // Regiones cerradas, sombreadas como en Fusion.
        profiles->SetShape(profileCompound(sketch, toAx3(sketchPlane)));
        profiles->SetToUpdate();
        if (context->IsDisplayed(profiles)) {
            context->Redisplay(profiles, false);
        } else {
            context->Display(profiles, AIS_Shaded, -1, false);
        }
    }

    // Vuelve a dibujar el último sketch mostrado (también al cambiar el zoom,
    // porque las anotaciones se colocan en píxeles).
    void rebuildSketch()
    {
        if (!shownSketch) {
            return;
        }
        const sketch::Sketch& sketch = *shownSketch;
        const std::vector<sketch::EntityId>& underconstrained = shownUnderconstrained;
        const SketchHighlight& highlight = shownHighlight;
        const auto same = [](sketch::Ref a, sketch::Ref b) { return a == b; };
        const auto isSelected = [&](sketch::Ref r) {
            return std::ranges::any_of(highlight.selected, [&](sketch::Ref s) { return same(s, r); });
        };
        // Color de resaltado de una referencia, si lo tiene.
        const auto highlightOf = [&](sketch::Ref r) -> std::optional<Quantity_Color> {
            if (isSelected(r)) {
                return selectedColor();
            }
            if (same(highlight.hovered, r)) {
                return hoveredColor();
            }
            return std::nullopt;
        };

        drawing->clear();
        for (const sketch::Entity& e : sketch.entities()) {
            const auto whole = highlightOf(sketch::Ref::of(e.id));
            if (e.reference) {
                // El origen y los ejes ya los dibuja la rejilla: solo se
                // resaltan (los ejes, como un tramo largo).
                if (!whole) {
                    continue;
                }
                if (const auto* axis = std::get_if<sketch::Line>(&e.geometry)) {
                    constexpr double kLong = 1e4;
                    const sketch::Vec2 d{axis->end.x - axis->start.x, axis->end.y - axis->start.y};
                    drawing->addGeometry(sketch::Line{{-kLong * d.x, -kLong * d.y}, {kLong * d.x, kLong * d.y}},
                                         {*whole, 2.0});
                } else if (const auto* origin = std::get_if<sketch::Point>(&e.geometry)) {
                    drawing->addPoint(origin->position, *whole, 2.2);
                }
                continue;
            }
            const bool loose = std::ranges::find(underconstrained, e.id) != underconstrained.end();
            SketchStroke stroke = e.construction ? constructionStroke() : (loose ? freeStroke() : fixedStroke());
            if (e.locked && !e.construction) {
                stroke.color = rgb(0xB0, 0x7C, 0xFF);  // bloqueada: violeta, como lo proyectado en Fusion
            }
            if (whole) {
                stroke.color = *whole;
            }
            drawing->addGeometry(e.geometry, stroke);
            // Extremos y centros como puntos (más grandes si están resaltados).
            // (En la geometría bloqueada, como el texto, solo si están resaltados:
            // serían cientos de puntos.)
            const auto mark = [&](sketch::Part which) {
                if (const auto p = sketch.point({e.id, which})) {
                    const auto own = highlightOf({e.id, which});
                    if (own || !e.locked) {
                        drawing->addPoint(*p, own ? *own : stroke.color, own ? 2.2 : 1.6);
                    }
                }
            };
            for (sketch::Part which : {sketch::Part::Start, sketch::Part::End, sketch::Part::Center}) {
                mark(which);
            }
            // Spline seleccionada o bajo el cursor: polígono de control
            // (discontinuo) y polos intermedios.
            const auto* spline = std::get_if<sketch::BSpline>(&e.geometry);
            const bool showPoles = spline && (whole || [&] {
                for (int i = 0; i < int(spline->poles.size()); ++i) {
                    if (highlightOf(sketch::Ref::pole(e.id, i))) {
                        return true;
                    }
                }
                return false;
            }());
            if (showPoles) {
                drawing->addPolyline(spline->poles, {stroke.color, 1.0, Aspect_TOL_DOT});
                for (int i = 1; i + 1 < int(spline->poles.size()); ++i) {
                    const auto own = highlightOf(sketch::Ref::pole(e.id, i));
                    drawing->addPoint(spline->poles[std::size_t(i)], own ? *own : stroke.color, own ? 2.0 : 1.3);
                }
            }
        }

        // Restricciones y cotas.
        annotationScale = pixelSize();
        const Quantity_Color text = darkTheme ? rgb(0xE6, 0xE9, 0xED) : rgb(0x1F, 0x23, 0x29);
        const Quantity_Color dimmed = darkTheme ? rgb(0x9A, 0xA3, 0xAE) : rgb(0x6B, 0x74, 0x80);
        const Quantity_Color box = darkTheme ? rgb(0x2B, 0x30, 0x37) : rgb(0xF4, 0xF5, 0xF7);
        for (const sketch::Annotation& note : sketch::layoutAnnotations(sketch, annotationScale)) {
            Quantity_Color color = note.reference ? dimmed : text;
            if (std::ranges::find(highlight.selectedConstraints, note.constraint) != highlight.selectedConstraints.end()) {
                color = selectedColor();
            } else if (note.constraint == highlight.hoveredConstraint) {
                color = hoveredColor();
            }
            for (const sketch::Geometry& line : note.lines) {
                drawing->addGeometry(line, {color, 1.0});
            }
            drawing->addText(note.anchor, note.text, color, box);
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
        double width = 0, height = 0;
        view->Size(width, height);
        int pixelsWide = 0, pixelsHigh = 0;
        view->Window()->Size(pixelsWide, pixelsHigh);
        return width / std::max(pixelsWide, 1);
    }

    double gridStep() const { return grid->step; }

    void setProjectMode(bool on)
    {
        projectMode = on;
        activateBodies();
        context->ClearDetected(false);
    }

    // Arista de la pieza bajo el píxel, proyectada, y su referencia.
    std::pair<std::vector<sketch::Geometry>, geometry::ElementRef> projectionAt(const Vec2i& pixel)
    {
        context->MoveTo(pixel.x(), pixel.y(), view, false);
        // Bajo el cursor puede haber varias aristas (por ejemplo, la costura
        // de un cilindro, que vista de frente se proyecta en un punto): se
        // usa la primera que da algo.
        for (context->InitDetected(); context->MoreDetected(); context->NextDetected()) {
            Handle(StdSelect_BRepOwner) owner = Handle(StdSelect_BRepOwner)::DownCast(context->DetectedCurrentOwner());
            if (owner.IsNull() || !owner->HasShape() || owner->Shape().ShapeType() != TopAbs_EDGE) {
                continue;
            }
            auto projected = projectEdge(TopoDS::Edge(owner->Shape()), sketchPlane);
            if (!projected.empty()) {
                geometry::ElementRef source;
                const Handle(AIS_InteractiveObject) object = Handle(AIS_InteractiveObject)::DownCast(owner->Selectable());
                if (const ShownBody* body = bodyOf(object)) {
                    if (const auto index = geometry::edgeIndexOf(body->shape, owner->Shape())) {
                        source = body->shape.edgeRef(*index);
                    }
                }
                return {projected, source};
            }
        }
        return {};
    }

    std::vector<sketch::Geometry> textOutline(const QString& text, double height) const
    {
        StdPrs_BRepFont font;
        if (!font.FindAndInit(pickLabelFont(), Font_FA_Regular, height)) {
            return {};
        }
        StdPrs_BRepTextBuilder builder;
        const TopoDS_Shape shape = builder.Perform(font, NCollection_String(text.toUtf8().constData()));
        // El texto se crea en el plano XY del modelo: sus coordenadas son ya
        // las del sketch.
        NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher> edges;
        TopExp::MapShapes(shape, TopAbs_EDGE, edges);
        std::vector<sketch::Geometry> result;
        for (int i = 1; i <= edges.Extent(); ++i) {
            for (const sketch::Geometry& g : projectEdge(TopoDS::Edge(edges(i)), sketch::Plane::xy())) {
                result.push_back(g);
            }
        }
        return result;
    }

    bool showImage(const QString& path, sketch::Vec2 corner, double width, double height)
    {
        const TopoDS_Face face =
            BRepBuilderAPI_MakeFace(gp_Pln(toAx3(sketchPlane)), corner.x, corner.x + width, corner.y, corner.y + height);
        // Se carga con Qt: el OCCT de las distribuciones a menudo no lee PNG/JPG.
        const QImage picture = QImage(path).convertToFormat(QImage::Format_RGBA8888);
        if (picture.isNull()) {
            return false;
        }
        Handle(Image_PixMap) pixels = new Image_PixMap();
        if (!pixels->InitTrash(Image_Format_RGBA, std::size_t(picture.width()), std::size_t(picture.height()))) {
            return false;
        }
        pixels->SetTopDown(true);
        for (int row = 0; row < picture.height(); ++row) {
            std::memcpy(pixels->ChangeRow(std::size_t(row)), picture.constScanLine(row), std::size_t(picture.width()) * 4);
        }
        Handle(AIS_TexturedShape) image = new AIS_TexturedShape(face);
        image->SetTexturePixMap(pixels);
        image->SetTextureMapOn();
        image->SetTextureRepeat(false, 1.0, 1.0);
        image->DisableTextureModulate();  // colores de la imagen, sin sombreado
        image->SetTransparency(0.25);
        image->Attributes()->SetFaceBoundaryDraw(false);
        context->Display(image, 3, -1, false);  // 3 = modo con textura
        images.push_back(image);
        return true;
    }

    std::vector<sketch::Geometry> section() const
    {
        std::vector<sketch::Geometry> result;
        BRepAlgoAPI_Section cut(allBodies(), gp_Pln(toAx3(sketchPlane)));
        if (!cut.IsDone()) {
            return result;
        }
        for (TopExp_Explorer it(cut.Shape(), TopAbs_EDGE); it.More(); it.Next()) {
            for (const sketch::Geometry& g : projectEdge(TopoDS::Edge(it.Current()), sketchPlane)) {
                result.push_back(g);
            }
        }
        return result;
    }

    // Sketch terminado en la vista 3D: una forma por región (perfil),
    // seleccionable entera, y una con todas las curvas, seleccionables por
    // arista. Van por encima de la pieza: un sketch suele quedar dentro o
    // debajo de ella, y lo que se dibuja ahí debe poder elegirse.
    struct FinishedSketch {
        QString name;
        std::vector<std::pair<Handle(AIS_Shape), int>> regions;  // y su índice de perfil
        Handle(AIS_Shape) curves;
        std::vector<std::pair<TopoDS_Edge, sketch::EntityId>> curveEdges;
    };

    void showFinishedSketch(document::FeatureId id, const QString& name, const sketch::Sketch& sketch,
                            const sketch::Plane& plane)
    {
        hideFinishedSketch(id);
        FinishedSketch shown;
        shown.name = name;
        const gp_Ax3 ax3 = toAx3(plane);
        const std::vector<sketch::Profile> found = sketch::findProfiles(sketch);
        for (std::size_t i = 0; i < found.size(); ++i) {
            const auto face = geometry::profileFace(sketch, found[i], ax3);
            if (!face) {
                continue;
            }
            Handle(AIS_Shape) region = new AIS_Shape(*face);
            region->SetColor(rgb(0x6C, 0xA6, 0xFF));
            region->SetTransparency(0.8);
            region->Attributes()->SetFaceBoundaryDraw(false);
            pullTowardCamera(region);
            region->SetZLayer(regionLayer);
            context->Display(region, AIS_Shaded, 0, false);
            shown.regions.emplace_back(region, int(i));
        }
        BRep_Builder builder;
        TopoDS_Compound edges;
        builder.MakeCompound(edges);
        for (const sketch::Entity& e : sketch.entities()) {
            if (!e.reference && !e.construction) {
                if (const auto edge = sketchEdge(e, ax3)) {
                    builder.Add(edges, *edge);
                    shown.curveEdges.emplace_back(*edge, e.id);
                }
            }
        }
        shown.curves = new AIS_Shape(edges);
        shown.curves->SetColor(rgb(0x8F, 0xB3, 0xE8));
        shown.curves->SetWidth(2.0);
        shown.curves->SetZLayer(Graphic3d_ZLayerId_Topmost);
        context->Display(shown.curves, AIS_WireFrame, AIS_Shape::SelectionMode(TopAbs_EDGE), false);
        finished[id] = shown;
    }

    void hideFinishedSketch(document::FeatureId id)
    {
        const auto it = finished.find(id);
        if (it == finished.end()) {
            return;
        }
        for (const auto& [region, index] : it->second.regions) {
            context->Remove(region, false);
        }
        context->Remove(it->second.curves, false);
        finished.erase(it);
    }

    // Sketch terminado al que pertenece un objeto, y si es una región.
    std::optional<std::pair<document::FeatureId, bool>> finishedOwner(const Handle(AIS_InteractiveObject)& object) const
    {
        for (const auto& [id, shown] : finished) {
            if (object == shown.curves) {
                return std::pair{id, false};
            }
            for (const auto& [region, index] : shown.regions) {
                if (object == region) {
                    return std::pair{id, true};
                }
            }
        }
        return std::nullopt;
    }

    Handle(V3d_Viewer) viewer;
    Handle(V3d_View) view;
    Handle(AIS_InteractiveContext) context;
    Handle(AIS_ViewCube) viewCube;
    Mode mode = Mode::Model;
    bool sketchToolActive = false;
    bool projectMode = false;
    bool sketchDragging = false;
    std::optional<sketch::Sketch> shownSketch;
    std::vector<sketch::EntityId> shownUnderconstrained;
    SketchHighlight shownHighlight;
    double annotationScale = 0.0;
    bool leftPressed = false;
    std::map<document::FeatureId, FinishedSketch> finished;
    bool originShown = false;
    Handle(AIS_Shape) handle;  // flecha de distancia
    double handleDistance = 0.0;
    bool handleDragging = false;
    double handleGrab = 0.0;  // parámetro del eje donde se agarró, menos la distancia
    bool ignoreNextRelease = false;
    QPointF pressPosition;
    std::optional<sketch::Vec2> pressSketchPoint;  // dónde se pulsó, en el plano

protected:
    // Pide otro cuadro mientras haya animaciones (ViewCube, inercia).
    void handleViewRedraw(const Handle(AIS_InteractiveContext)& ctx,
                          const Handle(V3d_View)& v) override
    {
        // La cámara ya está actualizada para este cuadro: se ajusta la rejilla
        // antes de dibujar.
        if (mode == Mode::Sketch) {
            updateGrid(false);
            if (shownSketch && std::abs(pixelSize() - annotationScale) > 0.01 * annotationScale) {
                rebuildSketch();
            }
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
        auto& gestures = ChangeMouseGestureMap();
        gestures.Clear();
        gestures.Bind(key(Aspect_VKeyMouse_LeftButton), AIS_MouseGesture_RotateOrbit);
        gestures.Bind(key(Aspect_VKeyMouse_LeftButton, Aspect_VKeyFlags_SHIFT), AIS_MouseGesture_SelectRectangle);
        gestures.Bind(key(Aspect_VKeyMouse_RightButton), AIS_MouseGesture_Pan);
        gestures.Bind(key(Aspect_VKeyMouse_MiddleButton), AIS_MouseGesture_Pan);
        gestures.Bind(key(Aspect_VKeyMouse_MiddleButton, Aspect_VKeyFlags_SHIFT), AIS_MouseGesture_RotateOrbit);
        MouseSelectionSchemes().Find(Aspect_VKeyMouse_LeftButton, defaultClickScheme);
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
            // En la capa del propio objeto (OCCT lo pone en Top, encima de
            // todo): así lo que está delante, como un cilindro, lo tapa.
            context->HighlightStyle(type)->SetZLayer(Graphic3d_ZLayerId_UNKNOWN);
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
            new Graphic3d_TransformPers(Graphic3d_TMF_TriedronPers, Aspect_TOTP_RIGHT_UPPER, Vec2i(85, 85)));
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
        document::OriginPlane kind;
    };

    // Planos XY, XZ e YZ como cuadrados translúcidos en el cuadrante positivo,
    // con un tamaño proporcional a la pieza (como en Fusion 360).
public:
    // Fuera de la elección del plano: a la vista (sin seleccionarse) o no.
    void showOrigin()
    {
        if (originShown && originPlanes.empty()) {
            createOriginPlanes();
        }
        for (const OriginPlane& origin : originPlanes) {
            if (originShown) {
                context->Display(origin.shape, AIS_Shaded, 0, false);
                context->Deactivate(origin.shape);
            } else {
                context->Erase(origin.shape, false);
            }
        }
    }

private:
    void createOriginPlanes()
    {
        for (const OriginPlane& origin : originPlanes) {
            context->Remove(origin.shape, false);
        }
        originPlanes.clear();
        Bnd_Box box;
        BRepBndLib::Add(allBodies(), box);
        double size = 20.0;
        if (!box.IsVoid()) {
            size = std::max(size, 0.6 * std::sqrt(box.SquareExtent()) / std::sqrt(3.0));
        }
        const std::tuple<sketch::Plane, QString, document::OriginPlane> planes[] = {
            {sketch::Plane::xy(), QStringLiteral("Plano XY"), document::OriginPlane::XY},
            {sketch::Plane::xz(), QStringLiteral("Plano XZ"), document::OriginPlane::XZ},
            {sketch::Plane::yz(), QStringLiteral("Plano YZ"), document::OriginPlane::YZ}};
        for (const auto& [plane, name, kind] : planes) {
            const TopoDS_Face face = BRepBuilderAPI_MakeFace(gp_Pln(toAx3(plane)), 0.0, size, 0.0, size);
            Handle(AIS_Shape) shape = new AIS_Shape(face);
            shape->SetColor(rgb(0xF2, 0xA3, 0x4B));
            shape->SetTransparency(0.6);
            // Por encima de la pieza: suelen atravesarla y quedarían ocultos.
            shape->SetZLayer(Graphic3d_ZLayerId_Topmost);
            originPlanes.push_back({shape, plane, name, kind});
        }
    }

    void pickedSketchPlane()
    {
        context->InitSelected();
        if (!context->MoreSelected()) {
            return;
        }
        std::optional<std::pair<sketch::Plane, QString>> picked;
        document::SketchAttachment attachment;
        const Handle(AIS_InteractiveObject) object = context->SelectedInteractive();
        for (const OriginPlane& origin : originPlanes) {
            if (origin.shape == object) {
                picked.emplace(origin.plane, origin.name);
                attachment.origin = origin.kind;
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
                attachment.origin.reset();
                if (const ShownBody* body = bodyOf(object)) {
                    if (const auto index = geometry::faceIndexOf(body->shape, face)) {
                        attachment.face = body->shape.faceRef(*index);
                    }
                }
            }
        }
        context->ClearSelected(false);
        if (!picked) {
            return;
        }
        attachment.lastPlane = picked->first;
        // Se avisa fuera del procesado de eventos de OCCT: quien recibe la
        // señal cambia el contexto (beginSketch).
        QMetaObject::invokeMethod(
            &widget_, [widget = &widget_, plane = picked->first, name = picked->second, attachment] {
                emit widget->sketchPlanePicked(plane, name, attachment);
            },
            Qt::QueuedConnection);
    }

    // Paso de la rejilla según el zoom: la menor potencia de 10 cuyas líneas
    // queden a 7 px o más. Región centrada en lo que se mira.
    void updateGrid(bool force)
    {
        double width = 0, height = 0;
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

    Viewport& widget_;
    std::vector<ShownBody> bodies;
    Handle(AIS_Shape) toolPreview;
    gp_Pnt handleOrigin;
    gp_Dir handleDirection;
    AIS_SelectionScheme defaultClickScheme = AIS_SelectionScheme_Replace;
    std::vector<OriginPlane> originPlanes;
    Handle(SketchPlaneFilter) planeFilter;
    Handle(SketchGrid) grid;
    Handle(SketchDrawing) drawing;
    Handle(SketchDrawing) preview;
    Handle(AIS_Shape) profiles;
    Graphic3d_ZLayerId regionLayer = Graphic3d_ZLayerId_UNKNOWN;
    std::vector<Handle(AIS_InteractiveObject)> images;  // de referencia, del sketch en curso
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

void Viewport::setOriginVisible(bool visible)
{
    impl_->originShown = visible;
    if (impl_->mode != Impl::Mode::PickPlane) {
        impl_->showOrigin();
    }
    impl_->view->Invalidate();
    update();
}

void Viewport::showBodies(const std::vector<document::Body>& bodies)
{
    impl_->showBodies(bodies);
    impl_->view->Invalidate();
    update();
}

void Viewport::showToolPreview(const geometry::Shape& tool, bool removes)
{
    impl_->showToolPreview(tool, removes);
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

void Viewport::showDistanceHandle(const sketch::Vec3& origin, const sketch::Vec3& direction, double distance)
{
    impl_->showDistanceHandle(origin, direction, distance);
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

std::vector<sketch::Geometry> Viewport::textOutline(const QString& text, double height) const
{
    return impl_->textOutline(text, height);
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
    if (((impl_->sketchToolActive || impl_->sketchDragging) && isSketching()) || impl_->handleDragging) {
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

void Viewport::mousePressEvent(QMouseEvent* event)
{
    const qreal ratio = devicePixelRatioF();
    const Vec2i point(int(event->position().x() * ratio), int(event->position().y() * ratio));
    if (event->button() == Qt::LeftButton && !isSketching() && !impl_->handle.IsNull()) {
        // ¿Se agarra la flecha de distancia?
        impl_->context->MoveTo(point.x(), point.y(), impl_->view, false);
        if (impl_->context->HasDetected() && impl_->context->DetectedInteractive() == impl_->handle) {
            if (const auto t = impl_->handleParameterAt(point)) {
                impl_->handleDragging = true;
                impl_->handleGrab = *t - impl_->handleDistance;
            }
        }
    }
    if (event->button() == Qt::LeftButton && isSketching()) {
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
    if (event->button() == Qt::LeftButton && impl_->handleDragging) {
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
