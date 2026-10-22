#include "Naming.hpp"

#include <nuf/geometry/Modeling.hpp>
#include <nuf/geometry/Occt.hpp>

#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepOffsetAPI_DraftAngle.hxx>
#include <BRepPrimAPI_MakeHalfSpace.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepPrimAPI_MakeRevol.hxx>
#include <BRep_Builder.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <Standard_Failure.hxx>
#include <Standard_Version.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
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
            algo = std::make_unique<BRepAlgoAPI_Fuse>(a, b);
            break;
        case BooleanOp::Cut:
            algo = std::make_unique<BRepAlgoAPI_Cut>(a, b);
            break;
        case BooleanOp::Common:
            algo = std::make_unique<BRepAlgoAPI_Common>(a, b);
            break;
        }
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
        return unified.isValid() || !result.isValid() ? unified : result;
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
    BRepExtrema_DistShapeShape distance(occtShape(a), occtShape(b));
    return distance.IsDone() && distance.Value() < 1e-6;
}

} // namespace nuf::geometry
