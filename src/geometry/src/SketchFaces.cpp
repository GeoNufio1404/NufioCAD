#include <nuf/geometry/Occt.hpp>

#include <nuf/sketch/Tools.hpp>

#include <BRepAdaptor_Curve.hxx>
#include <BOPAlgo_Builder.hxx>
#include <BOPAlgo_BuilderFace.hxx>
#include <BRepBndLib.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <Bnd_Box.hxx>
#include <Poly_Triangulation.hxx>
#include <gp_Pnt2d.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <NCollection_IndexedMap.hxx>
#include <NCollection_List.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <gp.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRep_Tool.hxx>
#include <GC_MakeArcOfCircle.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <GC_MakeArcOfEllipse.hxx>
#include <GeomConvert.hxx>
#include <GeomConvert_BSplineCurveToBezierCurve.hxx>
#include <Geom_BSplineCurve.hxx>
#include <Geom_BezierCurve.hxx>
#include <NCollection_Array1.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Ax2.hxx>
#include <gp_Circ.hxx>
#include <gp_Elips.hxx>
#include <gp_Pln.hxx>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace nuf::geometry {

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

// Arista OCCT de una curva del sketch (nullopt si es un punto o falla).
std::optional<TopoDS_Edge> sketchEdge(const sketch::Entity& e, const gp_Ax3& plane)
{
    const auto at = [&](sketch::Vec2 p) {
        return plane.Location().Translated(gp_Vec(plane.XDirection()) * p.x + gp_Vec(plane.YDirection()) * p.y);
    };
    const auto circleOf = [&](sketch::Vec2 center, double radius) {
        return gp_Circ(gp_Ax2(at(center), plane.Direction(), plane.XDirection()), radius);
    };
    const auto ellipseOf = [&](const sketch::Ellipse& el) {
        // Eje mayor girado «rotation» dentro del plano del sketch.
        const gp_Dir major(gp_Vec(plane.XDirection()) * std::cos(el.rotation)
                           + gp_Vec(plane.YDirection()) * std::sin(el.rotation));
        return gp_Elips(gp_Ax2(at(el.center), plane.Direction(), major), el.majorRadius, el.minorRadius);
    };
    if (const auto* l = std::get_if<sketch::Line>(&e.geometry)) {
        return BRepBuilderAPI_MakeEdge(at(l->start), at(l->end)).Edge();
    }
    if (const auto* a = std::get_if<sketch::Arc>(&e.geometry)) {
        GC_MakeArcOfCircle arc(circleOf(a->center, a->radius), a->startAngle, a->endAngle, true);
        return arc.IsDone() ? std::optional(BRepBuilderAPI_MakeEdge(arc.Value()).Edge()) : std::nullopt;
    }
    if (const auto* c = std::get_if<sketch::Circle>(&e.geometry)) {
        return BRepBuilderAPI_MakeEdge(circleOf(c->center, c->radius)).Edge();
    }
    if (const auto* el = std::get_if<sketch::Ellipse>(&e.geometry)) {
        return BRepBuilderAPI_MakeEdge(ellipseOf(*el)).Edge();
    }
    if (const auto* ea = std::get_if<sketch::EllipseArc>(&e.geometry)) {
        GC_MakeArcOfEllipse arc(ellipseOf(ea->ellipse), ea->startParam, ea->endParam, true);
        return arc.IsDone() ? std::optional(BRepBuilderAPI_MakeEdge(arc.Value()).Edge()) : std::nullopt;
    }
    if (const auto* b = std::get_if<sketch::BSpline>(&e.geometry)) {
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
        Handle(Geom_BSplineCurve) curve = new Geom_BSplineCurve(poles, weights, knots, mults, b->effectiveDegree());
        return BRepBuilderAPI_MakeEdge(curve).Edge();
    }
    return std::nullopt;
}

namespace {

// El tramo de la arista entre dos puntos suyos (en el sentido de su curva).
std::optional<TopoDS_Edge> trimmedEdge(const TopoDS_Edge& edge, const gp_Ax3& plane, const sketch::Trim& trim)
{
    const auto at = [&](sketch::Vec2 p) {
        return plane.Location().Translated(gp_Vec(plane.XDirection()) * p.x + gp_Vec(plane.YDirection()) * p.y);
    };
    double first = 0.0;
    double last = 0.0;
    Handle(Geom_Curve) curve = BRep_Tool::Curve(edge, first, last);
    if (curve.IsNull()) {
        return std::nullopt;
    }
    // Las curvas recortadas (arcos) se proyectan sobre su curva base, que
    // puede ser periódica; MakeEdge ajusta el segundo parámetro tras el primero.
    if (const auto trimmed = Handle(Geom_TrimmedCurve)::DownCast(curve); !trimmed.IsNull()) {
        curve = trimmed->BasisCurve();
    }
    BRepBuilderAPI_MakeEdge piece(curve, at(trim.start), at(trim.end));
    if (!piece.IsDone()) {
        return std::nullopt;
    }
    return piece.Edge();
}

} // namespace

std::optional<TopoDS_Face> profileFace(const sketch::Sketch& sketch, const sketch::Profile& profile,
                                       const gp_Ax3& plane, std::vector<EdgeOrigin>* origins)
{
    const auto wireOf = [&](const sketch::Loop& loop) -> std::optional<TopoDS_Wire> {
        BRepBuilderAPI_MakeWire wire;
        for (const sketch::ProfileEdge& pe : loop.edges) {
            const sketch::Entity* e = sketch.entity(pe.entity);
            auto edge = e ? sketchEdge(*e, plane) : std::nullopt;
            if (!edge) {
                return std::nullopt;
            }
            if (pe.trim) {
                // Solo el tramo entre los cruces, en el sentido de la curva.
                edge = trimmedEdge(*edge, plane, *pe.trim);
                if (!edge) {
                    return std::nullopt;
                }
            }
            if (pe.reversed) {
                edge->Reverse();
            }
            wire.Add(*edge);
            if (!wire.IsDone()) {
                return std::nullopt;
            }
            if (origins) {
                // La arista que queda en el contorno (MakeWire puede rehacerla
                // para compartir vértices).
                origins->push_back({wire.Edge(), pe.entity});
            }
        }
        return wire.Wire();
    };

    const auto outer = wireOf(profile.outer);
    if (!outer) {
        return std::nullopt;
    }
    BRepBuilderAPI_MakeFace face(gp_Pln(plane), *outer, true);
    for (const sketch::Loop& hole : profile.holes) {
        if (const auto inner = wireOf(hole)) {
            face.Add(TopoDS::Wire(inner->Reversed()));
        }
    }
    if (!face.IsDone()) {
        return std::nullopt;
    }
    return face.Face();
}

std::vector<TopoDS_Face> profileFaces(const sketch::Sketch& sketch, const gp_Ax3& plane)
{
    std::vector<TopoDS_Face> faces;
    for (const sketch::Profile& profile : sketch::findProfiles(sketch)) {
        if (auto face = profileFace(sketch, profile, plane)) {
            faces.push_back(*face);
        }
    }
    return faces;
}

namespace {

// Contorno muestreado en orden (con el sentido de cada arista en el contorno).
std::vector<gp_Pnt2d> sampledWire(const TopoDS_Wire& wire)
{
    std::vector<gp_Pnt2d> points;
    for (BRepTools_WireExplorer ex(wire); ex.More(); ex.Next()) {
        const BRepAdaptor_Curve curve(ex.Current());
        const bool reversed = ex.Current().Orientation() == TopAbs_REVERSED;
        constexpr int kSamples = 24;
        for (int i = 0; i < kSamples; ++i) {
            const double t = double(i) / kSamples;
            const double u = reversed ? curve.LastParameter() - t * (curve.LastParameter() - curve.FirstParameter())
                                      : curve.FirstParameter() + t * (curve.LastParameter() - curve.FirstParameter());
            const gp_Pnt p = curve.Value(u);
            points.emplace_back(p.X(), p.Y());
        }
    }
    return points;
}

// Número de vueltas de los contornos alrededor de p (regla de relleno de
// las fuentes: distinto de cero = dentro).
int windingNumber(const std::vector<std::vector<gp_Pnt2d>>& contours, const gp_Pnt2d& p)
{
    int winding = 0;
    for (const auto& c : contours) {
        for (std::size_t i = 0; i < c.size(); ++i) {
            const gp_Pnt2d& a = c[i];
            const gp_Pnt2d& b = c[(i + 1) % c.size()];
            const double side = (b.X() - a.X()) * (p.Y() - a.Y()) - (p.X() - a.X()) * (b.Y() - a.Y());
            if (a.Y() <= p.Y()) {
                if (b.Y() > p.Y() && side > 0.0) {
                    ++winding;
                }
            } else if (b.Y() <= p.Y() && side < 0.0) {
                --winding;
            }
        }
    }
    return winding;
}

// Un punto seguro dentro de la cara (centro de un triángulo de su malla).
std::optional<gp_Pnt2d> pointInside(const TopoDS_Face& face)
{
    BRepMesh_IncrementalMesh mesh(face, 0.01, false, 0.5);
    TopLoc_Location location;
    const Handle(Poly_Triangulation) triangles = BRep_Tool::Triangulation(face, location);
    if (triangles.IsNull() || triangles->NbTriangles() == 0) {
        return std::nullopt;
    }
    // El triángulo más grande: su centro queda lejos de los bordes.
    double best = -1.0;
    gp_Pnt2d center;
    for (int t = 1; t <= triangles->NbTriangles(); ++t) {
        int i1 = 0, i2 = 0, i3 = 0;
        triangles->Triangle(t).Get(i1, i2, i3);
        const gp_Pnt a = triangles->Node(i1).Transformed(location.Transformation());
        const gp_Pnt b = triangles->Node(i2).Transformed(location.Transformation());
        const gp_Pnt c = triangles->Node(i3).Transformed(location.Transformation());
        const double area = std::abs((b.X() - a.X()) * (c.Y() - a.Y()) - (c.X() - a.X()) * (b.Y() - a.Y()));
        if (area > best) {
            best = area;
            center = gp_Pnt2d((a.X() + b.X() + c.X()) / 3.0, (a.Y() + b.Y() + c.Y()) / 3.0);
        }
    }
    return center;
}

} // namespace

std::vector<sketch::Geometry> outlineOf(const TopoDS_Shape& planarShape)
{
    // Contornos originales, con su sentido, para el número de vueltas.
    std::vector<std::vector<gp_Pnt2d>> contours;
    NCollection_List<TopoDS_Shape> edges;
    for (TopExp_Explorer ex(planarShape, TopAbs_WIRE); ex.More(); ex.Next()) {
        contours.push_back(sampledWire(TopoDS::Wire(ex.Current())));
    }
    NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher> unique;
    TopExp::MapShapes(planarShape, TopAbs_EDGE, unique);
    for (int i = 1; i <= unique.Extent(); ++i) {
        edges.Append(unique(i));
    }
    if (edges.IsEmpty()) {
        return {};
    }

    // Las curvas partidas en todos sus cruces (también los de un contorno
    // consigo mismo) y las regiones que forman.
    BOPAlgo_Builder split;
    split.SetArguments(edges);
    split.Perform();
    if (split.HasErrors()) {
        return {};
    }
    Bnd_Box box;
    BRepBndLib::Add(planarShape, box);
    double x0 = 0, y0 = 0, z0 = 0, x1 = 0, y1 = 0, z1 = 0;
    box.Get(x0, y0, z0, x1, y1, z1);
    const double margin = 1.0 + 0.1 * std::max(x1 - x0, y1 - y0);
    const TopoDS_Face base =
        BRepBuilderAPI_MakeFace(gp_Pln(gp::XOY()), x0 - margin, x1 + margin, y0 - margin, y1 + margin).Face();
    NCollection_List<TopoDS_Shape> pieces;
    for (TopExp_Explorer ex(split.Shape(), TopAbs_EDGE); ex.More(); ex.Next()) {
        pieces.Append(ex.Current().Oriented(TopAbs_FORWARD));
        pieces.Append(ex.Current().Oriented(TopAbs_REVERSED));
    }
    BOPAlgo_BuilderFace regions;
    regions.SetFace(base);
    regions.SetShapes(pieces);
    regions.Perform();
    if (regions.HasErrors()) {
        return {};
    }

    // Las regiones «pintadas», unidas.
    NCollection_List<TopoDS_Shape> ink;
    for (const TopoDS_Shape& area : regions.Areas()) {
        const TopoDS_Face face = TopoDS::Face(area);
        const auto inside = pointInside(face);
        if (inside && windingNumber(contours, *inside) != 0) {
            ink.Append(face);
        }
    }
    if (ink.IsEmpty()) {
        return {};
    }
    TopoDS_Shape result = ink.First();
    if (ink.Extent() > 1) {
        NCollection_List<TopoDS_Shape> first;
        first.Append(ink.First());
        ink.RemoveFirst();
        BRepAlgoAPI_Fuse fuse;
        fuse.SetArguments(first);
        fuse.SetTools(ink);
        fuse.Build();
        if (!fuse.IsDone()) {
            return {};
        }
        result = fuse.Shape();
    }
    ShapeUpgrade_UnifySameDomain unify(result, true, true, false);
    unify.Build();

    std::vector<sketch::Geometry> geometry;
    NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher> outline;
    TopExp::MapShapes(unify.Shape(), TopAbs_EDGE, outline);
    for (int i = 1; i <= outline.Extent(); ++i) {
        for (const sketch::Geometry& g : projectEdge(TopoDS::Edge(outline(i)), sketch::Plane::xy())) {
            geometry.push_back(g);
        }
    }
    return geometry;
}

} // namespace nuf::geometry
