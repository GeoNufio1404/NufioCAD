#include <nuf/geometry/Occt.hpp>

#include <nuf/sketch/Tools.hpp>

#include <BRepAdaptor_Curve.hxx>
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

} // namespace nuf::geometry
