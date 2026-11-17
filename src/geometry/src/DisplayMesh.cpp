#include <nuf/geometry/DisplayMesh.hpp>

#include <nuf/geometry/Occt.hpp>

#include "Naming.hpp"

#include <Standard_Version.hxx>

// ComputeNormals vive en BRepLib_ desde OCCT 7.7; antes estaba en StdPrs_.
#if OCC_VERSION_HEX >= 0x070700
#include <BRepLib_ToolTriangulatedShape.hxx>
#else
#include <StdPrs_ToolTriangulatedShape.hxx>
#endif

#include <BRepAdaptor_Surface.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <IMeshTools_Parameters.hxx>
#include <Message_ProgressIndicator.hxx>
#include <Message_ProgressRange.hxx>
#include <Standard_Failure.hxx>
#include <Poly_PolygonOnTriangulation.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopLoc_Location.hxx>
#include <NCollection_IndexedMap.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>

namespace nuf::geometry {

using ShapeIndex = NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher>;

struct PreparedMesh::Impl {
    TopoDS_Shape copy;  // la copia ya mallada, con las mismas caras en el mismo orden
};

PreparedMesh::PreparedMesh() = default;
PreparedMesh::PreparedMesh(std::shared_ptr<Impl> impl)
    : impl_(std::move(impl))
{
}
PreparedMesh::~PreparedMesh() = default;
PreparedMesh::PreparedMesh(PreparedMesh&&) noexcept = default;
PreparedMesh& PreparedMesh::operator=(PreparedMesh&&) noexcept = default;

bool hasHeavySurface(const Shape& shape)
{
    if (shape.impl() == nullptr) {
        return false;
    }
    if (const signed char known = shape.impl()->heavy.load(); known >= 0) {
        return known == 1;
    }
    // Suma de puntos de control de las caras spline (una rosca partida en tramos tiene miles).
    constexpr long kHeavyPoles = 2000;
    long poles = 0;
    for (int i = 0; i < shape.faceCount(); ++i) {
        const BRepAdaptor_Surface surface(faceAt(shape, i));
        if (surface.GetType() == GeomAbs_BSplineSurface) {
            poles += long(surface.NbUPoles()) * surface.NbVPoles();
        }
    }
    shape.impl()->heavy.store(poles > kHeavyPoles ? 1 : 0);
    return poles > kHeavyPoles;
}

namespace {

// Avisa de la cancelación al mallador.
class Canceller final : public Message_ProgressIndicator {
public:
    explicit Canceller(const std::atomic<bool>& flag)
        : flag_(flag)
    {
    }

protected:
    bool UserBreak() override { return flag_.load(); }
    void Show(const Message_ProgressScope&, const bool) override {}

private:
    const std::atomic<bool>& flag_;
};

} // namespace

PreparedMesh prepareMesh(const Shape& shape, double deviation, double angle, const std::atomic<bool>& cancel)
{
    if (shape.isNull()) {
        return PreparedMesh(nullptr);
    }
    try {
        // Copia completa (topología y geometría): el hilo de trabajo no comparte nada con la interfaz.
        BRepBuilderAPI_Copy copier(occtShape(shape), true, false);
        TopoDS_Shape copy = copier.Shape();
        IMeshTools_Parameters parameters;
        parameters.Deflection = deviation;
        parameters.Angle = angle;
        parameters.InParallel = true;
        parameters.InternalVerticesMode = true;
        parameters.ControlSurfaceDeflection = true;
        occ::handle<Canceller> canceller = new Canceller(cancel);
        BRepMesh_IncrementalMesh mesher(copy, parameters, canceller->Start());
        if (cancel.load()) {
            return PreparedMesh(nullptr);
        }
        // Las normales de cada nodo, evaluando la superficie: lo más caro de dibujar una rosca.
        ShapeIndex faces;
        TopExp::MapShapes(copy, TopAbs_FACE, faces);
        for (int i = 1; i <= faces.Extent(); ++i) {
            if (cancel.load()) {
                return PreparedMesh(nullptr);
            }
            const TopoDS_Face face = TopoDS::Face(faces(i));
            TopLoc_Location location;
            const occ::handle<Poly_Triangulation> triangulation = BRep_Tool::Triangulation(face, location);
            if (!triangulation.IsNull()) {
#if OCC_VERSION_HEX >= 0x070700
                BRepLib_ToolTriangulatedShape::ComputeNormals(face, triangulation);
#else
                StdPrs_ToolTriangulatedShape::ComputeNormals(face, triangulation);
#endif
            }
        }
        auto impl = std::make_shared<PreparedMesh::Impl>();
        impl->copy = copy;
        return PreparedMesh(std::move(impl));
    } catch (...) {
        return PreparedMesh(nullptr);  // sea cual sea el fallo, el visor dibuja el cuerpo como siempre
    }
}

void applyMesh(const Shape& shape, const PreparedMesh& mesh)
{
    if (shape.isNull() || !mesh.valid()) {
        return;
    }
    ShapeIndex from;
    ShapeIndex to;
    TopExp::MapShapes(mesh.impl()->copy, TopAbs_FACE, from);
    TopExp::MapShapes(occtShape(shape), TopAbs_FACE, to);
    if (from.Extent() != to.Extent()) {
        return;
    }
    const BRep_Builder builder;
    for (int i = 1; i <= from.Extent(); ++i) {
        const TopoDS_Face fromFace = TopoDS::Face(from(i));
        const TopoDS_Face toFace = TopoDS::Face(to(i));
        TopLoc_Location fromLocation;
        const occ::handle<Poly_Triangulation> triangulation = BRep_Tool::Triangulation(fromFace, fromLocation);
        if (triangulation.IsNull()) {
            continue;
        }
        builder.UpdateFace(toFace, triangulation);
        // Los polígonos de las aristas sobre esa malla: el visor dibuja con ellos el borde de cada cara. Las dos caras
        // (la copia y la original) tienen la misma topología, así que sus aristas salen en el mismo orden.
        TopLoc_Location toLocation;
        BRep_Tool::Triangulation(toFace, toLocation);
        TopExp_Explorer fromEdges(fromFace, TopAbs_EDGE);
        TopExp_Explorer toEdges(toFace, TopAbs_EDGE);
        for (; fromEdges.More() && toEdges.More(); fromEdges.Next(), toEdges.Next()) {
            const TopoDS_Edge fromEdge = TopoDS::Edge(fromEdges.Current());
            const TopoDS_Edge toEdge = TopoDS::Edge(toEdges.Current());
            if (BRep_Tool::Degenerated(fromEdge)) {
                continue;
            }
            if (BRep_Tool::IsClosed(fromEdge, fromFace)) {
                // Una costura aparece dos veces en la cara (una por cada lado): se pasan los dos polígonos de una vez.
                if (fromEdges.Current().Orientation() != TopAbs_FORWARD) {
                    continue;  // ya se pasó con su pareja
                }
                const occ::handle<Poly_PolygonOnTriangulation> forward =
                    BRep_Tool::PolygonOnTriangulation(TopoDS::Edge(fromEdge.Oriented(TopAbs_FORWARD)), triangulation, fromLocation);
                const occ::handle<Poly_PolygonOnTriangulation> backward =
                    BRep_Tool::PolygonOnTriangulation(TopoDS::Edge(fromEdge.Oriented(TopAbs_REVERSED)), triangulation, fromLocation);
                if (!forward.IsNull() && !backward.IsNull()) {
                    builder.UpdateEdge(TopoDS::Edge(toEdge.Oriented(TopAbs_FORWARD)), forward, backward, triangulation, toLocation);
                }
                continue;
            }
            const occ::handle<Poly_PolygonOnTriangulation> polygon =
                BRep_Tool::PolygonOnTriangulation(fromEdge, triangulation, fromLocation);
            if (!polygon.IsNull()) {
                builder.UpdateEdge(toEdge, polygon, triangulation, toLocation);
            }
        }
    }
}

bool hasBoundaryPolygons(const Shape& shape)
{
    if (shape.isNull() || shape.faceCount() == 0) {
        return false;
    }
    for (int i = 0; i < shape.faceCount(); ++i) {
        const TopoDS_Face& face = faceAt(shape, i);
        TopLoc_Location location;
        const occ::handle<Poly_Triangulation> triangulation = BRep_Tool::Triangulation(face, location);
        if (triangulation.IsNull()) {
            return false;
        }
        for (TopExp_Explorer edges(face, TopAbs_EDGE); edges.More(); edges.Next()) {
            const TopoDS_Edge edge = TopoDS::Edge(edges.Current());
            if (!BRep_Tool::Degenerated(edge) && BRep_Tool::PolygonOnTriangulation(edge, triangulation, location).IsNull()) {
                return false;
            }
        }
    }
    return true;
}

bool hasMesh(const Shape& shape)
{
    if (shape.isNull() || shape.faceCount() == 0) {
        return false;
    }
    if (shape.impl()->meshed.load()) {
        return true;
    }
    for (int i = 0; i < shape.faceCount(); ++i) {
        TopLoc_Location location;
        if (BRep_Tool::Triangulation(faceAt(shape, i), location).IsNull()) {
            return false;
        }
    }
    shape.impl()->meshed.store(true);
    return true;
}

} // namespace nuf::geometry
