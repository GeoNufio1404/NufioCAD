#include <nuf/geometry/DisplayMesh.hpp>

#include <nuf/geometry/Occt.hpp>

#include <BRepAdaptor_Surface.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepLib_ToolTriangulatedShape.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <IMeshTools_Parameters.hxx>
#include <Message_ProgressIndicator.hxx>
#include <Message_ProgressRange.hxx>
#include <Standard_Failure.hxx>
#include <TopExp.hxx>
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
    // Suma de puntos de control de las caras spline (una rosca partida en tramos tiene miles).
    constexpr long kHeavyPoles = 2000;
    long poles = 0;
    for (int i = 0; i < shape.faceCount(); ++i) {
        const BRepAdaptor_Surface surface(faceAt(shape, i));
        if (surface.GetType() == GeomAbs_BSplineSurface) {
            poles += long(surface.NbUPoles()) * surface.NbVPoles();
        }
    }
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
        // Copia de la topología (la geometría se comparte: solo se lee).
        BRepBuilderAPI_Copy copier(occtShape(shape), false, false);
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
                BRepLib_ToolTriangulatedShape::ComputeNormals(face, triangulation);
            }
        }
        auto impl = std::make_shared<PreparedMesh::Impl>();
        impl->copy = copy;
        return PreparedMesh(std::move(impl));
    } catch (const Standard_Failure&) {
        return PreparedMesh(nullptr);
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
        TopLoc_Location location;
        const occ::handle<Poly_Triangulation> triangulation = BRep_Tool::Triangulation(TopoDS::Face(from(i)), location);
        if (!triangulation.IsNull()) {
            builder.UpdateFace(TopoDS::Face(to(i)), triangulation);
        }
    }
}

bool hasMesh(const Shape& shape)
{
    if (shape.isNull() || shape.faceCount() == 0) {
        return false;
    }
    for (int i = 0; i < shape.faceCount(); ++i) {
        TopLoc_Location location;
        if (BRep_Tool::Triangulation(faceAt(shape, i), location).IsNull()) {
            return false;
        }
    }
    return true;
}

} // namespace nuf::geometry
