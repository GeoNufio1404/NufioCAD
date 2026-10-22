#include <nuf/render/Viewport.hpp>

// En Windows, antes que OCCT: OCCT 8 incluye <windows.h> con su propia
// configuración y después ya no estarían wglGetCurrentDC ni WindowFromDC.
#ifdef _WIN32
#include <windows.h>
#endif

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
#include <TopTools_IndexedMapOfShape.hxx>
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
#include <BRepPrimAPI_MakeBox.hxx>
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

gp_Pnt toPnt(const sketch::Vec3& v) { return gp_Pnt(v.x, v.y, v.z); }
gp_Dir toDir(const sketch::Vec3& v) { return gp_Dir(v.x, v.y, v.z); }
sketch::Vec3 toVec3(const gp_XYZ& v) { return {v.X(), v.Y(), v.Z()}; }

gp_Ax3 toAx3(const sketch::Plane& plane)
{
    return gp_Ax3(toPnt(plane.origin), toDir(plane.normal), toDir(plane.xAxis));
}

// Arista 3D proyectada sobre el plano del sketch, como geometría del sketch:
// líneas y circunferencias paralelas al plano, exactas; el resto, como una
// spline por puntos de la proyección. Vacío si la proyección es un punto.
std::vector<sketch::Geometry> projectEdge(const TopoDS_Edge& edge, const sketch::Plane& plane)
{
    const BRepAdaptor_Curve curve(edge);
    const double first = curve.FirstParameter();
    const double last = curve.LastParameter();
    const auto local = [&](const gp_Pnt& p) { return plane.toLocal(toVec3(p.XYZ())); };
    const sketch::Vec2 a = local(curve.Value(first));
    const sketch::Vec2 b = local(curve.Value(last));
    const bool closed = std::hypot(b.x - a.x, b.y - a.y) < 1e-7;
    const gp_Dir normal = toDir(plane.normal);

    switch (curve.GetType()) {
    case GeomAbs_Line:
        if (std::hypot(b.x - a.x, b.y - a.y) < 1e-7) {
            return {};  // perpendicular al plano: se proyecta en un punto
        }
        return {sketch::Line{a, b}};
    case GeomAbs_Circle: {
        const gp_Circ c = curve.Circle();
        const double facing = c.Axis().Direction().Dot(normal);
        if (std::abs(facing) < 1.0 - 1e-9) {
            break;  // inclinada: su proyección es una elipse (spline abajo)
        }
        const sketch::Vec2 center = local(c.Location());
        if (closed) {
            return {sketch::Circle{center, c.Radius()}};
        }
        const double angleA = std::atan2(a.y - center.y, a.x - center.x);
        const double angleB = std::atan2(b.y - center.y, b.x - center.x);
        // Sentido del arco visto desde el lado de la normal del sketch.
        const double from = facing > 0.0 ? angleA : angleB;
        double to = facing > 0.0 ? angleB : angleA;
        while (to <= from) {
            to += 2.0 * std::numbers::pi;
        }
        return {sketch::Arc{center, c.Radius(), from, to}};
    }
    case GeomAbs_BezierCurve:
    case GeomAbs_BSplineCurve: {
        // Una proyección paralela conserva la B-spline: se proyectan los polos.
        // Se trocea en segmentos Bézier (nudos uniformes, como los del sketch).
        Handle(Geom_BSplineCurve) spline = curve.GetType() == GeomAbs_BSplineCurve
                                               ? Handle(Geom_BSplineCurve)::DownCast(curve.BSpline()->Copy())
                                               : GeomConvert::CurveToBSplineCurve(curve.Bezier());
        if (spline.IsNull()) {
            break;
        }
        // (BRepAdaptor_Curve ya la devuelve con la posición de la arista aplicada.)
        spline->Segment(first, last);
        GeomConvert_BSplineCurveToBezierCurve pieces(spline);
        std::vector<sketch::Geometry> result;
        for (int i = 1; i <= pieces.NbArcs(); ++i) {
            const Handle(Geom_BezierCurve) bezier = pieces.Arc(i);
            sketch::BSpline segment;
            segment.degree = bezier->Degree();
            for (int k = 1; k <= bezier->NbPoles(); ++k) {
                segment.poles.push_back(local(bezier->Pole(k)));
                segment.weights.push_back(bezier->Weight(k));
            }
            const sketch::Vec2 s = segment.poles.front();
            const sketch::Vec2 e = segment.poles.back();
            if (std::hypot(e.x - s.x, e.y - s.y) > 1e-9) {
                result.push_back(segment);
            }
        }
        if (edge.Orientation() == TopAbs_REVERSED) {
            std::ranges::reverse(result);
        }
        return result;
    }
    default:
        break;
    }
    // Cualquier otra curva: spline por puntos de su proyección.
    constexpr int kSamples = 24;
    std::vector<sketch::Vec2> points;
    for (int i = 0; i <= kSamples; ++i) {
        const sketch::Vec2 p = local(curve.Value(first + (last - first) * i / kSamples));
        if (points.empty() || std::hypot(p.x - points.back().x, p.y - points.back().y) > 1e-7) {
            points.push_back(p);
        }
    }
    if (points.size() < 2) {
        return {};
    }
    return {sketch::interpolatingSpline(points)};
}

// Caras planas de los perfiles cerrados del sketch (para sombrearlos). En la
// Fase 3 esto pasa a geometry/, que lo necesitará para extruir.
TopoDS_Shape profileFaces(const sketch::Sketch& sketch, const gp_Ax3& plane)
{
    const auto at = [&](sketch::Vec2 p) {
        return plane.Location().Translated(gp_Vec(plane.XDirection()) * p.x + gp_Vec(plane.YDirection()) * p.y);
    };
    const auto circleOf = [&](sketch::Vec2 center, double radius) {
        return gp_Circ(gp_Ax2(at(center), plane.Direction(), plane.XDirection()), radius);
    };
    const auto ellipseOf = [&](const sketch::Ellipse& e) {
        // Eje mayor girado «rotation» dentro del plano del sketch.
        const gp_Dir major(gp_Vec(plane.XDirection()) * std::cos(e.rotation)
                           + gp_Vec(plane.YDirection()) * std::sin(e.rotation));
        return gp_Elips(gp_Ax2(at(e.center), plane.Direction(), major), e.majorRadius, e.minorRadius);
    };
    const auto wireOf = [&](const sketch::Loop& loop) -> std::optional<TopoDS_Wire> {
        BRepBuilderAPI_MakeWire wire;
        for (const sketch::ProfileEdge& pe : loop.edges) {
            const sketch::Entity* e = sketch.entity(pe.entity);
            if (!e) {
                return std::nullopt;
            }
            TopoDS_Edge edge;
            if (const auto* l = std::get_if<sketch::Line>(&e->geometry)) {
                edge = BRepBuilderAPI_MakeEdge(at(l->start), at(l->end));
            } else if (const auto* a = std::get_if<sketch::Arc>(&e->geometry)) {
                GC_MakeArcOfCircle arc(circleOf(a->center, a->radius), a->startAngle, a->endAngle, true);
                if (!arc.IsDone()) {
                    return std::nullopt;
                }
                edge = BRepBuilderAPI_MakeEdge(arc.Value());
            } else if (const auto* c = std::get_if<sketch::Circle>(&e->geometry)) {
                edge = BRepBuilderAPI_MakeEdge(circleOf(c->center, c->radius));
            } else if (const auto* el = std::get_if<sketch::Ellipse>(&e->geometry)) {
                edge = BRepBuilderAPI_MakeEdge(ellipseOf(*el));
            } else if (const auto* ea = std::get_if<sketch::EllipseArc>(&e->geometry)) {
                GC_MakeArcOfEllipse arc(ellipseOf(ea->ellipse), ea->startParam, ea->endParam, true);
                if (!arc.IsDone()) {
                    return std::nullopt;
                }
                edge = BRepBuilderAPI_MakeEdge(arc.Value());
            } else if (const auto* b = std::get_if<sketch::BSpline>(&e->geometry)) {
                const int n = int(b->poles.size());
                NCollection_Array1<gp_Pnt> poles(1, n);
                NCollection_Array1<double> weights(1, n);
                for (int i = 0; i < n; ++i) {
                    poles.SetValue(i + 1, at(b->poles[std::size_t(i)]));
                    weights.SetValue(i + 1, b->weights.empty() ? 1.0 : b->weights[std::size_t(i)]);
                }
                const auto knotValues = b->knots();
                const auto multiplicities = b->multiplicities();
                NCollection_Array1<double> knots(1, int(knotValues.size()));
                NCollection_Array1<int> mults(1, int(knotValues.size()));
                for (std::size_t i = 0; i < knotValues.size(); ++i) {
                    knots.SetValue(int(i) + 1, knotValues[i]);
                    mults.SetValue(int(i) + 1, multiplicities[i]);
                }
                Handle(Geom_BSplineCurve) curve =
                    new Geom_BSplineCurve(poles, weights, knots, mults, b->effectiveDegree());
                edge = BRepBuilderAPI_MakeEdge(curve);
            }
            if (pe.reversed) {
                edge.Reverse();
            }
            wire.Add(edge);
            if (!wire.IsDone()) {
                return std::nullopt;
            }
        }
        return wire.Wire();
    };

    BRep_Builder builder;
    TopoDS_Compound compound;
    builder.MakeCompound(compound);
    for (const sketch::Profile& profile : sketch::findProfiles(sketch)) {
        const auto outer = wireOf(profile.outer);
        if (!outer) {
            continue;
        }
        BRepBuilderAPI_MakeFace face(gp_Pln(plane), *outer, true);
        for (const sketch::Loop& hole : profile.holes) {
            if (const auto inner = wireOf(hole)) {
                face.Add(TopoDS::Wire(inner->Reversed()));
            }
        }
        if (face.IsDone()) {
            builder.Add(compound, face.Face());
        }
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
        drawing->font = pickLabelFont();
        preview = new SketchDrawing();
        preview->SetZLayer(Graphic3d_ZLayerId_Topmost);
        profiles = new AIS_Shape(TopoDS_Shape());
        profiles->SetZLayer(Graphic3d_ZLayerId_Topmost);
        profiles->SetColor(rgb(0x6C, 0xA6, 0xFF));
        profiles->SetTransparency(0.82);
        profiles->Attributes()->SetFaceBoundaryDraw(false);
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
        context->Erase(profiles, false);
        shownSketch.reset();
        projectMode = false;
        for (const Handle(AIS_InteractiveObject)& image : images) {
            context->Remove(image, false);
        }
        images.clear();
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
        profiles->SetShape(profileFaces(sketch, toAx3(sketchPlane)));
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
        if (on) {
            context->Activate(part, AIS_Shape::SelectionMode(TopAbs_EDGE));
        } else {
            context->Deactivate(part);
        }
        context->ClearDetected(false);
    }

    // Arista de la pieza bajo el píxel, proyectada.
    std::vector<sketch::Geometry> projectionAt(const Vec2i& pixel)
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
                return projected;
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
        TopTools_IndexedMapOfShape edges;
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
        BRepAlgoAPI_Section cut(part->Shape(), gp_Pln(toAx3(sketchPlane)));
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

    void showFinishedSketch(const sketch::Sketch& sketch, const sketch::Plane& plane)
    {
        Handle(SketchDrawing) curves = new SketchDrawing();
        curves->plane = toAx3(plane);
        // Las curvas por encima de la pieza: un sketch suele quedar dentro o
        // debajo de ella (por ejemplo, en el plano XY bajo la base).
        curves->SetZLayer(Graphic3d_ZLayerId_Topmost);
        const SketchStroke stroke{rgb(0x8F, 0xB3, 0xE8), 1.5};
        for (const sketch::Entity& e : sketch.entities()) {
            if (!e.reference && !e.construction) {
                curves->addGeometry(e.geometry, stroke);
            }
        }
        Handle(AIS_Shape) regions = new AIS_Shape(profileFaces(sketch, toAx3(plane)));
        regions->SetColor(rgb(0x6C, 0xA6, 0xFF));
        regions->SetTransparency(0.8);
        regions->Attributes()->SetFaceBoundaryDraw(false);
        context->Display(regions, AIS_Shaded, -1, false);
        context->Display(curves, 0, -1, false);
        finishedSketches.push_back(curves);
        finishedSketches.push_back(regions);
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
    Handle(AIS_Shape) profiles;
    std::vector<Handle(AIS_InteractiveObject)> finishedSketches;
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

void Viewport::showFinishedSketch(const sketch::Sketch& sketch, const sketch::Plane& plane)
{
    impl_->showFinishedSketch(sketch, plane);
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
    if ((impl_->sketchToolActive || impl_->sketchDragging) && isSketching()) {
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
    if (event->button() == Qt::LeftButton && impl_->sketchDragging) {
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
            const auto projected = impl_->projectionAt(point);
            if (!projected.empty()) {
                emit sketchEdgeProjected(projected);
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
