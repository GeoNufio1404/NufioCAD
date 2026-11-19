#include "Naming.hpp"

#include <nuf/geometry/Cancel.hpp>
#include <nuf/geometry/Modeling.hpp>
#include <nuf/geometry/Occt.hpp>

#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepBndLib.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeSolid.hxx>
#include <BRepBuilderAPI_Sewing.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <Geom_BSplineSurface.hxx>
#include <NCollection_Array1.hxx>
#include <NCollection_Array2.hxx>
#include <TopoDS_Shell.hxx>
#include <GeomLib.hxx>
#include <Geom_BoundedSurface.hxx>
#include <Geom_RectangularTrimmedSurface.hxx>
#include <Geom_Surface.hxx>
#include <Geom_SurfaceOfLinearExtrusion.hxx>
#include <Precision.hxx>
#include <gp_Cone.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepAlgoAPI_Splitter.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <GCPnts_UniformAbscissa.hxx>
#include <BRepAdaptor_CompCurve.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepOffsetAPI_MakePipeShell.hxx>
#include <BRepOffsetAPI_ThruSections.hxx>
#include <BRepTools.hxx>
#include <GeomAPI_Interpolate.hxx>
#include <Geom_BSplineCurve.hxx>
#include <Message_ProgressIndicator.hxx>
#include <Message_ProgressRange.hxx>
#include <NCollection_HArray1.hxx>
#include <gp_Circ.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepFilletAPI_MakeChamfer.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <gp_Pnt2d.hxx>
#include <BRepGProp.hxx>
#include <BRepOffsetAPI_DraftAngle.hxx>
#include <BRepOffsetAPI_MakeOffset.hxx>
#include <BRepOffsetAPI_MakeThickSolid.hxx>
#include <NCollection_List.hxx>
#include <GProp_GProps.hxx>
#include <BRepLib.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCone.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <TopoDS_Vertex.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakeHalfSpace.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <BRepPrimAPI_MakeTorus.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepPrimAPI_MakeRevol.hxx>
#include <BRep_Builder.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <Standard_Failure.hxx>
#include <Standard_Version.hxx>
#include <Law_Linear.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <NCollection_IndexedDataMap.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <TopoDS_Iterator.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Solid.hxx>
#include <gp_Ax1.hxx>
#include <gp_Ax2.hxx>
#include <gp_Cylinder.hxx>
#include <gp_Sphere.hxx>
#include <gp_Torus.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>
#include <gp_Pln.hxx>

#include <algorithm>
#include <cstdio>
#include <functional>
#include <cmath>
#include <numbers>
#include <memory>
#include <string>

namespace nuf::geometry {

namespace {

constexpr double kTiny = 1e-9;

// OCCT 8 hereda de std::exception (what()); OCCT 7 solo tiene GetMessageString().
std::string kernelMessage(const Standard_Failure& e)
{
#if OCC_VERSION_HEX >= 0x080000
    return e.what();
#else
    return e.GetMessageString();
#endif
}

// Un fallo del kernel en lenguaje claro: qué se intentaba, qué probar y, al final, el detalle técnico.
// Qué quiere decir en español el error del kernel (U8): el nombre de la excepción de OpenCASCADE y su causa más probable.
std::string explainKernelError(const std::string& detail)
{
    struct Cause {
        const char* marker;
        const char* text;
    };
    static const Cause causes[] = {
        {"ConstructionError", "se intentó construir una forma con medidas imposibles (un radio, un ancho o una distancia que no caben)"},
        {"DomainError", "un valor está fuera de lo que admite la operación (un ángulo, una distancia o una cantidad)"},
        {"OutOfRange", "se pidió un elemento que no existe (una cara o una arista que ya no está)"},
        {"NullObject", "falta una forma de entrada (un cuerpo vacío o una cara que desapareció)"},
        {"NoSuchObject", "no se encontró una de las caras o aristas elegidas"},
        {"TypeMismatch", "una de las entradas no es del tipo que pide la operación (por ejemplo, una cara curva donde hace falta una plana)"},
        {"NotDone", "el kernel no pudo terminar el cálculo (la geometría es muy ajustada, tangente o se solapa consigo misma)"},
        {"command not done", "el kernel no pudo terminar el cálculo (la geometría es muy ajustada, tangente o se solapa consigo misma)"},
        {"Overflow", "una medida es tan grande que el cálculo se desborda"},
        {"DimensionError", "las formas que se combinan no son compatibles (por ejemplo, un perfil abierto)"}};
    for (const Cause& cause : causes) {
        if (detail.find(cause.marker) != std::string::npos) {
            return cause.text;
        }
    }
    return {};
}

GeometryError kernelFailure(const std::string& action, const std::string& hint, const Standard_Failure& e)
{
    const std::string detail = kernelMessage(e);
    const std::string cause = explainKernelError(detail);
    std::string message = "No se pudo " + action + ": " + hint;
    if (!cause.empty()) {
        message += ". Causa probable: " + cause;
    }
    if (!detail.empty()) {
        message += " (detalle técnico: " + detail + ")";
    }
    return GeometryError(message);
}

// Deja que el kernel se detenga a media operación cuando el hilo de trabajo pide cancelar (cerrar la ventana, un
// cambio nuevo en el historial). Lo usan las booleanas y todas las construcciones que tardan: loft, redondeo, chaflán,
// vaciado, barrido y la unión de partes. Las que no admiten progreso (UnifySameDomain) se comprueban antes y después.
class ProgressCanceller final : public Message_ProgressIndicator {
protected:
    bool UserBreak() override { return cancelRequested(); }
    void Show(const Message_ProgressScope&, const bool) override {}
};

void throwIfCancelled()
{
    if (cancelRequested()) {
        throw GeometryError("Cálculo cancelado");
    }
}

// «builder.Build()» interrumpible: si se pidió cancelar, lanza «Cálculo cancelado».
template <typename Builder>
void buildCancellable(Builder& builder)
{
    occ::handle<ProgressCanceller> canceller = new ProgressCanceller();
    builder.Build(canceller->Start());
    throwIfCancelled();
}

// Une las caras coplanares (y aristas colineales) que dejan las booleanas.
Shape unify(const Shape& input, const std::string& tag)
{
    throwIfCancelled();
    ShapeUpgrade_UnifySameDomain unifier(occtShape(input), true, true, false);
    unifier.Build();
    throwIfCancelled();
    Namer namer(unifier.Shape());
    namer.inherit(input, *unifier.History());
    return namer.finish(tag);
}

// Fusión de varias formas con nombre (lados de una extrusión, perfiles…).
Shape fuseAll(const std::vector<Shape>& parts, const std::string& tag)
{
    if (parts.size() == 1) {
        return parts.front();
    }
    NCollection_List<TopoDS_Shape> arguments;
    NCollection_List<TopoDS_Shape> tools;
    arguments.Append(occtShape(parts.front()));
    for (std::size_t i = 1; i < parts.size(); ++i) {
        tools.Append(occtShape(parts[i]));
    }
    BRepAlgoAPI_Fuse fuse;
    fuse.SetArguments(arguments);
    fuse.SetTools(tools);
    fuse.SetRunParallel(true);
    buildCancellable(fuse);
    if (!fuse.IsDone() || fuse.HasErrors()) {
        throw GeometryError("No se pudieron unir las partes");
    }
    Namer namer(fuse.Shape());
    for (const Shape& part : parts) {
        namer.inherit(part, *fuse.History());
    }
    return unify(namer.finish(tag), tag);
}

// Cara a extruir, con el nombre de lo que origina cada arista («e5» = curva
// 5 del sketch, o el nombre de la arista del cuerpo).
struct ProfileFace {
    TopoDS_Face face;
    std::vector<std::pair<TopoDS_Edge, std::string>> sources;
    std::string key;  // perfil: «e<k>», su menor curva del contorno exterior
};

std::vector<ProfileFace> regionFaces(const SketchRegion& region)
{
    if (region.sketch == nullptr || region.profiles.empty()) {
        throw GeometryError("No hay perfiles que usar");
    }
    const gp_Ax3 plane = toAx3(region.plane);
    std::vector<ProfileFace> faces;
    for (const sketch::Profile& profile : region.profiles) {
        ProfileFace pf;
        std::vector<EdgeOrigin> origins;
        auto face = profileFace(*region.sketch, profile, plane, &origins);
        if (!face) {
            throw GeometryError("Un perfil no forma una cara válida");
        }
        pf.face = *face;
        for (const EdgeOrigin& o : origins) {
            pf.sources.emplace_back(o.edge, "e" + std::to_string(o.entity));
        }
        sketch::EntityId lowest = 0;
        for (const sketch::ProfileEdge& e : profile.outer.edges) {
            lowest = lowest == 0 ? e.entity : std::min(lowest, e.entity);
        }
        pf.key = "e" + std::to_string(lowest);
        faces.push_back(std::move(pf));
    }
    return faces;
}

// Cara plana con el nombre de cada arista (para empujar/tirar una cara).
ProfileFace bodyFace(const Shape& body, int faceIndex)
{
    if (faceIndex < 0 || faceIndex >= body.faceCount() || !body.facePlane(faceIndex)) {
        throw GeometryError("Solo se pueden extruir caras planas");
    }
    ProfileFace pf;
    pf.face = faceAt(body, faceIndex);
    pf.key = body.faceName(faceIndex);
    for (TopExp_Explorer ex(pf.face, TopAbs_EDGE); ex.More(); ex.Next()) {
        if (const auto e = edgeIndexOf(body, ex.Current())) {
            pf.sources.emplace_back(TopoDS::Edge(ex.Current()), body.edgeName(*e));
        }
    }
    return pf;
}

// ¿Se cierra la cara al encogerla «inset» hacia dentro? (un ángulo que
// estrecha tanto haría pasar las laterales por su vértice).
bool collapses(const TopoDS_Face& face, double inset)
{
    if (inset <= kTiny) {
        return false;
    }
    try {
        BRepOffsetAPI_MakeOffset offset(face, GeomAbs_Arc);
        offset.Perform(-inset);
        if (!offset.IsDone() || offset.Shape().IsNull()) {
            return true;
        }
        GProp_GProps props;
        BRepGProp::LinearProperties(offset.Shape(), props);
        return props.Mass() < 1e-6;
    } catch (const Standard_Failure&) {
        return true;
    }
}

// Longitud que seguro atraviesa todo el contexto desde el plano.
double throughLength(const std::vector<Shape>& context, const TopoDS_Shape& start)
{
    Box all;
    const auto grow = [&](const Box& b) {
        if (b.empty) {
            return;
        }
        if (all.empty) {
            all = b;
            return;
        }
        all.min = {std::min(all.min.x, b.min.x), std::min(all.min.y, b.min.y), std::min(all.min.z, b.min.z)};
        all.max = {std::max(all.max.x, b.max.x), std::max(all.max.y, b.max.y), std::max(all.max.z, b.max.z)};
    };
    for (const Shape& s : context) {
        grow(s.bounds());
    }
    Bnd_Box startBox;
    BRepBndLib::Add(start, startBox);
    if (!startBox.IsVoid()) {
        Box b;
        startBox.Get(b.min.x, b.min.y, b.min.z, b.max.x, b.max.y, b.max.z);
        b.empty = false;
        grow(b);
    }
    if (all.empty) {
        return 1000.0;
    }
    const double dx = all.max.x - all.min.x;
    const double dy = all.max.y - all.min.y;
    const double dz = all.max.z - all.min.z;
    return 2.0 * std::sqrt(dx * dx + dy * dy + dz * dz) + 10.0;
}

// Cara de «context» señalada por la referencia (de cualquier tipo de superficie).
TopoDS_Face targetFace(const std::vector<Shape>& context, const ElementRef& ref)
{
    for (const Shape& s : context) {
        if (const auto index = s.findFace(ref)) {
            return faceAt(s, *index);
        }
    }
    throw GeometryError("No se encuentra la cara de destino");
}

// ¿Está «face» sobre la misma superficie que la de destino (mismo tipo, radio y eje o centro)?
bool onSurface(const TopoDS_Face& face, const BRepAdaptor_Surface& target)
{
    constexpr double tol = 1e-6;
    const BRepAdaptor_Surface surface(face);
    if (surface.GetType() != target.GetType()) {
        return false;
    }
    switch (target.GetType()) {
    case GeomAbs_Cylinder:
        return std::abs(surface.Cylinder().Radius() - target.Cylinder().Radius()) < tol
               && surface.Cylinder().Axis().IsCoaxial(target.Cylinder().Axis(), tol, tol);
    case GeomAbs_Sphere:
        return std::abs(surface.Sphere().Radius() - target.Sphere().Radius()) < tol
               && surface.Sphere().Location().Distance(target.Sphere().Location()) < tol;
    case GeomAbs_Torus:
        return std::abs(surface.Torus().MajorRadius() - target.Torus().MajorRadius()) < tol
               && std::abs(surface.Torus().MinorRadius() - target.Torus().MinorRadius()) < tol
               && surface.Torus().Axis().IsCoaxial(target.Torus().Axis(), tol, tol);
    case GeomAbs_Cone:
        return std::abs(surface.Cone().SemiAngle() - target.Cone().SemiAngle()) < tol
               && surface.Cone().Apex().Distance(target.Cone().Apex()) < 1e-5
               && surface.Cone().Axis().IsCoaxial(target.Cone().Axis(), tol, tol);
    default:
        return false;
    }
}

// ¿Cubre «cap» (un trozo de la superficie de destino) algo de la cara real «target»? La superficie es
// la misma, pero la cara puede ser solo un trozo de ella. Si el kernel no puede decidirlo se da por
// buena (como si la cara fuera la superficie entera).
bool overlapsFace(const TopoDS_Face& cap, const TopoDS_Face& target)
{
    try {
        BRepAlgoAPI_Common common(cap, target);
        if (!common.IsDone() || common.HasErrors()) {
            return true;
        }
        GProp_GProps props;
        BRepGProp::SurfaceProperties(common.Shape(), props);
        return props.Mass() > 1e-6;
    } catch (const Standard_Failure&) {
        return true;
    }
}

// Hasta una cara plana: recorta el prisma largo «part» con el semiespacio del plano de la cara que
// contiene el perfil (el plano se prolonga más allá de la cara).
Shape trimToPlane(const std::string& tag, const std::string& endName, const Shape& part, const TopoDS_Face& target,
                  const TopoDS_Face& profile)
{
    constexpr double big = 1e5;
    const gp_Pnt inside = BRepAdaptor_Surface(profile).Plane().Location();
    const TopoDS_Face bigFace = BRepBuilderAPI_MakeFace(BRepAdaptor_Surface(target).Plane(), -big, big, -big, big).Face();
    const TopoDS_Solid half = BRepPrimAPI_MakeHalfSpace(bigFace, inside).Solid();
    BRepAlgoAPI_Common common(occtShape(part), half);
    if (cancelRequested()) {
        throw GeometryError("Cálculo cancelado");
    }
    if (!common.IsDone() || common.HasErrors()) {
        throw GeometryError("No se pudo extruir hasta la cara");
    }
    Namer trimmed(common.Shape());
    trimmed.inherit(part, *common.History());
    trimmed.nameUnnamed(endName);  // la cara nueva viene del destino
    return trimmed.finish(tag);
}

// Lámina que prolonga la superficie libre de «face» (B-spline, de extrusión, de revolución…) más allá de sus bordes, para
// partir un prisma con ella. Las B-spline se alargan con GeomLib; las demás se recortan a los parámetros de la cara,
// ensanchados en su dirección ilimitada. Nula si no se pudo.
TopoDS_Shape extendedSurface(const TopoDS_Face& face, double reach)
{
    try {
        Handle(Geom_Surface) surface = BRep_Tool::Surface(face);
        if (surface.IsNull()) {
            return {};
        }
        if (Handle(Geom_RectangularTrimmedSurface) trimmed = Handle(Geom_RectangularTrimmedSurface)::DownCast(surface)) {
            surface = trimmed->BasisSurface();
        }
        double u1 = 0.0, u2 = 0.0, v1 = 0.0, v2 = 0.0;
        BRepTools::UVBounds(face, u1, u2, v1, v2);
        if (Handle(Geom_BoundedSurface) bounded = Handle(Geom_BoundedSurface)::DownCast(surface)) {
            Handle(Geom_BoundedSurface) copy = Handle(Geom_BoundedSurface)::DownCast(bounded->Copy());
            for (const bool inU : {true, false}) {
                for (const bool after : {false, true}) {
                    GeomLib::ExtendSurfByLength(copy, reach, 1, inU, after);
                }
            }
            return BRepBuilderAPI_MakeFace(copy, Precision::Confusion()).Shape();
        }
        // Extrusión y revolución: una de las dos direcciones no tiene límite; se ensancha esa.
        const double wide = reach * 2.0;
        if (!surface->IsUPeriodic() && Handle(Geom_SurfaceOfLinearExtrusion)::DownCast(surface).IsNull() == false) {
            return BRepBuilderAPI_MakeFace(surface, u1, u2, v1 - wide, v2 + wide, Precision::Confusion()).Shape();
        }
        return BRepBuilderAPI_MakeFace(surface, u1, u2, v1, v2, Precision::Confusion()).Shape();
    } catch (const Standard_Failure&) {
        return {};
    }
}

// Superficie libre: el extremo del tramo es una cara no plana cuyo centro está sobre la cara de destino real.
bool freeSurfaceCap(const TopoDS_Face& cap, const TopoDS_Face& target)
{
    if (BRepAdaptor_Surface(cap).GetType() == GeomAbs_Plane) {
        return false;
    }
    try {
        GProp_GProps props;
        BRepGProp::SurfaceProperties(cap, props);
        if (props.Mass() < 1e-9) {
            return false;
        }
        const TopoDS_Vertex center = BRepBuilderAPI_MakeVertex(props.CentreOfMass()).Vertex();
        BRepExtrema_DistShapeShape distance(center, target);
        return distance.IsDone() && distance.Value() < 1e-3;
    } catch (const Standard_Failure&) {
        return false;
    }
}

// Hasta una cara curva: cilíndrica, esférica, toroidal, cónica o libre. El prisma largo «part» se parte con la superficie
// completa en tramos (entra y sale de ella); la extrusión llega hasta el primer cruce que cae en el
// trozo REAL de la cara (los cruces con la superficie fuera de ese trozo se atraviesan), y ahí
// termina sobre la superficie.
Shape trimToCurvedFace(const std::string& tag, const std::string& endName, const Shape& part,
                       const TopoDS_Face& target, const TopoDS_Face& profile)
{
    const BRepAdaptor_Surface surface(target);
    // Cuerpos auxiliares del tamaño de la pieza (no enormes: con coordenadas muy grandes la booleana
    // pierde precisión).
    const Box box = part.bounds();
    const gp_Pnt center((box.min.x + box.max.x) / 2.0, (box.min.y + box.max.y) / 2.0, (box.min.z + box.max.z) / 2.0);
    const double reach = std::sqrt(std::pow(box.max.x - box.min.x, 2) + std::pow(box.max.y - box.min.y, 2)
                                   + std::pow(box.max.z - box.min.z, 2)) + 1.0;
    TopoDS_Shape limit;
    switch (surface.GetType()) {
    case GeomAbs_Cylinder: {
        const gp_Cylinder c = surface.Cylinder();
        // El Ax2 se arma con Axis().Direction(): Position().Ax2() invierte el eje si el sistema de la
        // cara es indirecto, y el cilindro auxiliar saldría fuera de sitio.
        const gp_Dir axis = c.Axis().Direction();
        const double t0 = gp_Vec(c.Location(), center).Dot(gp_Vec(axis));
        const gp_Ax2 ax(c.Location().Translated(gp_Vec(axis) * (t0 - reach)), axis, c.Position().XDirection());
        limit = BRepPrimAPI_MakeCylinder(ax, c.Radius(), 2.0 * reach).Shape();
        break;
    }
    case GeomAbs_Sphere: {
        const gp_Sphere sp = surface.Sphere();
        limit = BRepPrimAPI_MakeSphere(sp.Location(), sp.Radius()).Shape();
        break;
    }
    case GeomAbs_Torus: {
        const gp_Torus t = surface.Torus();
        limit = BRepPrimAPI_MakeTorus(gp_Ax2(t.Location(), t.Axis().Direction(), t.Position().XDirection()),
                                      t.MajorRadius(), t.MinorRadius()).Shape();
        break;
    }
    case GeomAbs_Cone: {
        // El cono completo (la hoja donde está la cara), del tamaño de la pieza: la hoja se corta en el vértice.
        const gp_Cone cone = surface.Cone();
        const gp_Dir axis = cone.Axis().Direction();
        const double slope = std::tan(cone.SemiAngle());
        const double t0 = gp_Vec(cone.Location(), center).Dot(gp_Vec(axis));
        const auto radiusAt = [&](double along) { return cone.RefRadius() + along * slope; };
        // La cara marca la hoja: la de los radios positivos en un punto suyo.
        double low = t0 - reach;
        double high = t0 + reach;
        const double apex = -cone.RefRadius() / slope;  // donde el radio vale 0
        if (slope > 0.0) {
            low = std::max(low, apex);
            high = std::max(high, low + 1.0);
        } else {
            high = std::min(high, apex);
            low = std::min(low, high - 1.0);
        }
        const gp_Ax2 ax(cone.Location().Translated(gp_Vec(axis) * low), axis, cone.Position().XDirection());
        limit = BRepPrimAPI_MakeCone(ax, std::max(0.0, radiusAt(low)), std::max(0.0, radiusAt(high)), high - low).Shape();
        break;
    }
    default:
        // Superficie libre (B-spline, de revolución…): se prolonga la superficie más allá de la cara y se usa como
        // lámina de corte (como el cuerpo auxiliar de las demás, pero abierto).
        limit = extendedSurface(target, reach);
        if (limit.IsNull()) {
            throw GeometryError("«Hasta una cara» no pudo prolongar esta superficie libre: elige otra cara o usa una distancia");
        }
        break;
    }

    NCollection_List<TopoDS_Shape> objects;
    NCollection_List<TopoDS_Shape> tools;
    objects.Append(occtShape(part));
    tools.Append(limit);
    BRepAlgoAPI_Splitter splitter;
    splitter.SetArguments(objects);
    splitter.SetTools(tools);
    splitter.Build();
    if (cancelRequested()) {
        throw GeometryError("Cálculo cancelado");
    }
    if (!splitter.IsDone() || splitter.HasErrors()) {
        throw GeometryError("No se pudo extruir hasta la cara: prueba con otra cara de destino o con una distancia");
    }

    // Los tramos, del perfil hacia fuera.
    struct Piece {
        double distance;
        Shape shape;
        TopoDS_Shape raw;
    };
    std::vector<Piece> pieces;
    for (TopExp_Explorer ex(splitter.Shape(), TopAbs_SOLID); ex.More(); ex.Next()) {
        BRepExtrema_DistShapeShape distance(profile, ex.Current());
        Namer namer(ex.Current());
        namer.inherit(part, *splitter.History());
        namer.nameUnnamed(endName);
        pieces.push_back({distance.IsDone() ? distance.Value() : 1e300, namer.finish(tag), ex.Current()});
    }
    std::ranges::sort(pieces, {}, &Piece::distance);

    // Primer tramo cuyo extremo (una cara sobre la superficie) cae en la cara real.
    std::optional<std::size_t> last;
    for (std::size_t i = 0; i < pieces.size() && !last; ++i) {
        for (TopExp_Explorer ex(pieces[i].raw, TopAbs_FACE); ex.More(); ex.Next()) {
            const TopoDS_Face cap = TopoDS::Face(ex.Current());
            const bool analytic = surface.GetType() == GeomAbs_Cylinder || surface.GetType() == GeomAbs_Sphere ||
                                  surface.GetType() == GeomAbs_Torus || surface.GetType() == GeomAbs_Cone;
            if (analytic ? (onSurface(cap, surface) && overlapsFace(cap, target)) : freeSurfaceCap(cap, target)) {
                last = i;
                break;
            }
        }
    }
    if (!last) {
        if (pieces.size() <= 1) {
            throw GeometryError("La extrusión no llega a la cara de destino: comprueba que esté delante del perfil, "
                                "en el sentido de la extrusión, y que no sea paralela a ella");
        }
        throw GeometryError("La extrusión cruza la superficie de la cara de destino, pero no en el trozo que forma "
                            "la cara: elige otra cara de destino o usa una distancia");
    }
    std::vector<Shape> kept;
    for (std::size_t i = 0; i <= *last; ++i) {
        kept.push_back(pieces[i].shape);
    }
    return fuseAll(kept, tag);
}

// Un lado de la extrusión: prisma de las caras a lo largo de «direction»
// (unitaria) desde «offset», con su ángulo y su límite. «second» distingue
// las tapas del segundo lado («inicio2», «fin2»).
Shape extrudeSide(const std::string& tag, const std::vector<ProfileFace>& faces, const gp_Dir& direction,
                  double offset, const ExtrudeSide& side, const std::vector<Shape>& context, bool second)
{
    const std::string startName = tag + (second ? ":inicio2:" : ":inicio:");
    const std::string endName = tag + (second ? ":fin2:" : ":fin:");
    double length = side.distance;
    if (side.extent != Extent::Distance) {
        TopoDS_Compound all;
        BRep_Builder builder;
        builder.MakeCompound(all);
        for (const ProfileFace& pf : faces) {
            builder.Add(all, pf.face);
        }
        length = throughLength(context, all);
    }
    if (!(length > kTiny)) {
        throw GeometryError("La distancia de la extrusión debe ser distinta de cero");
    }

    std::vector<Shape> parts;
    for (const ProfileFace& pf : faces) {
        TopoDS_Face face = pf.face;
        auto sources = pf.sources;
        if (std::abs(offset) > kTiny) {
            gp_Trsf move;
            move.SetTranslation(gp_Vec(direction) * offset);
            BRepBuilderAPI_Transform moved(face, move, true);
            face = TopoDS::Face(moved.Shape());
            for (auto& [edge, source] : sources) {
                edge = TopoDS::Edge(moved.ModifiedShape(edge));
            }
        }
        BRepPrimAPI_MakePrism prism(face, gp_Vec(direction) * length, true);
        if (!prism.IsDone()) {
            throw GeometryError("No se pudo extruir el perfil");
        }
        Namer namer(prism.Shape());
        namer.name(prism.FirstShape(), startName + pf.key);
        namer.name(prism.LastShape(), endName + pf.key);
        // Laterales: cada arista del perfil genera una cara.
        for (const auto& [edge, source] : sources) {
            for (const TopoDS_Shape& generated : prism.Generated(edge)) {
                namer.name(generated, tag + ":lado:" + source);
            }
        }
        Shape part = namer.finish(tag);

        if (side.taper < -kTiny && collapses(face, length * std::tan(-side.taper))) {
            throw GeometryError("Con ese ángulo el perfil se cierra antes de llegar a la distancia: "
                                "reduce el ángulo o la distancia");
        }
        if (std::abs(side.taper) > kTiny) {
            // Ángulo: las laterales giran alrededor de su arista en el plano
            // del perfil (DraftAngle cierra con ángulo positivo; aquí abre).
            BRepOffsetAPI_DraftAngle draft(occtShape(part));
            const gp_Pln base(BRepAdaptor_Surface(face).Plane().Location(), direction);
            for (int i = 0; i < part.faceCount(); ++i) {
                if (part.faceName(i).find(":lado:") == std::string::npos) {
                    continue;
                }
                draft.Add(faceAt(part, i), direction, -side.taper, base);
                if (!draft.AddDone()) {
                    throw GeometryError("El ángulo es demasiado grande para este perfil");
                }
            }
            draft.Build();
            if (!draft.IsDone()) {
                throw GeometryError("No se pudo aplicar el ángulo");
            }
            // DraftAngle no rellena Modified(): cada cara se sigue con
            // ModifiedShape().
            Namer drafted(draft.Shape());
            for (int i = 0; i < part.faceCount(); ++i) {
                drafted.name(draft.ModifiedShape(faceAt(part, i)), part.faceName(i));
            }
            part = drafted.finish(tag);
        }

        if (side.extent == Extent::ToFace) {
            // Recorta el prisma largo en la cara de destino, del lado del perfil.
            const TopoDS_Face target = targetFace(context, side.face);
            if (BRepAdaptor_Surface(target).GetType() == GeomAbs_Plane) {
                part = trimToPlane(tag, endName + pf.key, part, target, face);
            } else {
                part = trimToCurvedFace(tag, endName + pf.key, part, target, face);
            }
            if (part.solidCount() == 0) {
                throw GeometryError("La cara de destino no está delante del perfil");
            }
        }
        parts.push_back(part);
    }
    return fuseAll(parts, tag);
}

Shape extrudeFaces(const std::string& tag, const std::vector<ProfileFace>& faces, const gp_Dir& normal,
                   const ExtrudeSpec& spec, const std::vector<Shape>& context)
{
    std::vector<Shape> sides;
    switch (spec.direction) {
    case ExtrudeDirection::One: {
        ExtrudeSide side = spec.side1;
        const bool backwards = side.extent == Extent::Distance && side.distance < 0.0;
        side.distance = std::abs(side.distance);
        sides.push_back(extrudeSide(tag, faces, backwards ? normal.Reversed() : normal,
                                    backwards ? -spec.startOffset : spec.startOffset, side, context, false));
        break;
    }
    case ExtrudeDirection::Symmetric:
    case ExtrudeDirection::Two: {
        ExtrudeSide a = spec.side1;
        ExtrudeSide b = spec.direction == ExtrudeDirection::Two ? spec.side2 : spec.side1;
        a.distance = std::abs(a.distance);
        b.distance = std::abs(b.distance);
        sides.push_back(extrudeSide(tag, faces, normal, spec.startOffset, a, context, false));
        sides.push_back(extrudeSide(tag, faces, normal.Reversed(), -spec.startOffset, b, context, true));
        break;
    }
    }
    return fuseAll(sides, tag);
}

} // namespace

Shape extrude(std::string_view tag, const SketchRegion& region, const ExtrudeSpec& spec,
              const std::vector<Shape>& context)
{
    try {
        return extrudeFaces(std::string(tag), regionFaces(region), toDir(region.plane.normal), spec, context);
    } catch (const Standard_Failure& e) {
        throw kernelFailure("extruir el perfil", "revisa que el perfil esté cerrado y no se cruce consigo mismo, y que la distancia no sea enorme", e);
    }
}

Shape extrudeFace(std::string_view tag, const Shape& body, int faceIndex, const ExtrudeSpec& spec,
                  const std::vector<Shape>& context)
{
    try {
        const sketch::Plane plane = body.facePlane(faceIndex).value_or(sketch::Plane{});
        return extrudeFaces(std::string(tag), {bodyFace(body, faceIndex)}, toDir(plane.normal), spec, context);
    } catch (const Standard_Failure& e) {
        throw kernelFailure("extruir el perfil", "revisa que el perfil esté cerrado y no se cruce consigo mismo, y que la distancia no sea enorme", e);
    }
}

Shape revolve(std::string_view tagView, const SketchRegion& region, const Axis& axis, const RevolveSpec& spec)
{
    const std::string tag(tagView);
    try {
        const std::vector<ProfileFace> faces = regionFaces(region);
        const double full = 2.0 * std::numbers::pi;
        const gp_Ax1 ax(toPnt(axis.point), toDir(axis.direction));

        // Un lado: de «start» (radianes) girando «angle».
        const auto side = [&](double start, double angle) {
            std::vector<Shape> parts;
            for (const ProfileFace& pf : faces) {
                TopoDS_Shape face = pf.face;
                auto edges = pf.sources;
                if (std::abs(start) > kTiny) {
                    gp_Trsf turn;
                    turn.SetRotation(ax, start);
                    BRepBuilderAPI_Transform moved(face, turn, true);
                    face = moved.Shape();
                    for (auto& [edge, source] : edges) {
                        edge = TopoDS::Edge(moved.ModifiedShape(edge));
                    }
                }
                BRepPrimAPI_MakeRevol revol(face, ax, angle, true);
                if (!revol.IsDone()) {
                    throw GeometryError("No se pudo revolucionar el perfil (¿el eje lo cruza?)");
                }
                Namer namer(revol.Shape());
                if (std::abs(angle) < full - kTiny) {
                    namer.name(revol.FirstShape(), tag + ":inicio:" + pf.key);
                    namer.name(revol.LastShape(), tag + ":fin:" + pf.key);
                }
                for (const auto& [edge, source] : edges) {
                    for (const TopoDS_Shape& generated : revol.Generated(edge)) {
                        namer.name(generated, tag + ":lado:" + source);
                    }
                }
                parts.push_back(namer.finish(tag));
            }
            return fuseAll(parts, tag);
        };

        if (spec.extent == RevolveExtent::Full) {
            return side(0.0, full);
        }
        const auto clampAngle = [&](double a) { return std::clamp(a, -full, full); };
        switch (spec.direction) {
        case ExtrudeDirection::One:
            if (std::abs(spec.angle1) < kTiny) {
                throw GeometryError("El ángulo debe ser distinto de cero");
            }
            return side(0.0, clampAngle(spec.angle1));
        case ExtrudeDirection::Symmetric: {
            const double a = clampAngle(std::abs(spec.angle1));
            return side(-std::min(a, full / 2.0), 2.0 * std::min(a, full / 2.0));
        }
        case ExtrudeDirection::Two: {
            const double a = std::abs(spec.angle1);
            const double b = std::abs(spec.angle2);
            const double total = std::min(a + b, full);
            return side(-std::min(b, full - a), total);
        }
        }
        throw GeometryError("Revolución no válida");
    } catch (const Standard_Failure& e) {
        throw kernelFailure("revolucionar el perfil", "revisa que el eje no cruce el perfil y que el perfil esté cerrado", e);
    }
}

Shape fuseBalanced(std::vector<Shape> parts)
{
    if (parts.empty()) {
        return {};
    }
    while (parts.size() > 1) {
        std::vector<Shape> next;
        next.reserve(parts.size() / 2 + 1);
        for (std::size_t i = 0; i + 1 < parts.size(); i += 2) {
            throwIfCancelled();
            next.push_back(boolean(BooleanOp::Fuse, parts[i], parts[i + 1]));
        }
        if (parts.size() % 2 == 1) {
            next.push_back(std::move(parts.back()));
        }
        parts = std::move(next);
    }
    return std::move(parts.front());
}

Shape boolean(BooleanOp op, const Shape& target, const Shape& tool)
{
    try {
        const TopoDS_Shape& a = occtShape(target);
        const TopoDS_Shape& b = occtShape(tool);
        std::unique_ptr<BRepAlgoAPI_BooleanOperation> algo;
        switch (op) {
        case BooleanOp::Fuse:
            algo = std::make_unique<BRepAlgoAPI_Fuse>();
            break;
        case BooleanOp::Cut:
            algo = std::make_unique<BRepAlgoAPI_Cut>();
            break;
        case BooleanOp::Common:
            algo = std::make_unique<BRepAlgoAPI_Common>();
            break;
        }
        // En paralelo (el kernel reparte el trabajo entre los núcleos).
        NCollection_List<TopoDS_Shape> objects;
        NCollection_List<TopoDS_Shape> tools;
        objects.Append(a);
        tools.Append(b);
        algo->SetArguments(objects);
        algo->SetTools(tools);
        algo->SetRunParallel(true);
        buildCancellable(*algo);
        if (!algo->IsDone() || algo->HasErrors()) {
            throw GeometryError("No se pudo combinar los cuerpos: puede que solo se toquen en una arista o un punto, o que tengan caras casi coincidentes; prueba a moverlos una décima de milímetro");
        }
        Namer namer(algo->Shape());
        namer.inherit(target, *algo->History());
        namer.inherit(tool, *algo->History());
        Shape result = namer.finish("bool");
        if (result.solidCount() == 0) {
            return {};
        }
        // Si unir las caras coplanares estropea el sólido, mejor sin unirlas.
        Shape unified = unify(result, "bool");
        // La comprobación completa (con los controles de geometría) cuesta tanto
        // como la propia booleana; aquí solo hace falta saber si unir las caras
        // estropeó la topología.
        const auto sound = [](const Shape& shape) {
            return !shape.isNull() && BRepCheck_Analyzer(occtShape(shape), false).IsValid();
        };
        return sound(unified) || !sound(result) ? unified : result;
    } catch (const Standard_Failure& e) {
        throw kernelFailure("combinar los cuerpos", "puede que solo se toquen en una arista o un punto, o que tengan caras casi coincidentes; prueba a moverlos una décima de milímetro", e);
    }
}

std::optional<Overlap> overlap(const Shape& a, const Shape& b, double minVolume)
{
    if (a.isNull() || b.isNull()) {
        return std::nullopt;
    }
    const Box ba = a.bounds();
    const Box bb = b.bounds();
    if (ba.max.x <= bb.min.x || bb.max.x <= ba.min.x || ba.max.y <= bb.min.y || bb.max.y <= ba.min.y
        || ba.max.z <= bb.min.z || bb.max.z <= ba.min.z) {
        return std::nullopt;
    }
    try {
        BRepAlgoAPI_Common common(occtShape(a), occtShape(b));
        buildCancellable(common);
        if (!common.IsDone() || common.Shape().IsNull()) {
            return std::nullopt;
        }
        GProp_GProps props;
        BRepGProp::VolumeProperties(common.Shape(), props);
        const double volume = std::abs(props.Mass());
        if (volume < minVolume) {
            return std::nullopt;
        }
        Namer namer(common.Shape());
        namer.nameUnnamed("interferencia");
        return Overlap{namer.finish("interferencia"), volume};
    } catch (const Standard_Failure&) {
        return std::nullopt;
    }
}

bool touches(const Shape& a, const Shape& b)
{
    if (a.isNull() || b.isNull()) {
        return false;
    }
    const Box ba = a.bounds();
    const Box bb = b.bounds();
    constexpr double gap = 1e-6;
    if (ba.max.x < bb.min.x - gap || bb.max.x < ba.min.x - gap || ba.max.y < bb.min.y - gap
        || bb.max.y < ba.min.y - gap || ba.max.z < bb.min.z - gap || bb.max.z < ba.min.z - gap) {
        return false;
    }
    // Solo cuentan las caras de «a» cuya caja se cruza con la de «b»: medir la
    // distancia contra el cuerpo entero crece con todas sus caras.
    Bnd_Box toolBox;
    BRepBndLib::AddOptimal(occtShape(b), toolBox, false, false);
    toolBox.Enlarge(gap);
    BRep_Builder builder;
    TopoDS_Compound near;
    builder.MakeCompound(near);
    bool any = false;
    for (int i = 0; i < a.faceCount(); ++i) {
        Bnd_Box box;
        BRepBndLib::AddOptimal(faceAt(a, i), box, false, false);
        if (!box.IsOut(toolBox)) {
            builder.Add(near, faceAt(a, i));
            any = true;
        }
    }
    if (!any) {
        return false;
    }
    BRepExtrema_DistShapeShape distance(near, occtShape(b));
    return distance.IsDone() && distance.Value() < 1e-6;
}

// Redondeo y chaflán comparten todo salvo la herramienta del kernel.
namespace {

// «add» pone una arista en la operación (por defecto, con una sola medida).
template <typename Maker>
Shape edgeOperation(std::string_view tag, const std::string& label, const Shape& body, const std::vector<int>& edges,
                    double size, const std::string& failure,
                    const std::function<void(Maker&, const TopoDS_Edge&)>& add = {})
{
    if (body.isNull()) {
        throw GeometryError("No hay cuerpo sobre el que trabajar");
    }
    if (edges.empty()) {
        throw GeometryError("No hay aristas elegidas");
    }
    if (size <= kTiny) {
        throw GeometryError("La medida debe ser mayor que cero");
    }
    try {
        Maker maker(occtShape(body));
        for (int edge : edges) {
            if (add) {
                add(maker, edgeAt(body, edge));
            } else {
                maker.Add(size, edgeAt(body, edge));
            }
        }
        buildCancellable(maker);
        if (!maker.IsDone() || maker.Shape().IsNull()) {
            throw GeometryError(failure);
        }
        Namer namer(maker.Shape());
        const Shape::Impl& in = *body.impl();
        // Lo que sigue en el resultado conserva su nombre.
        for (int i = 1; i <= in.faces.Extent(); ++i) {
            const TopoDS_Shape& face = in.faces(i);
            if (maker.IsDeleted(face)) {
                continue;
            }
            const auto& modified = maker.Modified(face);
            if (modified.IsEmpty()) {
                namer.name(face, in.faceNames[std::size_t(i - 1)]);
            }
            for (const TopoDS_Shape& image : modified) {
                namer.name(image, in.faceNames[std::size_t(i - 1)]);
            }
        }
        // Las caras nuevas se nombran por la arista de la que salen.
        for (int edge : edges) {
            for (const TopoDS_Shape& generated : maker.Generated(edgeAt(body, edge))) {
                namer.name(generated, std::string(tag) + ":" + label + ":" + body.edgeName(edge));
            }
        }
        namer.nameUnnamed(std::string(tag) + ":esquina");
        Shape result = namer.finish(std::string(tag));
        if (!result.isValid()) {
            throw GeometryError(failure);
        }
        return result;
    } catch (const Standard_Failure& e) {
        throw GeometryError(failure + " (" + kernelMessage(e) + ")");
    }
}

} // namespace

Shape fillet(std::string_view tag, const Shape& body, const std::vector<int>& edges, double radius)
{
    return edgeOperation<BRepFilletAPI_MakeFillet>(tag, "redondeo", body, edges, radius,
                                                   "No se pudo redondear: prueba con un radio menor");
}

Shape chamfer(std::string_view tag, const Shape& body, const std::vector<int>& edges, double distance)
{
    return edgeOperation<BRepFilletAPI_MakeChamfer>(tag, "chaflan", body, edges, distance,
                                                     "No se pudo achaflanar: prueba con una distancia menor");
}

Shape filletVariable(std::string_view tag, const Shape& body, const std::vector<int>& edges, double start, double end, double middle)
{
    if (start <= kTiny || end <= kTiny || middle < 0.0) {
        throw GeometryError("Los radios del redondeo deben ser mayores que cero");
    }
    const std::function<void(BRepFilletAPI_MakeFillet&, const TopoDS_Edge&)> add = [&](BRepFilletAPI_MakeFillet& maker, const TopoDS_Edge& edge) {
        if (middle > kTiny) {
            NCollection_Array1<gp_Pnt2d> law(1, 3);
            law.SetValue(1, gp_Pnt2d(0.0, start));
            law.SetValue(2, gp_Pnt2d(0.5, middle));
            law.SetValue(3, gp_Pnt2d(1.0, end));
            maker.Add(law, edge);
        } else {
            maker.Add(start, end, edge);
        }
    };
    return edgeOperation<BRepFilletAPI_MakeFillet>(tag, "redondeo", body, edges, std::max({start, end, middle}),
                                                   "No se pudo redondear: prueba con radios menores", add);
}

Shape chamfer(std::string_view tag, const Shape& body, const std::vector<int>& edges, const ChamferSpec& spec)
{
    if (spec.style == ChamferStyle::Equal) {
        return chamfer(tag, body, edges, spec.distance);
    }
    if (spec.distance <= kTiny || (spec.style == ChamferStyle::TwoDistances && spec.distance2 <= kTiny)) {
        throw GeometryError("Las distancias del chaflán deben ser mayores que cero");
    }
    if (spec.style == ChamferStyle::DistanceAngle && (spec.angle <= 1e-3 || spec.angle >= std::numbers::pi / 2.0 - 1e-3)) {
        throw GeometryError("El ángulo del chaflán debe estar entre 0° y 90°");
    }
    if (body.isNull()) {
        throw GeometryError("No hay cuerpo sobre el que trabajar");
    }
    // La cara de referencia de cada arista: la primera de las dos que la comparten (o la segunda con «flip»).
    NCollection_IndexedDataMap<TopoDS_Shape, NCollection_List<TopoDS_Shape>, TopTools_ShapeMapHasher> ancestors;
    TopExp::MapShapesAndAncestors(occtShape(body), TopAbs_EDGE, TopAbs_FACE, ancestors);
    const std::function<void(BRepFilletAPI_MakeChamfer&, const TopoDS_Edge&)> add = [&](BRepFilletAPI_MakeChamfer& maker,
                                                                                     const TopoDS_Edge& edge) {
        const int found = ancestors.FindIndex(edge);
        if (found == 0) {
            throw GeometryError("Una arista no pertenece a ninguna cara");
        }
        const NCollection_List<TopoDS_Shape>& faces = ancestors.FindFromIndex(found);
        std::vector<TopoDS_Face> around;
        for (const TopoDS_Shape& face : faces) {
            around.push_back(TopoDS::Face(face));
        }
        if (around.empty()) {
            throw GeometryError("Una arista no pertenece a ninguna cara");
        }
        const TopoDS_Face& reference = around[spec.flip && around.size() > 1 ? 1 : 0];
        if (spec.style == ChamferStyle::TwoDistances) {
            maker.Add(spec.distance, spec.distance2, edge, reference);
        } else {
            maker.AddDA(spec.distance, spec.angle, edge, reference);
        }
    };
    return edgeOperation<BRepFilletAPI_MakeChamfer>(tag, "chaflan", body, edges, spec.distance,
                                                     "No se pudo achaflanar: prueba con distancias menores", add);
}

std::pair<Shape, Shape> splitBody(std::string_view tag, const Shape& body, const sketch::Plane& plane)
{
    if (body.isNull()) {
        throw GeometryError("No hay cuerpo que dividir");
    }
    const Box b = body.bounds();
    const double reach = 2.0 * std::hypot(std::hypot(b.max.x - b.min.x, b.max.y - b.min.y), b.max.z - b.min.z) + 10.0;
    // Dos bloques enormes, uno a cada lado del plano (el segundo con la normal invertida).
    sketch::Plane front = plane;
    sketch::Plane back = plane;
    back.normal = {-plane.normal.x, -plane.normal.y, -plane.normal.z};
    back.yAxis = {-plane.yAxis.x, -plane.yAxis.y, -plane.yAxis.z};
    const Shape blockFront = box(std::string(tag) + ":lado", front, reach, reach, reach, true, 0.0);
    const Shape blockBack = box(std::string(tag) + ":lado2", back, reach, reach, reach, true, 0.0);
    Shape first = boolean(BooleanOp::Common, body, blockFront);
    Shape second = boolean(BooleanOp::Common, body, blockBack);
    if (first.isNull() || second.isNull() || first.solidCount() == 0 || second.solidCount() == 0 || first.volume() < kTiny ||
        second.volume() < kTiny) {
        throw GeometryError("El plano no atraviesa el cuerpo: no hay nada que dividir");
    }
    return {std::move(first), std::move(second)};
}

std::pair<Shape, Shape> splitBodyByFace(std::string_view tagView, const Shape& body, const Shape& toolBody, int toolFace)
{
    const std::string tag(tagView);
    if (body.isNull() || toolBody.isNull()) {
        throw GeometryError("No hay cuerpo que dividir");
    }
    if (toolFace < 0 || toolFace >= toolBody.faceCount()) {
        throw GeometryError("Ya no existe la cara con que se iba a dividir");
    }
    try {
        NCollection_List<TopoDS_Shape> objects;
        NCollection_List<TopoDS_Shape> tools;
        objects.Append(occtShape(body));
        tools.Append(toolBody.impl()->faces(toolFace + 1));
        BRepAlgoAPI_Splitter splitter;
        splitter.SetArguments(objects);
        splitter.SetTools(tools);
        splitter.Build();
        if (!splitter.IsDone() || splitter.HasErrors()) {
            throw GeometryError("No se pudo dividir el cuerpo con esa cara");
        }
        std::vector<TopoDS_Shape> solids;
        for (TopExp_Explorer ex(splitter.Shape(), TopAbs_SOLID); ex.More(); ex.Next()) {
            // Sin dividir, el resultado puede traer el mismo sólido más de una vez: no cuenta como trozo nuevo.
            if (std::ranges::none_of(solids, [&](const TopoDS_Shape& known) { return known.IsSame(ex.Current()); })) {
                solids.push_back(ex.Current());
            }
        }
        if (solids.size() < 2) {
            throw GeometryError("La cara no atraviesa el cuerpo de lado a lado: no hay nada que dividir");
        }
        const Shape::Impl& toolIn = *toolBody.impl();
        const auto make = [&](const std::vector<TopoDS_Shape>& pieces, const std::string& label) {
            TopoDS_Shape shape;
            if (pieces.size() == 1) {
                shape = pieces.front();
            } else {
                BRep_Builder builder;
                TopoDS_Compound compound;
                builder.MakeCompound(compound);
                for (const TopoDS_Shape& piece : pieces) {
                    builder.Add(compound, piece);
                }
                shape = compound;
            }
            Namer namer(shape);
            namer.inherit(body, *splitter.History());
            for (const TopoDS_Shape& image : splitter.Modified(toolIn.faces(toolFace + 1))) {
                namer.name(image, label);
            }
            namer.nameUnnamed(label);
            return namer.finish(tag);
        };
        Shape first = make({solids.front()}, tag + ":corte");
        Shape second = make(std::vector<TopoDS_Shape>(solids.begin() + 1, solids.end()), tag + ":corte2");
        return {std::move(first), std::move(second)};
    } catch (const Standard_Failure& e) {
        throw kernelFailure("dividir el cuerpo", "elige una cara que atraviese el cuerpo de lado a lado", e);
    }
}

Shape mirror(std::string_view tag, const Shape& body, const sketch::Plane& plane)
{
    if (body.isNull()) {
        throw GeometryError("No hay cuerpo que reflejar");
    }
    try {
        gp_Trsf reflection;
        reflection.SetMirror(gp_Ax2(toPnt(plane.origin), toDir(plane.normal)));
        BRepBuilderAPI_Transform transform(occtShape(body), reflection, true);
        if (!transform.IsDone() || transform.Shape().IsNull()) {
            throw GeometryError("No se pudo reflejar el cuerpo");
        }
        // Un espejo invierte la orientación de los sólidos (quedarían «del
        // revés», con volumen negativo): se vuelven a orientar hacia fuera.
        const TopoDS_Shape reflected = transform.Shape();
        TopoDS_Shape result = reflected;
        if (reflected.ShapeType() == TopAbs_SOLID) {
            TopoDS_Solid solid = TopoDS::Solid(reflected);
            BRepLib::OrientClosedSolid(solid);
            result = solid;
        } else {
            BRep_Builder builder;
            TopoDS_Compound compound;
            builder.MakeCompound(compound);
            for (TopExp_Explorer ex(reflected, TopAbs_SOLID); ex.More(); ex.Next()) {
                TopoDS_Solid solid = TopoDS::Solid(ex.Current());
                BRepLib::OrientClosedSolid(solid);
                builder.Add(compound, solid);
            }
            result = compound;
        }
        Namer namer(result);
        const Shape::Impl& in = *body.impl();
        for (int i = 1; i <= in.faces.Extent(); ++i) {
            for (const TopoDS_Shape& image : transform.Modified(in.faces(i))) {
                namer.name(image, std::string(tag) + ":espejo:" + in.faceNames[std::size_t(i - 1)]);
            }
        }
        namer.nameUnnamed(std::string(tag) + ":espejo");
        return namer.finish(std::string(tag));
    } catch (const Standard_Failure& e) {
        throw kernelFailure("reflejar el cuerpo", "revisa que el plano de simetría no sea tangente al cuerpo", e);
    }
}

std::vector<Shape> pattern(std::string_view tag, const Shape& body, const PatternSpec& spec)
{
    if (body.isNull()) {
        throw GeometryError("No hay cuerpo que repetir");
    }
    const int total = spec.offsets.empty() ? spec.count : int(spec.offsets.size()) + 1;
    if (!spec.offsets.empty()) {
        if (total > 500) {
            throw GeometryError("Demasiados elementos en el patrón (máximo 500)");
        }
    } else if (spec.count < 2) {
        throw GeometryError("Un patrón necesita al menos 2 elementos");
    }
    if (spec.count > 500) {
        throw GeometryError("Demasiados elementos en el patrón (máximo 500)");
    }
    const sketch::Vec3& dir = spec.axis.direction;
    if (spec.offsets.empty() && std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z) <= kTiny) {
        throw GeometryError("La dirección del patrón no es válida");
    }
    if (spec.offsets.empty() && !spec.circular && std::abs(spec.spacing) <= kTiny) {
        throw GeometryError("La distancia entre copias debe ser distinta de cero");
    }
    if (spec.offsets.empty() && spec.circular && std::abs(spec.angle) <= 1e-9) {
        throw GeometryError("El ángulo del patrón debe ser distinto de cero");
    }
    try {
        const TopoDS_Shape source = occtShape(body);
        const Shape::Impl& in = *body.impl();
        const bool full = std::abs(spec.angle) >= 6.283185307179586 - 1e-6;
        const double step = full ? spec.angle / total : spec.angle / std::max(1, total - 1);
        std::vector<Shape> copies;
        for (int k = 1; k < total; ++k) {
            gp_Trsf move;
            if (!spec.offsets.empty()) {
                const sketch::Vec3& o = spec.offsets[std::size_t(k - 1)];
                move.SetTranslation(gp_Vec(o.x, o.y, o.z));
            } else if (spec.circular) {
                move.SetRotation(gp_Ax1(toPnt(spec.axis.point), toDir(spec.axis.direction)), step * k);
            } else {
                move.SetTranslation(gp_Vec(toDir(spec.axis.direction)).Multiplied(spec.spacing * k));
            }
            BRepBuilderAPI_Transform transform(source, move, true);
            if (!transform.IsDone() || transform.Shape().IsNull()) {
                throw GeometryError("No se pudo copiar el cuerpo");
            }
            const std::string label = std::string(tag) + ":patron" + std::to_string(k);
            Namer namer(transform.Shape());
            for (int i = 1; i <= in.faces.Extent(); ++i) {
                for (const TopoDS_Shape& image : transform.Modified(in.faces(i))) {
                    namer.name(image, label + ":" + in.faceNames[std::size_t(i - 1)]);
                }
            }
            namer.nameUnnamed(label);
            copies.push_back(namer.finish(std::string(tag)));
        }
        return copies;
    } catch (const Standard_Failure& e) {
        throw kernelFailure("repetir el cuerpo", "prueba con menos copias o con otra distancia entre ellas", e);
    }
}

std::vector<sketch::Vec3> pathPoints(const sketch::Sketch& pathSketch, const sketch::Plane& pathPlane,
                                     const std::vector<sketch::EntityId>& pathEntities, int count)
{
    if (pathEntities.empty()) {
        throw GeometryError("Elige la trayectoria del patrón");
    }
    if (count < 2 || count > 500) {
        throw GeometryError("El patrón necesita entre 2 y 500 elementos");
    }
    try {
        const gp_Ax3 pathAxes = toAx3(pathPlane);
        NCollection_List<TopoDS_Shape> edges;
        for (const sketch::EntityId id : pathEntities) {
            const sketch::Entity* entity = pathSketch.entity(id);
            const auto edge = entity ? sketchEdge(*entity, pathAxes) : std::nullopt;
            if (!edge) {
                throw GeometryError("Una curva de la trayectoria ya no existe o no se puede usar");
            }
            edges.Append(*edge);
        }
        BRepBuilderAPI_MakeWire pathMaker;
        pathMaker.Add(edges);
        if (!pathMaker.IsDone()) {
            throw GeometryError("Las curvas de la trayectoria no forman un camino continuo");
        }
        const BRepAdaptor_CompCurve curve(pathMaker.Wire());
        GCPnts_UniformAbscissa spread(curve, count);
        if (!spread.IsDone() || spread.NbPoints() < 2) {
            throw GeometryError("No se pudieron repartir los elementos a lo largo de la trayectoria");
        }
        std::vector<sketch::Vec3> points;
        for (int i = 1; i <= spread.NbPoints(); ++i) {
            const gp_Pnt p = curve.Value(spread.Parameter(i));
            points.push_back({p.X(), p.Y(), p.Z()});
        }
        return points;
    } catch (const Standard_Failure& e) {
        throw kernelFailure("repartir los elementos", "revisa que las curvas formen un camino continuo", e);
    }
}

namespace {

// Cremallera: barra recta con dientes trapezoidales (la involuta de radio infinito son flancos rectos).
Shape rackGear(const std::string& tag, const sketch::Plane& plane, const GearSpec& spec)
{
    constexpr double kPi = std::numbers::pi;
    constexpr double kDeg = kPi / 180.0;
    if (spec.teeth < 2 || spec.teeth > 300) {
        throw GeometryError("El número de dientes de la cremallera debe estar entre 2 y 300");
    }
    if (spec.module <= kTiny) {
        throw GeometryError("El módulo debe ser mayor que cero");
    }
    if (std::abs(spec.thickness) <= kTiny) {
        throw GeometryError("El ancho de la cremallera debe ser distinto de cero");
    }
    if (spec.pressureAngle < 5.0 * kDeg || spec.pressureAngle > 35.0 * kDeg) {
        throw GeometryError("El ángulo de presión debe estar entre 5° y 35°");
    }
    if (std::abs(spec.helixAngle) > 45.0 * kDeg) {
        throw GeometryError("El ángulo de hélice no puede pasar de 45°");
    }
    if (spec.herringbone && std::abs(spec.helixAngle) < kTiny) {
        throw GeometryError("La espina de pescado necesita un ángulo de hélice");
    }
    if (spec.rim <= kTiny) {
        throw GeometryError("El grosor de la base de la cremallera debe ser mayor que cero");
    }
    const double m = spec.module;
    const double p = kPi * m;
    const double t = std::tan(spec.pressureAngle);
    if (spec.backlash < 0.0) {
        throw GeometryError("El juego entre dientes no puede ser negativo");
    }
    // El juego adelgaza cada diente: medio juego por lado.
    const double tipHalf = p / 4.0 - m * t - spec.backlash / 2.0;
    const double rootHalf = p / 4.0 + 1.25 * m * t - spec.backlash / 2.0;
    if (tipHalf <= 0.02 * p) {
        throw GeometryError("Los dientes salen puntiagudos: prueba con menos ángulo de presión");
    }
    if (p - 2.0 * rootHalf <= 0.02 * p) {
        throw GeometryError("Los dientes se solapan en la raíz: prueba con menos ángulo de presión");
    }
    try {
        const double fullLength = spec.teeth * p;
        // Con «length» la barra se corta a esa medida (el último diente queda cortado).
        const double length = spec.length > kTiny ? spec.length : fullLength;
        if (length > fullLength + 1e-9) {
            throw GeometryError("El largo de la cremallera no puede pasar de dientes · π · módulo (" + std::to_string(fullLength) + " mm)");
        }
        if (length <= p) {
            throw GeometryError("El largo de la cremallera debe ser mayor que un paso (π · módulo = " + std::to_string(p) + " mm)");
        }
        const double bottom = -(1.25 * m + spec.rim);
        const auto at = [&](double x, double y) { return toPnt(plane.toWorld({x, y})); };
        std::vector<std::pair<TopoDS_Edge, std::string>> edges;
        const auto line = [&](double x1, double y1, double x2, double y2, const std::string& name) {
            edges.emplace_back(BRepBuilderAPI_MakeEdge(at(x1, y1), at(x2, y2)).Edge(), name);
        };
        // Los segmentos de arriba, de derecha a izquierda, empezando por el extremo de la barra entera.
        struct Top {
            double x1, y1, x2, y2;
            std::string name;
        };
        std::vector<Top> top;
        {
            double x = fullLength;
            for (int i = spec.teeth - 1; i >= 0; --i) {
                const double c = p / 2.0 + i * p;
                const std::string name = "d" + std::to_string(i + 1) + ":";
                top.push_back({x, -1.25 * m, c + rootHalf, -1.25 * m, name + "raiz_d"});
                top.push_back({c + rootHalf, -1.25 * m, c + tipHalf, m, name + "flanco_d"});
                top.push_back({c + tipHalf, m, c - tipHalf, m, name + "cabeza"});
                top.push_back({c - tipHalf, m, c - rootHalf, -1.25 * m, name + "flanco_i"});
                x = c - rootHalf;
            }
            top.push_back({x, -1.25 * m, 0.0, -1.25 * m, "raiz_i"});
        }
        // Lo que pasa del largo pedido se quita: los segmentos que quedan enteros más allá se descartan y el que lo cruza
        // se corta justo en x = length (conserva su nombre).
        std::vector<Top> kept;
        for (const Top& s : top) {
            if (s.x2 >= length - kTiny) {
                continue;
            }
            Top clipped = s;
            if (s.x1 > length + kTiny) {
                const double along = (length - s.x1) / (s.x2 - s.x1);
                clipped.x1 = length;
                clipped.y1 = s.y1 + along * (s.y2 - s.y1);
            }
            if (std::hypot(clipped.x2 - clipped.x1, clipped.y2 - clipped.y1) < 1e-6) {
                continue;  // el corte cae justo en un vértice: sin tramos de largo cero
            }
            kept.push_back(clipped);
        }
        if (kept.empty()) {
            throw GeometryError("El largo de la cremallera no deja ningún diente");
        }
        line(0.0, bottom, length, bottom, "base");
        line(length, bottom, length, kept.front().y1, "extremo_d");
        for (const Top& s : kept) {
            line(s.x1, s.y1, s.x2, s.y2, s.name);
        }
        line(0.0, -1.25 * m, 0.0, bottom, "extremo_i");
        BRepBuilderAPI_MakeWire outline;
        for (const auto& [edge, name] : edges) {
            outline.Add(edge);
            if (!outline.IsDone()) {
                throw GeometryError("No se pudo cerrar el contorno de la cremallera");
            }
        }
        {
            TopoDS_Iterator children(outline.Wire());
            for (auto& [edge, name] : edges) {
                if (!children.More()) {
                    throw GeometryError("El contorno de la cremallera quedó incompleto");
                }
                edge = TopoDS::Edge(children.Value());
                children.Next();
            }
        }
        const gp_Dir normal = toDir(plane.normal);
        const gp_Pnt origin = toPnt(plane.origin);
        if (std::abs(spec.helixAngle) < kTiny) {
            BRepBuilderAPI_MakeFace face(gp_Pln(origin, normal), outline.Wire(), true);
            BRepPrimAPI_MakePrism prism(face.Face(), gp_Vec(normal).Multiplied(spec.thickness), true);
            if (!prism.IsDone()) {
                throw GeometryError("No se pudo extruir la cremallera");
            }
            Namer namer(prism.Shape());
            namer.name(prism.FirstShape(), tag + ":inicio");
            namer.name(prism.LastShape(), tag + ":fin");
            for (const auto& [edge, name] : edges) {
                for (const TopoDS_Shape& side : prism.Generated(edge)) {
                    namer.name(side, tag + ":lado:" + name);
                }
            }
            Shape result = namer.finish(tag);
            if (!result.isValid()) {
                throw GeometryError("La cremallera no es un sólido válido");
            }
            return result;
        }

        // Con hélice los dientes van inclinados: cada sección se corre a lo largo de la barra (X del plano) tan(hélice)
        // por cada unidad de ancho, que es lo que hace el diente de una rueda helicoidal en su punto de contacto. Con
        // espina de pescado la segunda mitad deshace el corrimiento (una «V» de dientes). Se hace como el engranaje
        // helicoidal: un loft reglado por mitad, dos tapas planas, todo cosido; las caras de la segunda mitad llevan «:m2».
        const double slope = std::tan(spec.helixAngle);
        const gp_Vec along(toDir(plane.xAxis));
        const auto sectionAt = [&](double height, double shift) {
            gp_Trsf move;
            move.SetTranslation(gp_Vec(normal).Multiplied(height) + along.Multiplied(shift));
            BRepBuilderAPI_Transform moved(outline.Wire(), move, true);
            return TopoDS::Wire(moved.Shape());
        };
        struct Half {
            std::unique_ptr<BRepOffsetAPI_ThruSections> loft;
            std::vector<TopoDS_Edge> firstEdges;
        };
        std::vector<Half> halves;
        const auto makeLoft = [&](double h0, double s0, double h1, double s1, bool original) {
            Half piece;
            piece.loft = std::make_unique<BRepOffsetAPI_ThruSections>(false, true, 1e-6);
            const TopoDS_Wire first = original ? outline.Wire() : sectionAt(h0, s0);
            piece.loft->AddWire(first);
            piece.loft->AddWire(sectionAt(h1, s1));
            for (TopoDS_Iterator children(first); children.More(); children.Next()) {
                piece.firstEdges.push_back(TopoDS::Edge(children.Value()));
            }
            buildCancellable(*piece.loft);
            if (!piece.loft->IsDone()) {
                throw GeometryError("No se pudo generar la cremallera helicoidal");
            }
            halves.push_back(std::move(piece));
        };
        const double width = spec.thickness;
        TopoDS_Wire lastWire;
        if (spec.herringbone) {
            makeLoft(0.0, 0.0, width / 2.0, slope * width / 2.0, true);
            makeLoft(width / 2.0, slope * width / 2.0, width, 0.0, false);
            lastWire = sectionAt(width, 0.0);
        } else {
            makeLoft(0.0, 0.0, width, slope * width, true);
            lastWire = sectionAt(width, slope * width);
        }
        const double sign = width > 0 ? 1.0 : -1.0;
        const auto cap = [&](const gp_Pnt& where, const TopoDS_Wire& wire, bool end) {
            TopoDS_Face face = BRepBuilderAPI_MakeFace(gp_Pln(where, normal), wire, true).Face();
            if ((sign > 0) != end) {
                face.Reverse();  // el principio mira hacia atrás y el final hacia delante
            }
            return face;
        };
        const TopoDS_Face startCap = cap(origin, outline.Wire(), false);
        const TopoDS_Face endCap = cap(gp_Pnt(origin.XYZ() + normal.XYZ() * width), lastWire, true);
        BRepBuilderAPI_Sewing sewing(1e-6);
        for (const Half& each : halves) {
            for (TopExp_Explorer ex(each.loft->Shape(), TopAbs_FACE); ex.More(); ex.Next()) {
                sewing.Add(ex.Current());
            }
        }
        sewing.Add(startCap);
        sewing.Add(endCap);
        sewing.Perform();
        TopoDS_Shell shell;
        if (const TopExp_Explorer ex(sewing.SewedShape(), TopAbs_SHELL); ex.More()) {
            shell = TopoDS::Shell(ex.Current());
        }
        if (shell.IsNull()) {
            throw GeometryError("No se pudo cerrar la cremallera helicoidal");
        }
        TopoDS_Solid solid = BRepBuilderAPI_MakeSolid(shell).Solid();
        // Hacia fuera: la cara de la base (la de abajo, plana) debe mirar a -Y del plano.
        for (std::size_t index = 0; index < edges.size(); ++index) {
            if (edges[index].second != "base") {
                continue;
            }
            for (const TopoDS_Shape& side : halves.front().loft->Generated(halves.front().firstEdges[index])) {
                const TopoDS_Shape modified = sewing.Modified(side);
                const TopoDS_Face face = TopoDS::Face(modified.IsNull() ? side : modified);
                const BRepAdaptor_Surface probe(face);
                gp_Pnt onFace;
                gp_Vec du;
                gp_Vec dv;
                probe.D1(0.5 * (probe.FirstUParameter() + probe.LastUParameter()),
                         0.5 * (probe.FirstVParameter() + probe.LastVParameter()), onFace, du, dv);
                gp_Vec outward = du.Crossed(dv);
                if (face.Orientation() == TopAbs_REVERSED) {
                    outward.Reverse();
                }
                if (outward.Dot(-gp_Vec(toDir(plane.yAxis))) < 0.0) {
                    solid.Reverse();
                }
            }
            break;
        }
        Namer namer(solid);
        const auto sewnShape = [&](const TopoDS_Shape& shape) {
            const TopoDS_Shape modified = sewing.Modified(shape);
            return modified.IsNull() ? shape : modified;
        };
        namer.name(sewnShape(startCap), tag + ":inicio");
        namer.name(sewnShape(endCap), tag + ":fin");
        for (std::size_t h = 0; h < halves.size(); ++h) {
            for (std::size_t index = 0; index < edges.size(); ++index) {
                for (const TopoDS_Shape& side : halves[h].loft->Generated(halves[h].firstEdges[index])) {
                    namer.name(sewnShape(side), tag + ":lado:" + edges[index].second + (h == 0 ? "" : ":m2"));
                }
            }
        }
        Shape result = namer.finish(tag);
        if (!result.isValid()) {
            throw GeometryError("La cremallera helicoidal no es un sólido válido");
        }
        return result;
    } catch (const Standard_Failure& e) {
        throw kernelFailure("crear la cremallera", "revisa el módulo, el ángulo de presión y la base", e);
    }
}

} // namespace

namespace {

Shape gearSolid(std::string_view tagView, const sketch::Plane& plane, const GearSpec& input)
{
    const std::string tag(tagView);
    if (input.type == GearType::Rack) {
        return rackGear(tag, plane, input);
    }
    GearSpec spec = input;
    constexpr double kPi = std::numbers::pi;
    constexpr double kDeg = kPi / 180.0;
    // Cónico (E5): un diente recto de involuta en el extremo grande (con la cabeza y la raíz reducidas por el coseno del
    // ángulo del cono) y el mismo contorno reducido hacia el vértice del cono: un loft entre los dos extremos, que son
    // homotéticos respecto al vértice, así que los flancos son rectas que pasan por él (dientes cónicos rectos).
    const bool bevel = spec.type == GearType::Bevel;
    double apexHeight = 0.0;   // distancia con signo desde la cara grande hasta el vértice, a lo largo de la normal
    if (bevel) {
        if (spec.mateTeeth < 6 || spec.mateTeeth > 300) {
            throw GeometryError("Los dientes de la rueda pareja deben estar entre 6 y 300");
        }
        if (std::abs(spec.helixAngle) > kTiny || spec.herringbone) {
            throw GeometryError("Los engranajes cónicos se hacen con dientes rectos");
        }
        const double coneAngle = std::atan2(double(spec.teeth), double(spec.mateTeeth));  // eje a 90°
        const double coneDistance = spec.module * std::hypot(double(spec.teeth), double(spec.mateTeeth)) / 2.0;
        const double face = std::abs(spec.thickness);
        if (face <= kTiny) {
            throw GeometryError("El ancho de cara del engranaje cónico debe ser distinto de cero");
        }
        if (face > 0.5 * coneDistance) {
            throw GeometryError("El ancho de cara es demasiado grande: no debe pasar de un tercio de la distancia al vértice del cono (" +
                                std::to_string(coneDistance / 3.0) + " mm aquí); a partir de la mitad el diente se vuelve puntiagudo");
        }
        const double sign = spec.thickness > 0.0 ? 1.0 : -1.0;
        apexHeight = sign * coneDistance * std::cos(coneAngle);
        spec.heightScale = std::cos(coneAngle);
        spec.thickness = sign * face * std::cos(coneAngle);  // largo axial
        spec.type = GearType::External;
    }
    const bool internal = spec.type == GearType::Internal;
    if (spec.teeth < 6 || spec.teeth > 300) {
        throw GeometryError("El número de dientes debe estar entre 6 y 300");
    }
    if (spec.module <= kTiny) {
        throw GeometryError("El módulo debe ser mayor que cero");
    }
    if (std::abs(spec.thickness) <= kTiny) {
        throw GeometryError("El ancho del engranaje debe ser distinto de cero");
    }
    if (spec.pressureAngle < 5.0 * kDeg || spec.pressureAngle > 35.0 * kDeg) {
        throw GeometryError("El ángulo de presión debe estar entre 5° y 35°");
    }
    if (std::abs(spec.helixAngle) > 45.0 * kDeg) {
        throw GeometryError("El ángulo de hélice no puede pasar de 45°");
    }
    const double z = spec.teeth;
    const double rp = spec.module * z / 2.0;
    const double rb = rp * std::cos(spec.pressureAngle);
    // Interno: lo que se dibuja es el hueco (con forma de rueda), así que cabeza y raíz cambian de lado.
    const double shift = internal ? 0.0 : spec.profileShift;
    if (std::abs(spec.profileShift) > 1.0) {
        throw GeometryError("La corrección de perfil debe estar entre -1 y 1 módulos");
    }
    if (internal && std::abs(spec.profileShift) > kTiny) {
        throw GeometryError("El engranaje interno no admite corrección de perfil");
    }
    const double ra = rp + ((internal ? 1.25 : 1.0) + shift) * spec.module * spec.heightScale;
    const double rf = rp - ((internal ? 1.0 : 1.25) - shift) * spec.module * spec.heightScale;
    if (spec.herringbone && std::abs(spec.helixAngle) < kTiny) {
        throw GeometryError("La espina de pescado necesita un ángulo de hélice");
    }
    if (internal && spec.rim <= kTiny) {
        throw GeometryError("El grosor del aro del engranaje interno debe ser mayor que cero");
    }
    if (rf <= kTiny) {
        throw GeometryError("Hay muy pocos dientes para ese módulo");
    }
    const auto inv = [](double a) { return std::tan(a) - a; };
    // Mitad del ancho angular del diente a un radio r (la evolvente).
    if (spec.backlash < 0.0) {
        throw GeometryError("El juego entre dientes no puede ser negativo");
    }
    // El juego adelgaza el diente (medio juego por lado, medido sobre la primitiva): un giro constante a cualquier
    // radio. En el interno lo que se dibuja es el hueco, y el diente de la corona se adelgaza ensanchándolo.
    // La corrección de perfil engruesa el diente sobre la primitiva: 2·x·tan(α) de arco por módulo.
    const double slack = (internal ? -1.0 : 1.0) * spec.backlash / (2.0 * rp) - 2.0 * shift * std::tan(spec.pressureAngle) / z;
    const auto half = [&](double r) {
        return kPi / (2.0 * z) - slack - (inv(std::acos(std::min(1.0, rb / r))) - inv(spec.pressureAngle));
    };
    const double r0 = std::max(rb, rf);  // dónde empieza la evolvente
    const double thetaS = half(r0);
    const double thetaA = half(ra);
    const double pitch = 2.0 * kPi / z;
    if (thetaA <= 0.02 * pitch) {
        throw GeometryError("Los dientes salen puntiagudos: prueba con menos ángulo de presión o más dientes");
    }
    if (pitch - 2.0 * thetaS <= 0.02 * pitch) {
        throw GeometryError("Los dientes se solapan en la raíz");
    }
    if (!internal && (spec.bore < 0.0 || spec.bore / 2.0 >= 0.9 * rf)) {
        throw GeometryError("El agujero central es demasiado grande para el engranaje");
    }

    try {
        const gp_Pnt origin = toPnt(plane.origin);
        const gp_Dir normal = toDir(plane.normal);
        const gp_Ax2 frame(origin, normal, toDir(plane.xAxis));
        const auto at = [&](double r, double phi) {
            return toPnt(plane.toWorld({r * std::cos(phi), r * std::sin(phi)}));
        };
        const auto spline = [&](const std::vector<gp_Pnt>& points) {
            occ::handle<HPntArray> array = new HPntArray(1, int(points.size()));
            for (std::size_t i = 0; i < points.size(); ++i) {
                array->SetValue(int(i) + 1, points[i]);
            }
            GeomAPI_Interpolate interpolation(array, false, 1e-7);
            interpolation.Perform();
            if (!interpolation.IsDone()) {
                throw GeometryError("No se pudo trazar el flanco del diente");
            }
            return BRepBuilderAPI_MakeEdge(interpolation.Curve()).Edge();
        };
        constexpr int kSamples = 8;

        std::vector<std::pair<TopoDS_Edge, std::string>> edges;
        for (int i = 0; i < spec.teeth; ++i) {
            const double phi = i * pitch;
            const std::string name = "d" + std::to_string(i + 1) + ":";
            const bool radial = rb > rf + kTiny;
            if (radial) {
                edges.emplace_back(BRepBuilderAPI_MakeEdge(at(rf, phi - thetaS), at(rb, phi - thetaS)).Edge(), name + "rad_d");
            }
            std::vector<gp_Pnt> right;
            std::vector<gp_Pnt> left;
            for (int j = 0; j <= kSamples; ++j) {
                const double r = r0 + (ra - r0) * j / kSamples;
                right.push_back(at(r, phi - half(r)));
                left.push_back(at(r, phi + half(r)));
            }
            edges.emplace_back(spline(right), name + "flanco_d");
            edges.emplace_back(BRepBuilderAPI_MakeEdge(gp_Circ(frame, ra), phi - thetaA, phi + thetaA).Edge(), name + "cabeza");
            std::ranges::reverse(left);
            edges.emplace_back(spline(left), name + "flanco_i");
            if (radial) {
                edges.emplace_back(BRepBuilderAPI_MakeEdge(at(rb, phi + thetaS), at(rf, phi + thetaS)).Edge(), name + "rad_i");
            }
            edges.emplace_back(BRepBuilderAPI_MakeEdge(gp_Circ(frame, rf), phi + thetaS, phi + pitch - thetaS).Edge(), name + "raiz");
        }
        BRepBuilderAPI_MakeWire outline;
        for (const auto& [edge, name] : edges) {
            outline.Add(edge);
            if (!outline.IsDone()) {
                throw GeometryError("No se pudo cerrar el contorno del engranaje");
            }
        }
        // El constructor de caminos puede sustituir las aristas por copias (al unir vértices):
        // los nombres van con las que quedaron en el contorno, en el mismo orden.
        {
            TopoDS_Iterator children(outline.Wire());
            for (auto& [edge, name] : edges) {
                if (!children.More()) {
                    throw GeometryError("El contorno del engranaje quedó incompleto");
                }
                edge = TopoDS::Edge(children.Value());
                children.Next();
            }
        }
        const double twist = spec.thickness * std::tan(spec.helixAngle) / rp;  // giro total de la hélice
        const gp_Vec along = gp_Vec(normal).Multiplied(spec.thickness);

        if (!bevel && std::abs(twist) < 1e-9) {
            TopoDS_Edge outerEdge;
            if (internal) {
                outerEdge = BRepBuilderAPI_MakeEdge(gp_Circ(frame, ra + spec.rim)).Edge();
            }
            BRepBuilderAPI_MakeFace face = internal
                ? BRepBuilderAPI_MakeFace(gp_Pln(origin, normal), BRepBuilderAPI_MakeWire(outerEdge).Wire(), true)
                : BRepBuilderAPI_MakeFace(gp_Pln(origin, normal), outline.Wire(), true);
            if (internal) {
                face.Add(TopoDS::Wire(outline.Wire().Reversed()));
            }
            TopoDS_Edge boreEdge;
            if (!internal && spec.bore > 0.0) {
                boreEdge = BRepBuilderAPI_MakeEdge(gp_Circ(frame, spec.bore / 2.0)).Edge();
                face.Add(TopoDS::Wire(BRepBuilderAPI_MakeWire(boreEdge).Wire().Reversed()));
            }
            BRepPrimAPI_MakePrism prism(face.Face(), along, true);
            if (!prism.IsDone()) {
                throw GeometryError("No se pudo extruir el engranaje");
            }
            Namer namer(prism.Shape());
            namer.name(prism.FirstShape(), tag + ":inicio");
            namer.name(prism.LastShape(), tag + ":fin");
            for (const auto& [edge, name] : edges) {
                for (const TopoDS_Shape& side : prism.Generated(edge)) {
                    namer.name(side, tag + ":lado:" + name);
                }
            }
            if (!boreEdge.IsNull()) {
                for (const TopoDS_Shape& side : prism.Generated(boreEdge)) {
                    namer.name(side, tag + ":lado:agujero");
                }
            }
            if (!outerEdge.IsNull()) {
                for (const TopoDS_Shape& side : prism.Generated(outerEdge)) {
                    namer.name(side, tag + ":lado:exterior");
                }
            }
            Shape result = namer.finish(tag);
            if (!result.isValid()) {
                throw GeometryError("El engranaje no es un sólido válido");
            }
            return result;
        }

        // Helicoidal: secciones giradas a lo largo del ancho, unidas en una superficie lateral. Las tapas
        // (con el agujero) y la pared del agujero se hacen aparte y se cosen: así no hace falta ninguna
        // booleana contra la superficie lateral (que era casi la mitad del tiempo).
        // Espina de pescado (E1): dos mitades con la hélice contraria (la primera gira hasta la mitad del ancho y la
        // segunda deshace el giro); se hacen como dos lofts que comparten el contorno de la mitad y se cosen juntos.
        const bool herringbone = spec.herringbone;
        const int sections = bevel ? 1 : std::clamp(int(std::ceil(std::abs(herringbone ? twist / 2.0 : twist) / (15.0 * kDeg))) + 1, 3, 24);
        const auto sectionWire = [&](double height, double angle) {
            gp_Trsf turn;
            turn.SetRotation(gp_Ax1(origin, normal), angle);
            gp_Trsf slide;
            if (bevel) {
                // Homotecia respecto al vértice del cono: lleva la cara grande al plano de esa altura.
                const gp_Pnt apex(origin.XYZ() + normal.XYZ() * apexHeight);
                slide.SetScale(apex, (apexHeight - height) / apexHeight);
            } else {
                slide.SetTranslation(gp_Vec(normal).Multiplied(height));
            }
            BRepBuilderAPI_Transform moved(outline.Wire(), slide * turn, true);
            return TopoDS::Wire(moved.Shape());
        };
        // Cada loft guarda las aristas de su primera sección, en el mismo orden que «edges», para nombrar sus caras.
        struct Half {
            std::unique_ptr<BRepOffsetAPI_ThruSections> loft;
            std::vector<TopoDS_Edge> firstEdges;
        };
        std::vector<Half> halves;
        const auto makeLoft = [&](double z0, double z1, double a0, double a1, bool original) {
            Half piece;
            piece.loft = std::make_unique<BRepOffsetAPI_ThruSections>(false, false, 1e-6);
            TopoDS_Wire first;
            for (int k = 0; k <= sections; ++k) {
                throwIfCancelled();
                const double f = double(k) / sections;
                if (k == 0) {
                    first = original ? outline.Wire() : sectionWire(z0, a0);
                    piece.loft->AddWire(first);
                    continue;
                }
                piece.loft->AddWire(sectionWire(z0 + (z1 - z0) * f, a0 + (a1 - a0) * f));
            }
            for (TopoDS_Iterator children(first); children.More(); children.Next()) {
                piece.firstEdges.push_back(TopoDS::Edge(children.Value()));
            }
            buildCancellable(*piece.loft);
            if (!piece.loft->IsDone()) {
                throw GeometryError("No se pudo generar el engranaje helicoidal");
            }
            halves.push_back(std::move(piece));
        };
        TopoDS_Wire lastWire;
        if (herringbone) {
            makeLoft(0.0, spec.thickness / 2.0, 0.0, twist / 2.0, true);
            makeLoft(spec.thickness / 2.0, spec.thickness, twist / 2.0, 0.0, false);
            lastWire = sectionWire(spec.thickness, 0.0);
        } else {
            makeLoft(0.0, spec.thickness, 0.0, twist, true);
            lastWire = sectionWire(spec.thickness, twist);
        }
        const double sign = spec.thickness > 0 ? 1.0 : -1.0;
        const gp_Pnt endPoint(origin.XYZ() + normal.XYZ() * spec.thickness);
        // Tapa: cara plana con el contorno y, si hay agujero, su círculo; hacia fuera del sólido. En el interno el contorno
        // es el hueco de la corona y el círculo exterior es el del aro.
        const auto cap = [&](const gp_Pnt& where, const TopoDS_Wire& wire, bool end) {
            const auto outerWire = [&] {
                return BRepBuilderAPI_MakeWire(BRepBuilderAPI_MakeEdge(gp_Circ(gp_Ax2(where, normal, toDir(plane.xAxis)), ra + spec.rim)).Edge()).Wire();
            };
            BRepBuilderAPI_MakeFace face = internal ? BRepBuilderAPI_MakeFace(gp_Pln(where, normal), outerWire(), true)
                                                    : BRepBuilderAPI_MakeFace(gp_Pln(where, normal), wire, true);
            if (internal) {
                face.Add(TopoDS::Wire(wire.Reversed()));
            }
            if (!internal && spec.bore > 0.0) {
                const TopoDS_Edge ring = BRepBuilderAPI_MakeEdge(gp_Circ(gp_Ax2(where, normal, toDir(plane.xAxis)), spec.bore / 2.0)).Edge();
                face.Add(TopoDS::Wire(BRepBuilderAPI_MakeWire(ring).Wire().Reversed()));
            }
            TopoDS_Face result = face.Face();
            if ((sign > 0) != end) {
                result.Reverse();  // el principio mira hacia atrás y el final hacia delante
            }
            return result;
        };
        const TopoDS_Face startCap = cap(origin, outline.Wire(), false);
        const TopoDS_Face endCap = cap(endPoint, lastWire, true);
        TopoDS_Face outerWall;
        if (internal) {
            const gp_Pnt base(spec.thickness > 0 ? origin : endPoint);
            BRepPrimAPI_MakeCylinder ring(gp_Ax2(base, normal, toDir(plane.xAxis)), ra + spec.rim, std::abs(spec.thickness));
            for (TopExp_Explorer ex(ring.Shape(), TopAbs_FACE); ex.More(); ex.Next()) {
                if (BRepAdaptor_Surface(TopoDS::Face(ex.Current())).GetType() == GeomAbs_Cylinder) {
                    outerWall = TopoDS::Face(ex.Current());
                }
            }
        }
        TopoDS_Face boreWall;
        if (!internal && spec.bore > 0.0) {
            const gp_Pnt base(spec.thickness > 0 ? origin : endPoint);
            BRepPrimAPI_MakeCylinder bore(gp_Ax2(base, normal, toDir(plane.xAxis)), spec.bore / 2.0, std::abs(spec.thickness));
            for (TopExp_Explorer ex(bore.Shape(), TopAbs_FACE); ex.More(); ex.Next()) {
                if (BRepAdaptor_Surface(TopoDS::Face(ex.Current())).GetType() == GeomAbs_Cylinder) {
                    boreWall = TopoDS::Face(ex.Current());
                    boreWall.Reverse();  // la pared del agujero mira hacia el eje
                }
            }
        }
        BRepBuilderAPI_Sewing sewing(1e-6);
        for (const Half& each : halves) {
            for (TopExp_Explorer ex(each.loft->Shape(), TopAbs_FACE); ex.More(); ex.Next()) {
                sewing.Add(ex.Current());
            }
        }
        sewing.Add(startCap);
        sewing.Add(endCap);
        if (!boreWall.IsNull()) {
            sewing.Add(boreWall);
        }
        if (!outerWall.IsNull()) {
            sewing.Add(outerWall);
        }
        sewing.Perform();
        TopoDS_Shell shell;
        if (const TopExp_Explorer ex(sewing.SewedShape(), TopAbs_SHELL); ex.More()) {
            shell = TopoDS::Shell(ex.Current());
        }
        if (shell.IsNull()) {
            throw GeometryError("No se pudo cerrar el engranaje helicoidal");
        }
        TopoDS_Solid solid = BRepBuilderAPI_MakeSolid(shell).Solid();
        // La costura deja las caras coherentes entre sí pero no siempre hacia fuera. Se mira una cara
        // de la cabeza de un diente (su normal debe apuntar lejos del eje): mucho más barato que clasificar
        // puntos o integrar el volumen contra las superficies.
        for (std::size_t index = 0; index < edges.size(); ++index) {
            const std::string& name = edges[index].second;
            if (name.ends_with(":cabeza")) {
                for (const TopoDS_Shape& side : halves.front().loft->Generated(halves.front().firstEdges[index])) {
                    const TopoDS_Shape modified = sewing.Modified(side);
                    const TopoDS_Face face = TopoDS::Face(modified.IsNull() ? side : modified);
                    const BRepAdaptor_Surface probe(face);
                    gp_Pnt tip;
                    gp_Vec du;
                    gp_Vec dv;
                    probe.D1(0.5 * (probe.FirstUParameter() + probe.LastUParameter()),
                             0.5 * (probe.FirstVParameter() + probe.LastVParameter()), tip, du, dv);
                    gp_Vec outward = du.Crossed(dv);
                    if (face.Orientation() == TopAbs_REVERSED) {
                        outward.Reverse();
                    }
                    const gp_Vec radial = gp_Vec(origin, tip) - gp_Vec(normal) * gp_Vec(origin, tip).Dot(gp_Vec(normal));
                    // Los dientes de una corona son un hueco: su normal hacia fuera del sólido mira al eje.
                    if ((internal ? -1.0 : 1.0) * outward.Dot(radial) < 0.0) {
                        solid.Reverse();
                    }
                }
                break;
            }
        }
        Namer namer(solid);
        const auto sewnShape = [&](const TopoDS_Shape& shape) {
            const TopoDS_Shape modified = sewing.Modified(shape);
            return modified.IsNull() ? shape : modified;
        };
        namer.name(sewnShape(startCap), tag + ":inicio");
        namer.name(sewnShape(endCap), tag + ":fin");
        if (!boreWall.IsNull()) {
            namer.name(sewnShape(boreWall), tag + ":lado:agujero");
        }
        if (!outerWall.IsNull()) {
            namer.name(sewnShape(outerWall), tag + ":lado:exterior");
        }
        for (std::size_t h = 0; h < halves.size(); ++h) {
            for (std::size_t index = 0; index < edges.size(); ++index) {
                for (const TopoDS_Shape& side : halves[h].loft->Generated(halves[h].firstEdges[index])) {
                    // La segunda mitad de una espina de pescado lleva «:m2» al final del nombre.
                    namer.name(sewnShape(side), tag + ":lado:" + edges[index].second + (h == 0 ? "" : ":m2"));
                }
            }
        }
        Shape result = namer.finish(tag);
        if (!result.isValid()) {
            throw GeometryError("El engranaje helicoidal no es un sólido válido");
        }
        return result;
    } catch (const Standard_Failure& e) {
        throw kernelFailure("generar el engranaje", "prueba con otro módulo, otro número de dientes o menos ángulo de presión y de hélice", e);
    }
}

// Chaflán en el contorno de la cara inicial (la primera capa al imprimir), sin tocar el agujero central.
Shape chamferStart(const std::string& tag, const Shape& body, double distance)
{
    const Shape::Impl& in = *body.impl();
    int start = -1;
    for (int i = 0; i < body.faceCount(); ++i) {
        if (body.faceName(i) == tag + ":inicio") {
            start = i;
            break;
        }
    }
    if (start < 0) {
        throw GeometryError("No se encontró la cara inicial del engranaje");
    }
    NCollection_IndexedDataMap<TopoDS_Shape, NCollection_List<TopoDS_Shape>, TopTools_ShapeMapHasher> ancestors;
    TopExp::MapShapesAndAncestors(in.shape, TopAbs_EDGE, TopAbs_FACE, ancestors);
    std::vector<int> edges;
    for (TopExp_Explorer ex(in.faces(start + 1), TopAbs_EDGE); ex.More(); ex.Next()) {
        const int index = in.edges.FindIndex(ex.Current());
        if (index <= 0) {
            continue;
        }
        bool bore = false;
        for (const TopoDS_Shape& face : ancestors.FindFromKey(ex.Current())) {
            const int faceIndex = in.faces.FindIndex(face);
            if (faceIndex > 0 && in.faceNames[std::size_t(faceIndex - 1)].ends_with(":lado:agujero")) {
                bore = true;
            }
        }
        if (!bore && std::ranges::find(edges, index - 1) == edges.end()) {
            edges.push_back(index - 1);
        }
    }
    // El kernel falla de forma irregular con algunas medidas: con 24 dientes y módulo 2 admite hasta 0,20 y falla de 0,21
    // a 0,30; con 30 dientes falla de 0,13 a 0,23 pero admite 0,25; con 40 falla de 0,07 a 0,11. No es un tope de tamaño
    // sino de la geometría de la raíz. Se reintenta con medidas apenas distintas (hasta un 6 % menos o más) y, si ninguna
    // sirve, se avisa de que otra medida cercana puede funcionar.
    for (const double factor : {1.0, 0.98, 1.02, 0.96, 1.04, 0.94, 1.06}) {
        try {
            return chamfer(tag, body, edges, distance * factor);
        } catch (const GeometryError&) {
            // siguiente medida
        }
    }
    throw GeometryError("No se pudo achaflanar la base del engranaje con esa medida: prueba con otra cercana (a veces una algo mayor o menor sí funciona)");
}

} // namespace

double recommendedProfileShift(int teeth, double pressureAngle)
{
    const double s = std::sin(pressureAngle);
    const double zMin = s > 1e-6 ? 2.0 / (s * s) : 1e9;
    if (teeth >= zMin || teeth < 2) {
        return 0.0;
    }
    const double wanted = std::min(1.0, (zMin - teeth) / zMin);
    // La corrección engruesa el diente pero también alarga la cabeza, y con pocos dientes la punta se afila: se queda
    // la mayor corrección (hasta la que quita el socavado) que deja una punta de al menos 0,2 módulos.
    const auto inv = [](double a) { return std::tan(a) - a; };
    const double z = teeth;
    const double rb = z / 2.0 * std::cos(pressureAngle);
    double best = 0.0;
    for (double x = 0.0; x <= wanted + 1e-9; x += 0.01) {
        const double ra = z / 2.0 + 1.0 + x;
        const double half = (std::numbers::pi / 2.0 + 2.0 * x * std::tan(pressureAngle)) / z -
                            (inv(std::acos(std::min(1.0, rb / ra))) - inv(pressureAngle));
        if (half * ra >= 0.2) {
            best = x;
        }
    }
    return best;
}

Shape gear(std::string_view tagView, const sketch::Plane& plane, const GearSpec& spec)
{
    if (spec.type == GearType::Worm) {
        WormSpec w;
        w.module = spec.module;
        w.starts = spec.teeth;
        w.pitchDiameter = spec.wormDiameter;
        w.length = spec.thickness;
        w.pressureAngle = spec.pressureAngle;
        w.leftHanded = spec.leftHanded;
        return worm(tagView, plane, w);
    }
    if (spec.baseChamfer < 0.0) {
        throw GeometryError("El chaflán de la base no puede ser negativo");
    }
    if (spec.baseChamfer > kTiny && std::abs(spec.helixAngle) > kTiny) {
        // Las caras laterales de la hélice son superficies libres: el kernel no las achaflanó en la prueba (0,15 mm).
        throw GeometryError("El chaflán de la base solo se hace con dientes rectos (sin ángulo de hélice)");
    }
    if (spec.herringboneGroove < 0.0 || (spec.herringboneGroove > kTiny && !spec.herringbone)) {
        throw GeometryError("El surco central solo existe en la espina de pescado, y no puede ser negativo");
    }
    Shape solid = gearSolid(tagView, plane, spec);
    if (spec.herringboneGroove > kTiny) {
        const double width = spec.herringboneGroove;
        if (width >= 0.5 * std::abs(spec.thickness)) {
            throw GeometryError("El surco central es demasiado ancho para el ancho del engranaje");
        }
        // Una corona (de la altura del surco, a media altura) desde la raíz de los dientes hacia fuera.
        const double rp = spec.module * spec.teeth / 2.0;
        const double outer = rp + (1.0 + spec.profileShift) * spec.module + 1.0;
        const double root = rp - (1.25 - spec.profileShift) * spec.module;
        sketch::Plane mid = plane;
        const double sign = spec.thickness > 0.0 ? 1.0 : -1.0;
        mid.origin = {plane.origin.x + plane.normal.x * (spec.thickness / 2.0 - sign * width / 2.0),
                      plane.origin.y + plane.normal.y * (spec.thickness / 2.0 - sign * width / 2.0),
                      plane.origin.z + plane.normal.z * (spec.thickness / 2.0 - sign * width / 2.0)};
        const Shape disc = cylinder("surco", mid, 2.0 * outer, sign * width);
        const Shape core = cylinder("nucleo", mid, 2.0 * root, sign * width);
        solid = boolean(BooleanOp::Cut, solid, boolean(BooleanOp::Cut, disc, core));
    }
    if (spec.baseChamfer <= kTiny) {
        return solid;
    }
    if (spec.baseChamfer >= 0.5 * std::abs(spec.thickness) || spec.baseChamfer >= spec.module) {
        throw GeometryError("El chaflán de la base es demasiado grande para el módulo o el ancho del engranaje");
    }
    return chamferStart(std::string(tagView), solid, spec.baseChamfer);
}

Shape box(std::string_view tagView, const sketch::Plane& plane, double width, double depth, double height, bool centered,
          double cornerRadius)
{
    const std::string tag(tagView);
    if (!(width > kTiny) || !(depth > kTiny) || std::abs(height) <= kTiny) {
        throw GeometryError("Las medidas de la caja deben ser mayores que cero");
    }
    try {
        const gp_Dir normal = toDir(plane.normal);
        const gp_Dir xAxis = toDir(plane.xAxis);
        const gp_Dir yAxis = normal.Crossed(xAxis);
        gp_Pnt corner = toPnt(plane.origin);
        if (centered) {
            corner.Translate(gp_Vec(xAxis) * (-width / 2.0) + gp_Vec(yAxis) * (-depth / 2.0));
        }
        if (cornerRadius > kTiny) {
            // Rectángulo con las esquinas redondeadas, extruido a lo largo de la normal.
            const double r = std::min({cornerRadius, width / 2.0 - kTiny, depth / 2.0 - kTiny});
            const gp_Ax2 frame(corner, normal, xAxis);
            const auto at = [&](double x, double y) { return corner.Translated(gp_Vec(xAxis) * x + gp_Vec(yAxis) * y); };
            const auto arc = [&](double cx, double cy, double from) {
                return BRepBuilderAPI_MakeEdge(gp_Circ(gp_Ax2(at(cx, cy), normal, xAxis), r), from, from + std::numbers::pi / 2.0)
                    .Edge();
            };
            const auto line = [&](gp_Pnt a, gp_Pnt b) { return BRepBuilderAPI_MakeEdge(a, b).Edge(); };
            constexpr double kPi = std::numbers::pi;
            std::vector<std::pair<TopoDS_Edge, std::string>> edges = {
                {line(at(r, 0), at(width - r, 0)), ":cara:-y"},
                {arc(width - r, r, 3.0 * kPi / 2.0), ":cara:esquina+x-y"},
                {line(at(width, r), at(width, depth - r)), ":cara:+x"},
                {arc(width - r, depth - r, 0.0), ":cara:esquina+x+y"},
                {line(at(width - r, depth), at(r, depth)), ":cara:+y"},
                {arc(r, depth - r, kPi / 2.0), ":cara:esquina-x+y"},
                {line(at(0, depth - r), at(0, r)), ":cara:-x"},
                {arc(r, r, kPi), ":cara:esquina-x-y"}};
            BRepBuilderAPI_MakeWire outline;
            for (const auto& [edge, name] : edges) {
                outline.Add(edge);
            }
            if (!outline.IsDone()) {
                throw GeometryError("No se pudo cerrar el contorno de la caja");
            }
            // El constructor de caminos puede sustituir las aristas por copias: se relee el contorno en orden.
            {
                TopoDS_Iterator children(outline.Wire());
                for (auto& [edge, name] : edges) {
                    if (!children.More()) {
                        throw GeometryError("El contorno de la caja quedó incompleto");
                    }
                    edge = TopoDS::Edge(children.Value());
                    children.Next();
                }
            }
            const BRepBuilderAPI_MakeFace face(gp_Pln(corner, normal), outline.Wire(), true);
            BRepPrimAPI_MakePrism prism(face.Face(), gp_Vec(normal) * height, true);
            if (!prism.IsDone()) {
                throw GeometryError("No se pudo generar la caja");
            }
            Namer namer(prism.Shape());
            namer.name(prism.FirstShape(), tag + (height > 0.0 ? ":cara:-z" : ":cara:+z"));
            namer.name(prism.LastShape(), tag + (height > 0.0 ? ":cara:+z" : ":cara:-z"));
            for (const auto& [edge, name] : edges) {
                for (const TopoDS_Shape& side : prism.Generated(edge)) {
                    namer.name(side, tag + name);
                }
            }
            return namer.finish(tag);
        }
        if (height < 0.0) {
            corner.Translate(gp_Vec(normal) * height);
        }
        BRepPrimAPI_MakeBox maker(gp_Ax2(corner, normal, xAxis), width, depth, std::abs(height));
        Namer namer(maker.Shape());
        namer.name(maker.LeftFace(), tag + ":cara:-y");
        namer.name(maker.RightFace(), tag + ":cara:+y");
        namer.name(maker.FrontFace(), tag + ":cara:+x");
        namer.name(maker.BackFace(), tag + ":cara:-x");
        namer.name(maker.BottomFace(), tag + ":cara:-z");
        namer.name(maker.TopFace(), tag + ":cara:+z");
        return namer.finish(tag);
    } catch (const Standard_Failure& e) {
        throw kernelFailure("generar la caja", "revisa que el radio de esquinas no sea mayor que la mitad del lado más corto", e);
    }
}

Shape cylinder(std::string_view tagView, const sketch::Plane& plane, double diameter, double height)
{
    const std::string tag(tagView);
    if (!(diameter > kTiny) || std::abs(height) <= kTiny) {
        throw GeometryError("Las medidas del cilindro deben ser mayores que cero");
    }
    try {
        const gp_Dir normal = toDir(plane.normal);
        gp_Pnt base = toPnt(plane.origin);
        if (height < 0.0) {
            base.Translate(gp_Vec(normal) * height);
        }
        BRepPrimAPI_MakeCylinder maker(gp_Ax2(base, normal, toDir(plane.xAxis)), diameter / 2.0, std::abs(height));
        Namer namer(maker.Shape());
        // Las caras se reconocen por su forma y hacia dónde miran.
        for (TopExp_Explorer ex(maker.Shape(), TopAbs_FACE); ex.More(); ex.Next()) {
            const TopoDS_Face& face = TopoDS::Face(ex.Current());
            const BRepAdaptor_Surface surface(face);
            if (surface.GetType() == GeomAbs_Plane) {
                gp_Dir outward = surface.Plane().Axis().Direction();
                if (face.Orientation() == TopAbs_REVERSED) {
                    outward.Reverse();
                }
                namer.name(face, tag + (outward.Dot(normal) > 0.0 ? ":cara:+z" : ":cara:-z"));
            } else {
                namer.name(face, tag + ":cara:lado");
            }
        }
        return namer.finish(tag);
    } catch (const Standard_Failure& e) {
        throw kernelFailure("generar el cilindro", "revisa que el diámetro y el alto sean mayores que cero", e);
    }
}

double threadMinorDiameter(const ThreadSpec& spec)
{
    return spec.diameter - 2.0 * 0.6134 * spec.pitch;
}

// Varilla cuya sección es una curva r(φ) que gira al avanzar (un tornillo de una o más entradas): a cada altura la
// misma curva girada, así que la hélice es UNA superficie por tramos más dos tapas. «radiusAt(s)» da el radio de la
// superficie a la distancia axial s (periódica); «lead» es el avance por vuelta; «what» y «wall» solo dan nombre
// a los mensajes y a la cara lateral.
static Shape twistedRod(const std::string& tag, const sketch::Plane& plane, const std::function<double(double)>& radiusAt,
                 double lead, double length, bool leftHanded, int kPoints, int sectionsPerTurn, double smooth, const std::string& wall,
                 const char* what, const char* hint)
{
    constexpr double kPi = std::numbers::pi;
    try {
        const gp_Pnt origin = toPnt(plane.origin);
        const gp_Dir normal = toDir(plane.normal);
        // Corte transversal: r(φ) = perfil(−paso·φ/2π), una curva cerrada y periódica; a cada altura es
        // la misma curva girada, así que la rosca es UNA superficie (lineal a lo largo del eje, por
        // secciones giradas) más dos tapas: 3 caras en vez de cientos, que es lo que hace rápido todo lo
        // que viene después (comprobar, mallar, booleanas).
                const double hand = leftHanded ? -1.0 : 1.0;
        occ::handle<HPntArray> array = new HPntArray(1, kPoints);
        for (int i = 0; i < kPoints; ++i) {
            const double phi = 2.0 * kPi * i / kPoints;
            // Las esquinas del perfil se suavizan (promedio en una ventana de ±paso/24): la curva por
            // puntos no se pasa de largo en las esquinas (sin ello el diámetro salía hasta un 11 % mayor)
            // y se parece más a una rosca real, con cresta y fondo redondeados.
            double r = 0.0;
            constexpr int kWindow = 9;
            for (int w = 0; w < kWindow; ++w) {
                const double offset = (double(w) / (kWindow - 1) - 0.5) * (smooth);
                r += radiusAt(-hand * lead * phi / (2.0 * kPi) + offset);
            }
            r /= kWindow;
            array->SetValue(i + 1, toPnt(plane.toWorld({r * std::cos(phi), r * std::sin(phi)})));
        }
        GeomAPI_Interpolate interpolation(array, true, 1e-7);
        interpolation.Perform();
        if (!interpolation.IsDone()) {
            throw GeometryError(std::string("No se pudo trazar el perfil de ") + what);
        }
        const occ::handle<Geom_BSplineCurve> curve = interpolation.Curve();

        const double turns = std::abs(length) / lead;
        // Una sección cada 45° (8 por vuelta), unidas por una curva cúbica en cada polo de la sección:
        // la superficie es suave (C2) en toda la hélice, que es lo que necesitan las booleanas (con
        // aristas vivas entre secciones no encontraban la intersección con los planos del bloque).
        const int sections = std::clamp(int(std::ceil(turns * sectionsPerTurn)), 6, 1600);
        const int nPoles = curve->NbPoles();
        std::vector<gp_Trsf> moves;
        for (int k = 0; k <= sections; ++k) {
            const double t = double(k) / sections;
            gp_Trsf turn;
            turn.SetRotation(gp_Ax1(origin, normal), hand * 2.0 * kPi * turns * t * (length < 0 ? -1.0 : 1.0));
            gp_Trsf slide;
            slide.SetTranslation(gp_Vec(normal).Multiplied(length * t));
            moves.push_back(slide * turn);
        }
        // Una cara por cada vuelta y media: el mallado (lo más caro de dibujar la rosca) crece más que
        // linealmente con el tamaño de cada cara (una sola de 8 vueltas: 17 s; en tramos de 1,5 vueltas: 3 s).
        const int chunks = std::max(1, int(std::ceil(turns / 1.5)));
        std::vector<TopoDS_Face> sides;
        for (int c = 0; c < chunks; ++c) {
            throwIfCancelled();
            const int k0 = int(std::lround(double(c) * sections / chunks));
            const int k1 = int(std::lround(double(c + 1) * sections / chunks));
            const int count = k1 - k0;
            occ::handle<HRealArray> parameters = new HRealArray(1, count + 1);
            for (int k = 0; k <= count; ++k) {
                parameters->SetValue(k + 1, double(k) / count);
            }
            std::vector<occ::handle<Geom_BSplineCurve>> helices;
            for (int j = 1; j <= nPoles; ++j) {
                throwIfCancelled();
                occ::handle<HPntArray> path = new HPntArray(1, count + 1);
                for (int k = 0; k <= count; ++k) {
                    path->SetValue(k + 1, curve->Pole(j).Transformed(moves[std::size_t(k0 + k)]));
                }
                GeomAPI_Interpolate along(path, parameters, false, 1e-9);
                along.Perform();
                if (!along.IsDone()) {
                    throw GeometryError(std::string("No se pudo trazar la hélice de ") + what);
                }
                helices.push_back(along.Curve());
            }
            const occ::handle<Geom_BSplineCurve>& first = helices.front();
            const int nU = first->NbPoles();
            NCollection_Array2<gp_Pnt> poles(1, nU, 1, nPoles);
            for (int j = 1; j <= nPoles; ++j) {
                for (int i = 1; i <= nU; ++i) {
                    poles.SetValue(i, j, helices[std::size_t(j - 1)]->Pole(i));
                }
            }
            const occ::handle<Geom_BSplineSurface> surface = new Geom_BSplineSurface(
                poles, first->Knots(), curve->Knots(), first->Multiplicities(), curve->Multiplicities(), first->Degree(),
                curve->Degree(), false, curve->IsPeriodic());
            sides.push_back(BRepBuilderAPI_MakeFace(surface, 1e-6).Face());
        }
        const TopoDS_Face& side = sides.front();

        // Las tapas: el plano de la primera y de la última sección, con el borde de la primera y de la
        // última cara.
        TopoDS_Face startCap;
        TopoDS_Face endCap;
        for (const TopoDS_Face* lateral : {&sides.front(), &sides.back()}) {
            for (TopExp_Explorer ee(*lateral, TopAbs_EDGE); ee.More(); ee.Next()) {
                const TopoDS_Edge edge = TopoDS::Edge(ee.Current());
                if (BRep_Tool::Degenerated(edge)) {
                    continue;
                }
                BRepAdaptor_Curve onEdge(edge);
                const gp_Pnt mid = onEdge.Value(0.5 * (onEdge.FirstParameter() + onEdge.LastParameter()));
                const double along = gp_Vec(origin, mid).Dot(gp_Vec(normal));
                const bool atStart = std::abs(along) < 1e-6;
                const bool atEnd = std::abs(along - length) < 1e-6;
                if ((!atStart && !atEnd) || (atStart && lateral != &sides.front()) || (atEnd && lateral != &sides.back())) {
                    continue;
                }
                const TopoDS_Wire wire = BRepBuilderAPI_MakeWire(edge).Wire();
                (atStart ? startCap : endCap) = BRepBuilderAPI_MakeFace(wire, true).Face();
            }
        }
        if (startCap.IsNull() || endCap.IsNull()) {
            throw GeometryError(std::string("No se pudo cerrar ") + what);
        }
        BRepBuilderAPI_Sewing sewing(1e-6);
        for (const TopoDS_Face& lateral : sides) {
            sewing.Add(lateral);
        }
        sewing.Add(startCap);
        sewing.Add(endCap);
        sewing.Perform();
        TopoDS_Shell shell;
        if (const TopExp_Explorer ex(sewing.SewedShape(), TopAbs_SHELL); ex.More()) {
            shell = TopoDS::Shell(ex.Current());
        }
        if (shell.IsNull()) {
            throw GeometryError(std::string("No se pudo cerrar ") + what);
        }
        BRepBuilderAPI_MakeSolid makeSolid(shell);
        TopoDS_Solid solid = makeSolid.Solid();
        // Orientación hacia fuera sin clasificar puntos (BRepLib::OrientClosedSolid lanza rayos contra la
        // superficie entera y tardaba segundos): en un punto de la cresta, la normal efectiva de la cara
        // lateral debe apuntar lejos del eje.
        {
            const TopoDS_Shape sewnSide = sewing.Modified(side);
            TopoDS_Face lateral = TopoDS::Face(sewnSide.IsNull() ? TopoDS_Shape(side) : sewnSide);
            const BRepAdaptor_Surface probe(lateral);
            gp_Pnt at;
            gp_Vec du;
            gp_Vec dv;
            probe.D1(0.5 * (probe.FirstUParameter() + probe.LastUParameter()), probe.FirstVParameter(), at, du, dv);
            gp_Vec outward = du.Crossed(dv);
            if (lateral.Orientation() == TopAbs_REVERSED) {
                outward.Reverse();
            }
            const gp_Vec axisPart = gp_Vec(normal) * gp_Vec(origin, at).Dot(gp_Vec(normal));
            const gp_Vec radial = gp_Vec(origin, at) - axisPart;
            if (outward.Dot(radial) < 0.0) {
                solid.Reverse();
            }
        }
        Namer namer(solid);
        const auto sewn = [&](const TopoDS_Face& face) {
            const TopoDS_Shape modified = sewing.Modified(face);
            return modified.IsNull() ? TopoDS_Shape(face) : modified;
        };
        namer.name(sewn(startCap), tag + ":inicio");
        namer.name(sewn(endCap), tag + ":fin");
        namer.nameUnnamed(tag + ":" + wall);
        Shape result = namer.finish(tag);
        if (!result.isValid()) {
            throw GeometryError(std::string(what) + " no es un sólido válido");
        }
        return result;
    } catch (const Standard_Failure& e) {
        throw kernelFailure(std::string("generar ") + what, hint, e);
    }
}

Shape thread(std::string_view tagView, const sketch::Plane& plane, const ThreadSpec& spec)
{
    const std::string tag(tagView);
    if (spec.pitch <= kTiny) {
        throw GeometryError("El paso de la rosca debe ser mayor que cero");
    }
    if (spec.diameter <= 4.0 * spec.pitch * 0.6134 * 0.5 + kTiny || threadMinorDiameter(spec) <= 0.2) {
        throw GeometryError("El diámetro es demasiado pequeño para ese paso");
    }
    if (std::abs(spec.length) < spec.pitch) {
        throw GeometryError("La rosca debe medir al menos un paso de largo");
    }
    const double rMax = spec.diameter / 2.0;
    const double rMin = threadMinorDiameter(spec) / 2.0;
    const double pitch = spec.pitch;
    // Radio de la rosca a lo largo de un paso: cresta, flanco, fondo, flanco.
    const auto profile = [&](double s) {
        s -= pitch * std::floor(s / pitch);
        const double crest = pitch / 8.0;
        const double root = pitch / 4.0;
        const double flank = (pitch - crest - root) / 2.0;
        if (s < crest) {
            return rMax;
        }
        if (s < crest + flank) {
            return rMax - (rMax - rMin) * (s - crest) / flank;
        }
        if (s < crest + flank + root) {
            return rMin;
        }
        return rMin + (rMax - rMin) * (s - crest - flank - root) / flank;
    };
    return twistedRod(tag, plane, profile, pitch, spec.length, spec.leftHanded, 32, 8, pitch / 12.0, "rosca", "la rosca",
                      "revisa el paso y el diámetro: el diámetro menor (diámetro − 1,23 × paso) debe ser positivo");
}

Shape threadOnCylinderTool(std::string_view tag, const Shape::CylinderInfo& info, const ThreadSpec& spec)
{
    const sketch::Plane plane = sketch::Plane::fromPointAndNormal(info.start, info.direction);
    ThreadSpec used = spec;
    used.length = info.length;
    if (info.hole) {
        used.diameter = 2.0 * info.radius + 2.0 * 0.6134 * spec.pitch;
        return thread(tag, plane, used);
    }
    used.diameter = 2.0 * info.radius;
    const Shape rod = thread(tag, plane, used);
    // Un poco más ancho que el cilindro para no dejar una lámina en las crestas.
    const Shape sleeve = cylinder(tag, plane, 2.0 * info.radius + 0.1, info.length);
    return boolean(BooleanOp::Cut, sleeve, rod);
}

double wormLeadAngle(const WormSpec& spec)
{
    return std::atan(spec.starts * spec.module / spec.pitchDiameter);
}

Shape worm(std::string_view tagView, const sketch::Plane& plane, const WormSpec& spec)
{
    const std::string tag(tagView);
    constexpr double kPi = std::numbers::pi;
    if (spec.module <= kTiny) {
        throw GeometryError("El módulo debe ser mayor que cero");
    }
    if (spec.starts < 1 || spec.starts > 4) {
        throw GeometryError("El tornillo sin fin tiene de 1 a 4 entradas");
    }
    if (spec.pressureAngle < 5.0 * kPi / 180.0 || spec.pressureAngle > 35.0 * kPi / 180.0) {
        throw GeometryError("El ángulo de presión debe estar entre 5° y 35°");
    }
    const double m = spec.module;
    const double rp = spec.pitchDiameter / 2.0;
    if (rp - 1.25 * m < 0.5 * m) {
        throw GeometryError("El diámetro primitivo del tornillo es demasiado pequeño para ese módulo (usa al menos 5 módulos)");
    }
    const double axialPitch = kPi * m;
    const double lead = spec.starts * axialPitch;
    const double tanA = std::tan(spec.pressureAngle);
    if (axialPitch / 4.0 - m * tanA < 0.05 * m) {
        throw GeometryError("Con ese ángulo de presión el filete sale puntiagudo: usa menos ángulo");
    }
    if (std::abs(spec.length) < lead) {
        throw GeometryError("El tornillo debe medir al menos una vuelta (entradas × π × módulo) de largo");
    }
    // Radio a la distancia axial s: un diente de cremallera por cada paso axial, centrado en s = 0.
    const auto radiusAt = [=](double s) {
        s -= axialPitch * std::floor(s / axialPitch + 0.5);
        const double height = (axialPitch / 4.0 - std::abs(s)) / tanA;
        return rp + std::clamp(height, -1.25 * m, m);
    };
    return twistedRod(tag, plane, radiusAt, lead, spec.length, spec.leftHanded, 32 * spec.starts, 8 * spec.starts, axialPitch / 24.0, "tornillo",
                      "el tornillo sin fin", "prueba con otro módulo, más diámetro primitivo o menos entradas");
}

Shape sweep(std::string_view tagView, const SketchRegion& profile, const sketch::Sketch& pathSketch,
            const sketch::Plane& pathPlane, const std::vector<sketch::EntityId>& pathEntities, const SweepSpec& spec)
{
    const std::string tag(tagView);
    if (pathEntities.empty()) {
        throw GeometryError("Elige la trayectoria del barrido");
    }
    if (spec.endScale <= 0.01 || spec.endScale > 100.0) {
        throw GeometryError("La escala al final del barrido debe estar entre 0,01 y 100");
    }
    try {
        const gp_Ax3 pathAxes = toAx3(pathPlane);
        NCollection_List<TopoDS_Shape> edges;
        for (const sketch::EntityId id : pathEntities) {
            const sketch::Entity* entity = pathSketch.entity(id);
            const auto edge = entity ? sketchEdge(*entity, pathAxes) : std::nullopt;
            if (!edge) {
                throw GeometryError("Una curva de la trayectoria ya no existe o no se puede usar");
            }
            edges.Append(*edge);
        }
        BRepBuilderAPI_MakeWire pathMaker;
        pathMaker.Add(edges);
        if (!pathMaker.IsDone()) {
            throw GeometryError("Las curvas de la trayectoria no forman un camino continuo");
        }
        const TopoDS_Wire path = pathMaker.Wire();

        std::vector<Shape> parts;
        for (const ProfileFace& pf : regionFaces(profile)) {
            const TopoDS_Wire outer = BRepTools::OuterWire(pf.face);
            // Barre un contorno y devuelve el sólido (orientado hacia fuera).
            const auto sweepWire = [&](const TopoDS_Wire& wire, BRepOffsetAPI_MakePipeShell& pipe) -> TopoDS_Shape {
                pipe.SetTransitionMode(BRepBuilderAPI_RoundCorner);
                if (!spec.followPath) {
                    pipe.SetMode(gp_Ax2(toPnt(profile.plane.origin), toDir(profile.plane.normal)));
                }
                pipe.Add(wire, false, false);
                if (std::abs(spec.endScale - 1.0) > 1e-9) {
                    // La escala va de 1 al principio a «endScale» al final, linealmente a lo largo de la trayectoria.
                    occ::handle<Law_Linear> law = new Law_Linear();
                    law->Set(0.0, 1.0, 1.0, spec.endScale);
                    pipe.SetLaw(wire, law, false, false);
                }
                buildCancellable(pipe);
                if (!pipe.IsDone() || !pipe.MakeSolid()) {
                    throw GeometryError("No se pudo barrer el perfil (¿está en el inicio de la trayectoria y de frente a ella?)");
                }
                TopoDS_Shape result = pipe.Shape();
                TopExp_Explorer solids(result, TopAbs_SOLID);
                if (!solids.More()) {
                    throw GeometryError("El barrido no dio un sólido");
                }
                TopoDS_Solid solid = TopoDS::Solid(solids.Current());
                BRepLib::OrientClosedSolid(solid);
                return solid;
            };
            BRepOffsetAPI_MakePipeShell pipe(path);
            const TopoDS_Shape solid = sweepWire(outer, pipe);
            Namer namer(solid);
            namer.name(pipe.FirstShape(), tag + ":inicio:" + pf.key);
            namer.name(pipe.LastShape(), tag + ":fin:" + pf.key);
            for (const auto& [edge, source] : pf.sources) {
                for (const TopoDS_Shape& side : pipe.Generated(edge)) {
                    namer.name(side, tag + ":lado:" + source);
                }
            }
            Shape part = namer.finish(tag);
            // Los huecos del perfil se barren aparte y se restan.
            for (TopExp_Explorer ex(pf.face, TopAbs_WIRE); ex.More(); ex.Next()) {
                const TopoDS_Wire hole = TopoDS::Wire(ex.Current());
                if (hole.IsSame(outer)) {
                    continue;
                }
                BRepOffsetAPI_MakePipeShell holePipe(path);
                Namer holeNamer(sweepWire(hole, holePipe));
                holeNamer.nameUnnamed(tag + ":hueco");
                part = boolean(BooleanOp::Cut, part, holeNamer.finish(tag));
            }
            parts.push_back(std::move(part));
        }
        Shape result = fuseAll(parts, tag);
        if (!result.isValid()) {
            throw GeometryError("El barrido no es un sólido válido");
        }
        return result;
    } catch (const Standard_Failure& e) {
        throw kernelFailure("barrer el perfil", "revisa que el perfil esté al inicio de la trayectoria y de frente a ella, y que el camino no tenga esquinas muy cerradas", e);
    }
}

Shape loft(std::string_view tagView, const std::vector<SketchRegion>& sections, bool ruled)
{
    const std::string tag(tagView);
    if (sections.size() < 2) {
        throw GeometryError("Un loft necesita al menos dos perfiles");
    }
    try {
        BRepOffsetAPI_ThruSections maker(true, ruled, 1e-6);
        std::vector<std::pair<TopoDS_Edge, std::string>> firstEdges;
        for (std::size_t i = 0; i < sections.size(); ++i) {
            const std::vector<ProfileFace> faces = regionFaces(sections[i]);
            if (faces.size() != 1) {
                throw GeometryError("Cada sección del loft debe ser un solo perfil");
            }
            const TopoDS_Wire outer = BRepTools::OuterWire(faces.front().face);
            maker.AddWire(outer);
            if (i == 0) {
                firstEdges = faces.front().sources;
            }
        }
        buildCancellable(maker);
        if (!maker.IsDone()) {
            throw GeometryError("No se pudo hacer el loft (¿los perfiles tienen una forma muy distinta?)");
        }
        Namer namer(maker.Shape());
        namer.name(maker.FirstShape(), tag + ":inicio");
        namer.name(maker.LastShape(), tag + ":fin");
        for (const auto& [edge, source] : firstEdges) {
            namer.name(maker.GeneratedFace(edge), tag + ":lado:" + source);
        }
        namer.nameUnnamed(tag + ":lado");
        Shape result = namer.finish(tag);
        if (!result.isValid()) {
            throw GeometryError("El loft no es un sólido válido");
        }
        return result;
    } catch (const Standard_Failure& e) {
        throw kernelFailure("hacer el loft", "revisa que los perfiles tengan una forma parecida y estén en el orden correcto", e);
    }
}

Shape moveBody(std::string_view tag, const Shape& body, const sketch::Vec3& translation, const Axis& axis, double angle,
               bool copy, const sketch::Vec3& turns, const sketch::Vec3& turnsAround)
{
    if (body.isNull()) {
        throw GeometryError("No hay cuerpo que mover");
    }
    try {
        gp_Trsf total;
        if (std::abs(angle) > 1e-12) {
            total.SetRotation(gp_Ax1(toPnt(axis.point), toDir(axis.direction)), angle);
        }
        // Los giros alrededor de X, Y y Z (el de X primero) por un mismo punto.
        const double around[3] = {turns.x, turns.y, turns.z};
        const gp_Dir directions[3] = {gp_Dir(1, 0, 0), gp_Dir(0, 1, 0), gp_Dir(0, 0, 1)};
        for (int k = 0; k < 3; ++k) {
            if (std::abs(around[k]) > 1e-12) {
                gp_Trsf turn;
                turn.SetRotation(gp_Ax1(toPnt(turnsAround), directions[k]), around[k]);
                total = turn * total;
            }
        }
        gp_Trsf shift;
        shift.SetTranslation(gp_Vec(translation.x, translation.y, translation.z));
        total = shift * total;
        BRepBuilderAPI_Transform transform(occtShape(body), total, true);
        if (!transform.IsDone() || transform.Shape().IsNull()) {
            throw GeometryError("No se pudo mover el cuerpo");
        }
        Namer namer(transform.Shape());
        const Shape::Impl& in = *body.impl();
        for (int i = 1; i <= in.faces.Extent(); ++i) {
            const std::string& name = in.faceNames[std::size_t(i - 1)];
            for (const TopoDS_Shape& image : transform.Modified(in.faces(i))) {
                namer.name(image, copy ? std::string(tag) + ":copia:" + name : name);
            }
        }
        namer.nameUnnamed(std::string(tag) + ":copia");
        return namer.finish(std::string(tag));
    } catch (const Standard_Failure& e) {
        throw kernelFailure("mover el cuerpo", "revisa que el desplazamiento y el ángulo sean razonables", e);
    }
}

Shape placeBody(std::string_view tag, const Shape& body, const sketch::Plane& from, const sketch::Plane& to)
{
    if (body.isNull()) {
        throw GeometryError("No hay cuerpo que colocar");
    }
    try {
        gp_Trsf displacement;
        displacement.SetDisplacement(toAx3(from), toAx3(to));
        BRepBuilderAPI_Transform transform(occtShape(body), displacement, true);
        if (!transform.IsDone() || transform.Shape().IsNull()) {
            throw GeometryError("No se pudo colocar el cuerpo");
        }
        Namer namer(transform.Shape());
        const Shape::Impl& in = *body.impl();
        for (int i = 1; i <= in.faces.Extent(); ++i) {
            for (const TopoDS_Shape& image : transform.Modified(in.faces(i))) {
                namer.name(image, in.faceNames[std::size_t(i - 1)]);
            }
        }
        namer.nameUnnamed(std::string(tag) + ":colocado");
        return namer.finish(std::string(tag));
    } catch (const Standard_Failure& e) {
        throw kernelFailure("colocar el cuerpo", "revisa que las dos caras elegidas sean planas y de cuerpos distintos", e);
    }
}

Shape holeTool(std::string_view tag, const sketch::Plane& plane, const std::vector<sketch::Vec2>& centers,
               const HoleSpec& spec, bool flip, const std::vector<Shape>& context)
{
    if (centers.empty()) {
        throw GeometryError("No hay círculos ni puntos que marquen dónde van los agujeros");
    }
    if (spec.diameter <= kTiny) {
        throw GeometryError("El diámetro del agujero debe ser mayor que cero");
    }
    double depth = spec.depth;
    if (spec.throughAll) {
        // Más largo que cualquier cuerpo: la diagonal de todo lo que hay.
        depth = 1.0;
        for (const Shape& shape : context) {
            const Box box = shape.bounds();
            if (!box.empty) {
                depth += std::sqrt(std::pow(box.max.x - box.min.x, 2) + std::pow(box.max.y - box.min.y, 2)
                                   + std::pow(box.max.z - box.min.z, 2));
            }
        }
    } else if (depth <= kTiny) {
        throw GeometryError("La profundidad del agujero debe ser mayor que cero");
    }
    const double radius = spec.diameter / 2.0;
    const double headRadius = spec.headDiameter / 2.0;
    if (spec.kind != HoleKind::Simple) {
        if (headRadius <= radius + kTiny) {
            throw GeometryError("El diámetro de la cabeza debe ser mayor que el del agujero");
        }
        if (spec.kind == HoleKind::Counterbore && (spec.headDepth <= kTiny || (!spec.throughAll && spec.headDepth >= depth))) {
            throw GeometryError("La profundidad del abocardado debe ser positiva y menor que la del agujero");
        }
        if (spec.kind == HoleKind::Countersink && (spec.headAngle <= 0.01 || spec.headAngle >= std::numbers::pi - 0.01)) {
            throw GeometryError("El ángulo del avellanado debe estar entre 0° y 180°");
        }
    }
    constexpr double kLead = 1.0;  // el sólido empieza fuera de la superficie
    try {
        // Hacia dentro del material: en contra de la normal de la cara.
        sketch::Vec3 direction = plane.normal;
        if (!flip) {
            direction = {-direction.x, -direction.y, -direction.z};
        }
        const gp_Dir axisDir = toDir(direction);
        BRep_Builder builder;
        TopoDS_Compound compound;
        builder.MakeCompound(compound);
        const auto fuse = [](const TopoDS_Shape& a, const TopoDS_Shape& b) {
            BRepAlgoAPI_Fuse fused(a, b);
            if (!fused.IsDone() || fused.HasErrors()) {
                throw GeometryError("No se pudo construir el agujero");
            }
            return fused.Shape();
        };
        for (const sketch::Vec2& center : centers) {
            const sketch::Vec3 on = plane.toWorld(center);
            const gp_Pnt start(on.x - direction.x * kLead, on.y - direction.y * kLead, on.z - direction.z * kLead);
            const gp_Ax2 axis(start, axisDir);
            TopoDS_Shape hole = BRepPrimAPI_MakeCylinder(axis, radius, depth + kLead).Shape();
            if (spec.kind == HoleKind::Counterbore) {
                hole = fuse(hole, BRepPrimAPI_MakeCylinder(axis, headRadius, spec.headDepth + kLead).Shape());
            } else if (spec.kind == HoleKind::Countersink) {
                const double half = spec.headAngle / 2.0;
                const double coneLength = (headRadius - radius) / std::tan(half);
                // El cono llega al diámetro de la cabeza justo en la superficie.
                hole = fuse(hole, BRepPrimAPI_MakeCone(axis, headRadius + kLead * std::tan(half), radius, coneLength + kLead).Shape());
            }
            builder.Add(compound, hole);
        }
        Namer namer(compound);
        namer.nameUnnamed(std::string(tag) + ":pared");
        return namer.finish(std::string(tag));
    } catch (const Standard_Failure& e) {
        throw kernelFailure("hacer el agujero", "revisa que el diámetro y la profundidad quepan en la pieza y que los centros estén sobre ella", e);
    }
}

namespace {

// El vaciado de verdad; con «allowClosed» y sin caras que quitar deja una cavidad cerrada.
Shape shellImpl(std::string_view tag, const Shape& body, const std::vector<int>& removedFaces, double thickness, bool outward,
                bool allowClosed)
{
    if (body.isNull()) {
        throw GeometryError("No hay cuerpo sobre el que trabajar");
    }
    if (removedFaces.empty() && !allowClosed) {
        throw GeometryError("Elige al menos una cara para dejar abierta");
    }
    if (thickness <= kTiny) {
        throw GeometryError("El espesor debe ser mayor que cero");
    }
    const std::string failure = "No se pudo vaciar: prueba con un espesor menor";
    try {
        NCollection_List<TopoDS_Shape> removed;
        for (int index : removedFaces) {
            removed.Append(faceAt(body, index));
        }
        BRepOffsetAPI_MakeThickSolid maker;
        occ::handle<ProgressCanceller> canceller = new ProgressCanceller();
        maker.MakeThickSolidByJoin(occtShape(body), removed, outward ? thickness : -thickness, 1e-4, BRepOffset_Skin,
                                   false, false, GeomAbs_Intersection, false, canceller->Start());
        throwIfCancelled();
        if (!maker.IsDone() || maker.Shape().IsNull()) {
            throw GeometryError(failure);
        }
        Namer namer(maker.Shape());
        const Shape::Impl& in = *body.impl();
        for (int i = 1; i <= in.faces.Extent(); ++i) {
            const TopoDS_Shape& face = in.faces(i);
            const std::string& name = in.faceNames[std::size_t(i - 1)];
            if (maker.IsDeleted(face)) {
                continue;
            }
            const auto& modified = maker.Modified(face);
            if (modified.IsEmpty()) {
                namer.name(face, name);
            }
            for (const TopoDS_Shape& image : modified) {
                namer.name(image, name);
            }
        }
        // La pared nueva de cada cara que se queda.
        for (int i = 1; i <= in.faces.Extent(); ++i) {
            for (const TopoDS_Shape& generated : maker.Generated(in.faces(i))) {
                namer.name(generated, std::string(tag) + ":interior:" + in.faceNames[std::size_t(i - 1)]);
            }
        }
        namer.nameUnnamed(std::string(tag) + ":borde");
        Shape result = namer.finish(std::string(tag));
        if (!result.isValid() || result.solidCount() == 0) {
            throw GeometryError(failure);
        }
        // Con un espesor imposible el kernel a veces devuelve el cuerpo intacto sin avisar.
        if (removedFaces.empty() ? std::abs(result.volume() - body.volume()) < 1e-9
                                 : result.faceCount() == body.faceCount() && std::abs(result.volume() - body.volume()) < 1e-9) {
            throw GeometryError(failure);
        }
        return result;
    } catch (const Standard_Failure& e) {
        throw GeometryError(failure + " (" + kernelMessage(e) + ")");
    }
}

} // namespace

Shape shell(std::string_view tag, const Shape& body, const std::vector<int>& removedFaces, double thickness, bool outward)
{
    return shellImpl(tag, body, removedFaces, thickness, outward, false);
}

Shape shellClosed(std::string_view tag, const Shape& body, double thickness)
{
    // Sin caras que quitar, el kernel deja solo el sólido de dentro (el desplazado hacia dentro): el cuerpo vaciado es el
    // cuerpo menos ese sólido, con su cavidad cerrada.
    const Shape inner = shellImpl(tag, body, {}, thickness, false, true);
    const std::string failure = "No se pudo vaciar: prueba con un espesor menor";
    try {
        BRepAlgoAPI_Cut cut(occtShape(body), occtShape(inner));
        buildCancellable(cut);
        if (!cut.IsDone() || cut.Shape().IsNull()) {
            throw GeometryError(failure);
        }
        Namer namer(cut.Shape());
        const Shape::Impl& outerIn = *body.impl();
        for (int i = 1; i <= outerIn.faces.Extent(); ++i) {
            const TopoDS_Shape& face = outerIn.faces(i);
            const auto& modified = cut.Modified(face);
            if (modified.IsEmpty() && !cut.IsDeleted(face)) {
                namer.name(face, outerIn.faceNames[std::size_t(i - 1)]);
            }
            for (const TopoDS_Shape& image : modified) {
                namer.name(image, outerIn.faceNames[std::size_t(i - 1)]);
            }
        }
        // Las caras de la cavidad: las del sólido de dentro, que son las paredes interiores.
        const Shape::Impl& innerIn = *inner.impl();
        for (int i = 1; i <= innerIn.faces.Extent(); ++i) {
            const TopoDS_Shape& face = innerIn.faces(i);
            const auto& modified = cut.Modified(face);
            const std::string name = std::string(tag) + ":interior:" + innerIn.faceNames[std::size_t(i - 1)];
            if (modified.IsEmpty() && !cut.IsDeleted(face)) {
                namer.name(face, name);
            }
            for (const TopoDS_Shape& image : modified) {
                namer.name(image, name);
            }
        }
        namer.nameUnnamed(std::string(tag) + ":interior");
        Shape result = namer.finish(std::string(tag));
        if (!result.isValid() || result.solidCount() == 0) {
            throw GeometryError(failure);
        }
        return result;
    } catch (const Standard_Failure& e) {
        throw GeometryError(failure + " (" + kernelMessage(e) + ")");
    }
}

} // namespace nuf::geometry
