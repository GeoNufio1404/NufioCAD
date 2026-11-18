#include <nuf/geometry/Measure.hpp>
#include <nuf/geometry/DisplayUnits.hpp>

#include <nuf/geometry/Occt.hpp>

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <sstream>

namespace nuf::geometry {

namespace {

enum class Kind { Plane, Cylinder, Cone, Sphere, OtherFace, Line, Circle, Arc, OtherEdge };

struct Info {
    Kind kind = Kind::OtherFace;
    bool hasDirection = false;  // normal del plano, eje o dirección de la recta
    gp_Dir direction{0.0, 0.0, 1.0};
    gp_Pnt point;               // punto del plano/eje/recta o centro del círculo
    bool hasPoint = false;
    double radius = 0.0;
    double size = 0.0;          // área (cara) o longitud (arista)
};

std::string number(double value, int decimals = 2)
{
    std::ostringstream out;
    out.setf(std::ios::fixed);
    out.precision(decimals);
    out << value;
    return out.str();
}

std::string point(const gp_Pnt& p)
{
    return "(" + display::number(p.X()) + "; " + display::number(p.Y()) + "; " + display::number(p.Z()) + ")";
}

Info inspect(const MeasureElement& element)
{
    Info info;
    if (element.isFace) {
        const TopoDS_Face& face = faceAt(element.shape, element.index);
        GProp_GProps props;
        BRepGProp::SurfaceProperties(face, props);
        info.size = props.Mass();
        const BRepAdaptor_Surface surface(face);
        switch (surface.GetType()) {
        case GeomAbs_Plane: {
            info.kind = Kind::Plane;
            gp_Dir normal = surface.Plane().Axis().Direction();
            if (face.Orientation() == TopAbs_REVERSED) {
                normal.Reverse();
            }
            info.direction = normal;
            info.hasDirection = true;
            info.point = surface.Plane().Location();
            info.hasPoint = true;
            break;
        }
        case GeomAbs_Cylinder: {
            info.kind = Kind::Cylinder;
            const gp_Cylinder cylinder = surface.Cylinder();
            info.direction = cylinder.Axis().Direction();
            info.hasDirection = true;
            info.point = cylinder.Location();
            info.hasPoint = true;
            info.radius = cylinder.Radius();
            break;
        }
        case GeomAbs_Cone:
            info.kind = Kind::Cone;
            break;
        case GeomAbs_Sphere:
            info.kind = Kind::Sphere;
            info.point = surface.Sphere().Location();
            info.hasPoint = true;
            info.radius = surface.Sphere().Radius();
            break;
        default:
            break;
        }
        return info;
    }

    const TopoDS_Edge& edge = edgeAt(element.shape, element.index);
    GProp_GProps props;
    BRepGProp::LinearProperties(edge, props);
    info.size = props.Mass();
    const BRepAdaptor_Curve curve(edge);
    switch (curve.GetType()) {
    case GeomAbs_Line:
        info.kind = Kind::Line;
        info.direction = curve.Line().Direction();
        info.hasDirection = true;
        info.point = curve.Line().Location();
        info.hasPoint = true;
        break;
    case GeomAbs_Circle: {
        const gp_Circ circle = curve.Circle();
        const bool full = std::abs((curve.LastParameter() - curve.FirstParameter()) - 2.0 * std::numbers::pi) < 1e-6;
        info.kind = full ? Kind::Circle : Kind::Arc;
        info.direction = circle.Axis().Direction();
        info.hasDirection = true;
        info.point = circle.Location();
        info.hasPoint = true;
        info.radius = circle.Radius();
        break;
    }
    default:
        info.kind = Kind::OtherEdge;
        break;
    }
    return info;
}

const char* kindName(Kind kind)
{
    switch (kind) {
    case Kind::Plane: return "Cara plana";
    case Kind::Cylinder: return "Cara cilíndrica";
    case Kind::Cone: return "Cara cónica";
    case Kind::Sphere: return "Cara esférica";
    case Kind::OtherFace: return "Cara curva";
    case Kind::Line: return "Arista recta";
    case Kind::Circle: return "Arista circular";
    case Kind::Arc: return "Arco";
    case Kind::OtherEdge: return "Arista curva";
    }
    return "";
}

bool isFace(Kind kind)
{
    return kind == Kind::Plane || kind == Kind::Cylinder || kind == Kind::Cone || kind == Kind::Sphere
        || kind == Kind::OtherFace;
}

void describeOne(const Info& info, Measurement& out)
{
    out.title = kindName(info.kind);
    if (isFace(info.kind)) {
        out.lines.push_back("Área: " + display::area(info.size));
    } else {
        out.lines.push_back("Longitud: " + display::length(info.size));
    }
    if (info.kind == Kind::Cylinder || info.kind == Kind::Sphere || info.kind == Kind::Circle || info.kind == Kind::Arc) {
        out.lines.push_back("Radio: " + display::length(info.radius));
        out.lines.push_back("Diámetro: " + display::length(2.0 * info.radius));
    }
    if (info.hasPoint && info.kind != Kind::Plane && info.kind != Kind::Line && info.kind != Kind::Cylinder) {
        out.lines.push_back("Centro: " + point(info.point));
    }
    if (info.kind == Kind::Plane && info.hasDirection) {
        out.lines.push_back("Normal: (" + number(info.direction.X()) + "; " + number(info.direction.Y()) + "; "
                            + number(info.direction.Z()) + ")");
    }
}

constexpr double kParallel = 1e-6;

void describePair(const Info& a, const Info& b, const gp_Pnt& pa, const gp_Pnt& pb, double distance, Measurement& out)
{
    out.title = std::string(kindName(a.kind)) + " → " + kindName(b.kind);
    out.lines.push_back("Distancia mínima: " + display::length(distance));
    if (distance < 1e-9) {
        out.lines.back() += " (se tocan)";
    }
    out.lines.push_back("Diferencia: dX " + display::number(std::abs(pb.X() - pa.X())) + ", dY " + display::number(std::abs(pb.Y() - pa.Y()))
                        + ", dZ " + display::length(std::abs(pb.Z() - pa.Z())));

    // Centros de círculos y ejes de cilindros: lo que se suele querer medir entre agujeros.
    const auto isRound = [](Kind kind) { return kind == Kind::Cylinder || kind == Kind::Circle || kind == Kind::Arc; };
    if (isRound(a.kind) && isRound(b.kind) && a.hasDirection && b.hasDirection) {
        const gp_Vec axis(a.direction);
        const gp_Vec between(a.point, b.point);
        const gp_Vec perpendicular = axis.Crossed(gp_Vec(b.direction));
        if (perpendicular.Magnitude() < kParallel) {
            // Ejes paralelos: distancia entre ellos (perpendicular al eje).
            const gp_Vec across = between - axis * between.Dot(axis);
            out.lines.push_back("Distancia entre ejes/centros: " + display::length(across.Magnitude()));
        }
    }

    // Ángulo y paralelismo.
    if (a.hasDirection && b.hasDirection) {
        const double cosine = std::clamp(std::abs(a.direction.Dot(b.direction)), 0.0, 1.0);
        const double angle = std::acos(cosine) * 180.0 / std::numbers::pi;  // 0° a 90°
        const bool planeA = a.kind == Kind::Plane;
        const bool planeB = b.kind == Kind::Plane;
        if (planeA && planeB) {
            const double signedCos = a.direction.Dot(b.direction);
            out.lines.push_back("Ángulo entre caras: " + number(std::acos(std::clamp(signedCos, -1.0, 1.0)) * 180.0 / std::numbers::pi)
                                + "° (agudo " + number(angle) + "°)");
            if (1.0 - cosine < kParallel) {
                const double gap = std::abs(gp_Vec(a.point, b.point).Dot(gp_Vec(a.direction)));
                out.lines.push_back("Paralelas, separadas " + display::length(gap));
            }
        } else if (planeA != planeB) {
            // Recta (o eje) contra plano: el ángulo con el plano es 90° menos el de la normal.
            out.lines.push_back("Ángulo con el plano: " + number(90.0 - angle) + "°");
        } else {
            out.lines.push_back("Ángulo entre direcciones: " + number(angle) + "°");
            if (1.0 - cosine < kParallel) {
                out.lines.back() += " (paralelas)";
            }
        }
    }
}

} // namespace

Measurement measure(const std::vector<MeasureElement>& elements)
{
    Measurement out;
    std::vector<const MeasureElement*> valid;
    for (const MeasureElement& e : elements) {
        if (e.shape.isNull() || e.index < 0 || e.index >= (e.isFace ? e.shape.faceCount() : e.shape.edgeCount())) {
            continue;
        }
        valid.push_back(&e);
    }
    if (valid.empty()) {
        return out;
    }
    if (valid.size() == 1) {
        describeOne(inspect(*valid[0]), out);
        return out;
    }
    const MeasureElement& a = *valid[valid.size() - 2];
    const MeasureElement& b = *valid[valid.size() - 1];
    const TopoDS_Shape sa = a.isFace ? TopoDS_Shape(faceAt(a.shape, a.index)) : TopoDS_Shape(edgeAt(a.shape, a.index));
    const TopoDS_Shape sb = b.isFace ? TopoDS_Shape(faceAt(b.shape, b.index)) : TopoDS_Shape(edgeAt(b.shape, b.index));
    const BRepExtrema_DistShapeShape extrema(sa, sb);
    if (!extrema.IsDone() || extrema.NbSolution() < 1) {
        out.title = "No se pudo medir";
        out.lines.push_back("No se encontró la distancia entre los dos elementos");
        return out;
    }
    const gp_Pnt pa = extrema.PointOnShape1(1);
    const gp_Pnt pb = extrema.PointOnShape2(1);
    out.hasSegment = true;
    out.from = toVec3(pa.XYZ());
    out.to = toVec3(pb.XYZ());
    out.distance = extrema.Value();
    describePair(inspect(a), inspect(b), pa, pb, out.distance, out);
    return out;
}

} // namespace nuf::geometry
