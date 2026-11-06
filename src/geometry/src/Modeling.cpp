#include "Naming.hpp"

#include <nuf/geometry/Modeling.hpp>
#include <nuf/geometry/Occt.hpp>

#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepBndLib.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeSolid.hxx>
#include <BRepBuilderAPI_Sewing.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <Geom_BSplineSurface.hxx>
#include <NCollection_Array1.hxx>
#include <NCollection_Array2.hxx>
#include <TopoDS_Shell.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepOffsetAPI_MakePipeShell.hxx>
#include <BRepOffsetAPI_ThruSections.hxx>
#include <BRepTools.hxx>
#include <GeomAPI_Interpolate.hxx>
#include <Geom_BSplineCurve.hxx>
#include <NCollection_HArray1.hxx>
#include <gp_Circ.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepFilletAPI_MakeChamfer.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepGProp.hxx>
#include <BRepOffsetAPI_DraftAngle.hxx>
#include <BRepOffsetAPI_MakeOffset.hxx>
#include <BRepOffsetAPI_MakeThickSolid.hxx>
#include <NCollection_List.hxx>
#include <GProp_GProps.hxx>
#include <BRepLib.hxx>
#include <BRepPrimAPI_MakeCone.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakeHalfSpace.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepPrimAPI_MakeRevol.hxx>
#include <BRep_Builder.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <Standard_Failure.hxx>
#include <Standard_Version.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS_Iterator.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Solid.hxx>
#include <gp_Ax1.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>
#include <gp_Pln.hxx>

#include <algorithm>
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

// Une las caras coplanares (y aristas colineales) que dejan las booleanas.
Shape unify(const Shape& input, const std::string& tag)
{
    ShapeUpgrade_UnifySameDomain unifier(occtShape(input), true, true, false);
    unifier.Build();
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
    fuse.Build();
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

// Cara de «context» señalada por la referencia, que debe ser plana.
gp_Pln targetPlane(const std::vector<Shape>& context, const ElementRef& ref)
{
    for (const Shape& s : context) {
        if (const auto index = s.findFace(ref)) {
            if (const auto plane = s.facePlane(*index)) {
                return gp_Pln(toPnt(plane->origin), toDir(plane->normal));
            }
            throw GeometryError("La cara de destino no es plana");
        }
    }
    throw GeometryError("No se encuentra la cara de destino");
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
            // Recorta el prisma largo con el semiespacio de la cara destino
            // que contiene el perfil.
            const gp_Pln target = targetPlane(context, side.face);
            const gp_Pnt inside = BRepAdaptor_Surface(face).Plane().Location();
            const TopoDS_Face big = BRepBuilderAPI_MakeFace(target, -1e5, 1e5, -1e5, 1e5).Face();
            const TopoDS_Solid half = BRepPrimAPI_MakeHalfSpace(big, inside).Solid();
            BRepAlgoAPI_Common common(occtShape(part), half);
            if (!common.IsDone() || common.HasErrors()) {
                throw GeometryError("No se pudo extruir hasta la cara");
            }
            Namer trimmed(common.Shape());
            trimmed.inherit(part, *common.History());
            trimmed.nameUnnamed(endName + pf.key);  // la cara nueva viene del destino
            part = trimmed.finish(tag);
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
        throw GeometryError(std::string("Error del kernel al extruir: ") + kernelMessage(e));
    }
}

Shape extrudeFace(std::string_view tag, const Shape& body, int faceIndex, const ExtrudeSpec& spec,
                  const std::vector<Shape>& context)
{
    try {
        const sketch::Plane plane = body.facePlane(faceIndex).value_or(sketch::Plane{});
        return extrudeFaces(std::string(tag), {bodyFace(body, faceIndex)}, toDir(plane.normal), spec, context);
    } catch (const Standard_Failure& e) {
        throw GeometryError(std::string("Error del kernel al extruir: ") + kernelMessage(e));
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
        throw GeometryError(std::string("Error del kernel al revolucionar: ") + kernelMessage(e));
    }
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
        algo->Build();
        if (!algo->IsDone() || algo->HasErrors()) {
            throw GeometryError("La operación booleana falló");
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
        throw GeometryError(std::string("Error del kernel en la booleana: ") + kernelMessage(e));
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

template <typename Maker>
Shape edgeOperation(std::string_view tag, const std::string& label, const Shape& body, const std::vector<int>& edges,
                    double size, const std::string& failure)
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
            maker.Add(size, edgeAt(body, edge));
        }
        maker.Build();
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
        throw GeometryError(std::string("Error del kernel al reflejar: ") + kernelMessage(e));
    }
}

std::vector<Shape> pattern(std::string_view tag, const Shape& body, const PatternSpec& spec)
{
    if (body.isNull()) {
        throw GeometryError("No hay cuerpo que repetir");
    }
    if (spec.count < 2) {
        throw GeometryError("Un patrón necesita al menos 2 elementos");
    }
    if (spec.count > 500) {
        throw GeometryError("Demasiados elementos en el patrón (máximo 500)");
    }
    const sketch::Vec3& dir = spec.axis.direction;
    if (std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z) <= kTiny) {
        throw GeometryError("La dirección del patrón no es válida");
    }
    if (!spec.circular && std::abs(spec.spacing) <= kTiny) {
        throw GeometryError("La distancia entre copias debe ser distinta de cero");
    }
    if (spec.circular && std::abs(spec.angle) <= 1e-9) {
        throw GeometryError("El ángulo del patrón debe ser distinto de cero");
    }
    try {
        const TopoDS_Shape source = occtShape(body);
        const Shape::Impl& in = *body.impl();
        const bool full = std::abs(spec.angle) >= 6.283185307179586 - 1e-6;
        const double step = full ? spec.angle / spec.count : spec.angle / (spec.count - 1);
        std::vector<Shape> copies;
        for (int k = 1; k < spec.count; ++k) {
            gp_Trsf move;
            if (spec.circular) {
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
        throw GeometryError(std::string("Error del kernel al repetir el cuerpo: ") + kernelMessage(e));
    }
}

Shape gear(std::string_view tagView, const sketch::Plane& plane, const GearSpec& spec)
{
    const std::string tag(tagView);
    constexpr double kPi = std::numbers::pi;
    constexpr double kDeg = kPi / 180.0;
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
    const double ra = rp + spec.module;
    const double rf = rp - 1.25 * spec.module;
    if (rf <= kTiny) {
        throw GeometryError("Hay muy pocos dientes para ese módulo");
    }
    const auto inv = [](double a) { return std::tan(a) - a; };
    // Mitad del ancho angular del diente a un radio r (la evolvente).
    const auto half = [&](double r) {
        return kPi / (2.0 * z) - (inv(std::acos(std::min(1.0, rb / r))) - inv(spec.pressureAngle));
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
    if (spec.bore < 0.0 || spec.bore / 2.0 >= 0.9 * rf) {
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
            occ::handle<NCollection_HArray1<gp_Pnt>> array = new NCollection_HArray1<gp_Pnt>(1, int(points.size()));
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

        if (std::abs(twist) < 1e-9) {
            BRepBuilderAPI_MakeFace face(gp_Pln(origin, normal), outline.Wire(), true);
            TopoDS_Edge boreEdge;
            if (spec.bore > 0.0) {
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
            Shape result = namer.finish(tag);
            if (!result.isValid()) {
                throw GeometryError("El engranaje no es un sólido válido");
            }
            return result;
        }

        // Helicoidal: secciones giradas a lo largo del ancho, unidas en una superficie lateral. Las tapas
        // (con el agujero) y la pared del agujero se hacen aparte y se cosen: así no hace falta ninguna
        // booleana contra la superficie lateral (que era casi la mitad del tiempo).
        const int sections = std::clamp(int(std::ceil(std::abs(twist) / (15.0 * kDeg))) + 1, 3, 24);
        BRepOffsetAPI_ThruSections loft(false, false, 1e-6);
        TopoDS_Wire lastWire = outline.Wire();
        for (int k = 0; k <= sections; ++k) {
            const double t = double(k) / sections;
            if (k == 0) {
                loft.AddWire(outline.Wire());
                continue;
            }
            gp_Trsf turn;
            turn.SetRotation(gp_Ax1(origin, normal), twist * t);
            gp_Trsf slide;
            slide.SetTranslation(gp_Vec(normal).Multiplied(spec.thickness * t));
            BRepBuilderAPI_Transform moved(outline.Wire(), slide * turn, true);
            lastWire = TopoDS::Wire(moved.Shape());
            loft.AddWire(lastWire);
        }
        loft.Build();
        if (!loft.IsDone()) {
            throw GeometryError("No se pudo generar el engranaje helicoidal");
        }
        const double sign = spec.thickness > 0 ? 1.0 : -1.0;
        const gp_Pnt endPoint(origin.XYZ() + normal.XYZ() * spec.thickness);
        // Tapa: cara plana con el contorno y, si hay agujero, su círculo; hacia fuera del sólido.
        const auto cap = [&](const gp_Pnt& where, const TopoDS_Wire& wire, bool end) {
            BRepBuilderAPI_MakeFace face(gp_Pln(where, normal), wire, true);
            if (spec.bore > 0.0) {
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
        TopoDS_Face boreWall;
        if (spec.bore > 0.0) {
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
        for (TopExp_Explorer ex(loft.Shape(), TopAbs_FACE); ex.More(); ex.Next()) {
            sewing.Add(ex.Current());
        }
        sewing.Add(startCap);
        sewing.Add(endCap);
        if (!boreWall.IsNull()) {
            sewing.Add(boreWall);
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
        for (const auto& [edge, name] : edges) {
            if (name.ends_with(":cabeza")) {
                for (const TopoDS_Shape& side : loft.Generated(edge)) {
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
                    if (outward.Dot(radial) < 0.0) {
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
        for (const auto& [edge, name] : edges) {
            for (const TopoDS_Shape& side : loft.Generated(edge)) {
                namer.name(sewnShape(side), tag + ":lado:" + name);
            }
        }
        Shape result = namer.finish(tag);
        if (!result.isValid()) {
            throw GeometryError("El engranaje helicoidal no es un sólido válido");
        }
        return result;
    } catch (const Standard_Failure& e) {
        throw GeometryError(std::string("Error del kernel al generar el engranaje: ") + kernelMessage(e));
    }
}

double threadMinorDiameter(const ThreadSpec& spec)
{
    return spec.diameter - 2.0 * 0.6134 * spec.pitch;
}

Shape thread(std::string_view tagView, const sketch::Plane& plane, const ThreadSpec& spec)
{
    const std::string tag(tagView);
    constexpr double kPi = std::numbers::pi;
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
    try {
        const gp_Pnt origin = toPnt(plane.origin);
        const gp_Dir normal = toDir(plane.normal);
        // Corte transversal: r(φ) = perfil(−paso·φ/2π), una curva cerrada y periódica; a cada altura es
        // la misma curva girada, así que la rosca es UNA superficie (lineal a lo largo del eje, por
        // secciones giradas) más dos tapas: 3 caras en vez de cientos, que es lo que hace rápido todo lo
        // que viene después (comprobar, mallar, booleanas).
        constexpr int kPoints = 32;
        const double hand = spec.leftHanded ? -1.0 : 1.0;
        occ::handle<NCollection_HArray1<gp_Pnt>> array = new NCollection_HArray1<gp_Pnt>(1, kPoints);
        for (int i = 0; i < kPoints; ++i) {
            const double phi = 2.0 * kPi * i / kPoints;
            // Las esquinas del perfil se suavizan (promedio en una ventana de ±paso/24): la curva por
            // puntos no se pasa de largo en las esquinas (sin ello el diámetro salía hasta un 11 % mayor)
            // y se parece más a una rosca real, con cresta y fondo redondeados.
            double r = 0.0;
            constexpr int kWindow = 9;
            for (int w = 0; w < kWindow; ++w) {
                const double offset = (double(w) / (kWindow - 1) - 0.5) * (pitch / 12.0);
                r += profile(-hand * pitch * phi / (2.0 * kPi) + offset);
            }
            r /= kWindow;
            array->SetValue(i + 1, toPnt(plane.toWorld({r * std::cos(phi), r * std::sin(phi)})));
        }
        GeomAPI_Interpolate interpolation(array, true, 1e-7);
        interpolation.Perform();
        if (!interpolation.IsDone()) {
            throw GeometryError("No se pudo trazar el perfil de la rosca");
        }
        const occ::handle<Geom_BSplineCurve> curve = interpolation.Curve();

        const double turns = std::abs(spec.length) / pitch;
        // Una sección cada 45° (8 por vuelta), unidas por una curva cúbica en cada polo de la sección:
        // la superficie es suave (C2) en toda la hélice, que es lo que necesitan las booleanas (con
        // aristas vivas entre secciones no encontraban la intersección con los planos del bloque).
        const int sections = std::clamp(int(std::ceil(turns * 8.0)), 6, 1600);
        const int nPoles = curve->NbPoles();
        std::vector<gp_Trsf> moves;
        for (int k = 0; k <= sections; ++k) {
            const double t = double(k) / sections;
            gp_Trsf turn;
            turn.SetRotation(gp_Ax1(origin, normal), hand * 2.0 * kPi * turns * t * (spec.length < 0 ? -1.0 : 1.0));
            gp_Trsf slide;
            slide.SetTranslation(gp_Vec(normal).Multiplied(spec.length * t));
            moves.push_back(slide * turn);
        }
        // Una cara por cada vuelta y media: el mallado (lo más caro de dibujar la rosca) crece más que
        // linealmente con el tamaño de cada cara (una sola de 8 vueltas: 17 s; en tramos de 1,5 vueltas: 3 s).
        const int chunks = std::max(1, int(std::ceil(turns / 1.5)));
        std::vector<TopoDS_Face> sides;
        for (int c = 0; c < chunks; ++c) {
            const int k0 = int(std::lround(double(c) * sections / chunks));
            const int k1 = int(std::lround(double(c + 1) * sections / chunks));
            const int count = k1 - k0;
            occ::handle<NCollection_HArray1<double>> parameters = new NCollection_HArray1<double>(1, count + 1);
            for (int k = 0; k <= count; ++k) {
                parameters->SetValue(k + 1, double(k) / count);
            }
            std::vector<occ::handle<Geom_BSplineCurve>> helices;
            for (int j = 1; j <= nPoles; ++j) {
                occ::handle<NCollection_HArray1<gp_Pnt>> path = new NCollection_HArray1<gp_Pnt>(1, count + 1);
                for (int k = 0; k <= count; ++k) {
                    path->SetValue(k + 1, curve->Pole(j).Transformed(moves[std::size_t(k0 + k)]));
                }
                GeomAPI_Interpolate along(path, parameters, false, 1e-9);
                along.Perform();
                if (!along.IsDone()) {
                    throw GeometryError("No se pudo trazar la hélice de la rosca");
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
                const bool atEnd = std::abs(along - spec.length) < 1e-6;
                if ((!atStart && !atEnd) || (atStart && lateral != &sides.front()) || (atEnd && lateral != &sides.back())) {
                    continue;
                }
                const TopoDS_Wire wire = BRepBuilderAPI_MakeWire(edge).Wire();
                (atStart ? startCap : endCap) = BRepBuilderAPI_MakeFace(wire, true).Face();
            }
        }
        if (startCap.IsNull() || endCap.IsNull()) {
            throw GeometryError("No se pudo cerrar la rosca");
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
            throw GeometryError("No se pudo cerrar la rosca");
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
        namer.nameUnnamed(tag + ":rosca");
        Shape result = namer.finish(tag);
        if (!result.isValid()) {
            throw GeometryError("La rosca no es un sólido válido");
        }
        return result;
    } catch (const Standard_Failure& e) {
        throw GeometryError(std::string("Error del kernel al generar la rosca: ") + kernelMessage(e));
    }
}

Shape sweep(std::string_view tagView, const SketchRegion& profile, const sketch::Sketch& pathSketch,
            const sketch::Plane& pathPlane, const std::vector<sketch::EntityId>& pathEntities, const SweepSpec& spec)
{
    const std::string tag(tagView);
    if (pathEntities.empty()) {
        throw GeometryError("Elige la trayectoria del barrido");
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
                pipe.Build();
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
        throw GeometryError(std::string("Error del kernel al barrer: ") + kernelMessage(e));
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
        maker.Build();
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
        throw GeometryError(std::string("Error del kernel en el loft: ") + kernelMessage(e));
    }
}

Shape moveBody(std::string_view tag, const Shape& body, const sketch::Vec3& translation, const Axis& axis, double angle,
               bool copy)
{
    if (body.isNull()) {
        throw GeometryError("No hay cuerpo que mover");
    }
    try {
        gp_Trsf total;
        if (std::abs(angle) > 1e-12) {
            total.SetRotation(gp_Ax1(toPnt(axis.point), toDir(axis.direction)), angle);
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
        throw GeometryError(std::string("Error del kernel al mover el cuerpo: ") + kernelMessage(e));
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
        throw GeometryError(std::string("Error del kernel al colocar el cuerpo: ") + kernelMessage(e));
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
        throw GeometryError(std::string("Error del kernel al hacer el agujero: ") + kernelMessage(e));
    }
}

Shape shell(std::string_view tag, const Shape& body, const std::vector<int>& removedFaces, double thickness, bool outward)
{
    if (body.isNull()) {
        throw GeometryError("No hay cuerpo sobre el que trabajar");
    }
    if (removedFaces.empty()) {
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
        maker.MakeThickSolidByJoin(occtShape(body), removed, outward ? thickness : -thickness, 1e-4, BRepOffset_Skin,
                                   false, false, GeomAbs_Intersection);
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
        if (result.faceCount() == body.faceCount() && std::abs(result.volume() - body.volume()) < 1e-9) {
            throw GeometryError(failure);
        }
        return result;
    } catch (const Standard_Failure& e) {
        throw GeometryError(failure + " (" + kernelMessage(e) + ")");
    }
}

} // namespace nuf::geometry
