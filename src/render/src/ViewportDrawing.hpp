#pragma once

// Interno del visor: lo que dibuja (cuadrícula, trazos del sketch…). Solo lo incluye Viewport*.cpp.

#include <nuf/render/Viewport.hpp>

// En Windows, antes que OCCT: OCCT 8 incluye <windows.h> con su propia
// configuración y después ya no estarían wglGetCurrentDC ni WindowFromDC.
#ifdef _WIN32
#include <windows.h>
#endif

#include <nuf/geometry/DisplayMesh.hpp>
#include <nuf/geometry/Occt.hpp>
#include <nuf/sketch/Annotations.hpp>
#include <nuf/sketch/Profiles.hpp>
#include <nuf/sketch/Tools.hpp>

#include <AIS_AnimationCamera.hxx>
#include <AIS_InteractiveContext.hxx>
#include <AIS_MouseGesture.hxx>
#include <TopLoc_Location.hxx>
#include <gp_Trsf.hxx>
#include <AIS_Shape.hxx>
#include <AIS_TexturedShape.hxx>
#include <AIS_ViewController.hxx>
#include <AIS_ViewCube.hxx>
#include <Aspect_DisplayConnection.hxx>
#include <Aspect_NeutralWindow.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <Aspect_TypeOfDeflection.hxx>
#include <Prs3d_Drawer.hxx>
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
#include <QElapsedTimer>
#include <QTimer>

#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCone.hxx>
#include <BRepPrimAPI_MakeTorus.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <Bnd_Box.hxx>
#include <Font_FontMgr.hxx>
#include <Graphic3d_ArrayOfPoints.hxx>
#include <Graphic3d_ArrayOfPolylines.hxx>
#include <Graphic3d_ArrayOfSegments.hxx>
#include <Graphic3d_AspectLine3d.hxx>
#include <Graphic3d_AspectMarker3d.hxx>
#include <Graphic3d_AspectText3d.hxx>
#include <Graphic3d_ClipPlane.hxx>
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
#include <atomic>
#include <thread>
#include <array>
#include <ranges>
#include <cmath>
#include <cstring>
#include <map>
#include <numbers>
#include <set>
#include <optional>
#include <tuple>
#include <utility>
#include <vector>

namespace nuf::render {

// Con nombre y no anónimo: GCC no admite tipos de un namespace anónimo dentro de
// un campo de Viewport::Impl declarado en una cabecera (-Wsubobject-linkage).
namespace drawing {

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

    // Texto centrado en p; con «boxed», sobre un recuadro del color «box».
    void addText(sketch::Vec2 p, const std::string& utf8, const Quantity_Color& color, const Quantity_Color& box,
                 double size = 13.0, bool boxed = true)
    {
        texts_.push_back({p, utf8, color, box, size, boxed});
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
                label.color, font.ToCString(), 1.0, 0.0, Aspect_TOST_NORMAL,
                label.boxed ? Aspect_TODT_SUBTITLE : Aspect_TODT_NORMAL);
            aspect->SetColorSubTitle(label.box);
            group->SetGroupPrimitivesAspect(aspect);
            Handle(Graphic3d_Text) text = new Graphic3d_Text(static_cast<float>(label.size));
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
        double size = 13.0;
        bool boxed = true;
    };

    gp_Pnt at(sketch::Vec2 p) const
    {
        return plane.Location().Translated(gp_Vec(plane.XDirection()) * p.x + gp_Vec(plane.YDirection()) * p.y);
    }

    std::vector<Curve> curves_;
    std::vector<Marker> points_;
    std::vector<Label> texts_;
};

} // namespace drawing

using namespace drawing;

} // namespace nuf::render
