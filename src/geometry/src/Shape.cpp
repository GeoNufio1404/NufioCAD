#include "Naming.hpp"

#include <nuf/geometry/Occt.hpp>

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBndLib.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>

#include <algorithm>
#include <cmath>
#include <set>
#include <tuple>

namespace nuf::geometry {

namespace {

const std::string kNoName;

// Centro de una cara o arista, redondeado para que el orden no dependa de
// errores de redondeo.
std::tuple<double, double, double> centerOf(const TopoDS_Shape& shape)
{
    GProp_GProps props;
    if (shape.ShapeType() == TopAbs_FACE) {
        BRepGProp::SurfaceProperties(shape, props);
    } else {
        BRepGProp::LinearProperties(shape, props);
    }
    const gp_Pnt c = props.CentreOfMass();
    const auto round = [](double v) { return std::round(v * 1e5) / 1e5; };
    return {round(c.X()), round(c.Y()), round(c.Z())};
}

// Índices con ese nombre, ordenados por su centro.
std::vector<int> sameName(const std::vector<std::string>& names, const ShapeMap& map, const std::string& name)
{
    std::vector<int> found;
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (names[i] == name) {
            found.push_back(int(i));
        }
    }
    if (found.size() > 1) {
        std::vector<std::pair<std::tuple<double, double, double>, int>> keyed;
        for (int i : found) {
            keyed.emplace_back(centerOf(map(i + 1)), i);
        }
        std::ranges::sort(keyed);
        for (std::size_t k = 0; k < keyed.size(); ++k) {
            found[k] = keyed[k].second;
        }
    }
    return found;
}

ElementRef refOf(const std::vector<std::string>& names, const ShapeMap& map, int index)
{
    if (index < 0 || index >= int(names.size())) {
        return {};
    }
    const std::vector<int> same = sameName(names, map, names[std::size_t(index)]);
    const auto it = std::ranges::find(same, index);
    return {names[std::size_t(index)], int(it - same.begin())};
}

std::optional<int> find(const std::vector<std::string>& names, const ShapeMap& map, const ElementRef& ref)
{
    const std::vector<int> same = sameName(names, map, ref.name);
    if (same.empty()) {
        return std::nullopt;
    }
    // Si ahora hay menos (se unieron), la última es la más parecida.
    return same[std::size_t(std::clamp(ref.occurrence, 0, int(same.size()) - 1))];
}

} // namespace

Shape::Shape(std::shared_ptr<const Impl> impl)
    : impl_(std::move(impl))
{
}

bool Shape::isNull() const { return !impl_ || impl_->shape.IsNull(); }

bool Shape::isValid() const
{
    if (isNull()) {
        return false;
    }
    if (!BRepCheck_Analyzer(impl_->shape).IsValid()) {
        return false;
    }
    // Cada cara con vecinas en todas sus aristas (sin bordes abiertos).
    for (TopExp_Explorer ex(impl_->shape, TopAbs_SHELL); ex.More(); ex.Next()) {
        if (!BRep_Tool::IsClosed(ex.Current())) {
            return false;
        }
    }
    return solidCount() > 0;
}

int Shape::solidCount() const
{
    int n = 0;
    if (!isNull()) {
        for (TopExp_Explorer ex(impl_->shape, TopAbs_SOLID); ex.More(); ex.Next()) {
            ++n;
        }
    }
    return n;
}

double Shape::volume() const
{
    if (isNull()) {
        return 0.0;
    }
    GProp_GProps props;
    BRepGProp::VolumeProperties(impl_->shape, props);
    return props.Mass();
}

Box Shape::bounds() const
{
    Box box;
    if (isNull()) {
        return box;
    }
    Bnd_Box bnd;
    // Caja ajustada (no la de los puntos de control de las superficies spline, que en una rosca
    // sale hasta un 10 % mayor); sin la malla, que puede no existir.
    BRepBndLib::AddOptimal(impl_->shape, bnd, false, false);
    if (bnd.IsVoid()) {
        return box;
    }
    double x0, y0, z0, x1, y1, z1;
    bnd.Get(x0, y0, z0, x1, y1, z1);
    box.min = {x0, y0, z0};
    box.max = {x1, y1, z1};
    box.empty = false;
    return box;
}

int Shape::faceCount() const { return impl_ ? int(impl_->faceNames.size()) : 0; }
int Shape::edgeCount() const { return impl_ ? int(impl_->edgeNames.size()) : 0; }

const std::string& Shape::faceName(int index) const
{
    return index >= 0 && index < faceCount() ? impl_->faceNames[std::size_t(index)] : kNoName;
}

const std::string& Shape::edgeName(int index) const
{
    return index >= 0 && index < edgeCount() ? impl_->edgeNames[std::size_t(index)] : kNoName;
}

ElementRef Shape::faceRef(int index) const
{
    return impl_ ? refOf(impl_->faceNames, impl_->faces, index) : ElementRef{};
}

ElementRef Shape::edgeRef(int index) const
{
    return impl_ ? refOf(impl_->edgeNames, impl_->edges, index) : ElementRef{};
}

std::optional<int> Shape::findFace(const ElementRef& ref) const
{
    return impl_ ? find(impl_->faceNames, impl_->faces, ref) : std::nullopt;
}

std::optional<int> Shape::findEdge(const ElementRef& ref) const
{
    return impl_ ? find(impl_->edgeNames, impl_->edges, ref) : std::nullopt;
}

std::optional<sketch::Plane> Shape::facePlane(int index) const
{
    if (index < 0 || index >= faceCount()) {
        return std::nullopt;
    }
    const TopoDS_Face& face = faceAt(*this, index);
    const BRepAdaptor_Surface surface(face);
    if (surface.GetType() != GeomAbs_Plane) {
        return std::nullopt;
    }
    const gp_Pln pln = surface.Plane();
    gp_Dir normal = pln.Axis().Direction();
    if (face.Orientation() == TopAbs_REVERSED) {
        normal.Reverse();
    }
    return sketch::Plane::fromPointAndNormal(toVec3(pln.Location().XYZ()), toVec3(normal.XYZ()));
}

Box Shape::faceBounds(int index) const
{
    Box box;
    if (index < 0 || index >= faceCount()) {
        return box;
    }
    Bnd_Box bnd;
    BRepBndLib::Add(faceAt(*this, index), bnd);
    if (bnd.IsVoid()) {
        return box;
    }
    double x0, y0, z0, x1, y1, z1;
    bnd.Get(x0, y0, z0, x1, y1, z1);
    box.min = {x0, y0, z0};
    box.max = {x1, y1, z1};
    box.empty = false;
    return box;
}

sketch::Vec3 Shape::faceCenter(int index) const
{
    if (index < 0 || index >= faceCount()) {
        return {};
    }
    GProp_GProps props;
    BRepGProp::SurfaceProperties(faceAt(*this, index), props);
    return toVec3(props.CentreOfMass().XYZ());
}

std::vector<sketch::Geometry> Shape::projectEdge(int index, const sketch::Plane& plane) const
{
    if (index < 0 || index >= edgeCount()) {
        return {};
    }
    return geometry::projectEdge(edgeAt(*this, index), plane);
}

std::optional<std::pair<sketch::Vec3, sketch::Vec3>> Shape::edgeLine(int index) const
{
    if (index < 0 || index >= edgeCount()) {
        return std::nullopt;
    }
    const BRepAdaptor_Curve curve(edgeAt(*this, index));
    if (curve.GetType() != GeomAbs_Line) {
        return std::nullopt;
    }
    const gp_Lin line = curve.Line();
    return std::pair{toVec3(line.Location().XYZ()), toVec3(line.Direction().XYZ())};
}

const TopoDS_Shape& occtShape(const Shape& shape)
{
    static const TopoDS_Shape kNull;
    return shape.impl() ? shape.impl()->shape : kNull;
}

std::optional<int> faceIndexOf(const Shape& shape, const TopoDS_Shape& face)
{
    const int i = shape.impl() ? shape.impl()->faces.FindIndex(face) : 0;
    return i > 0 ? std::optional(i - 1) : std::nullopt;
}

std::optional<int> edgeIndexOf(const Shape& shape, const TopoDS_Shape& edge)
{
    const int i = shape.impl() ? shape.impl()->edges.FindIndex(edge) : 0;
    return i > 0 ? std::optional(i - 1) : std::nullopt;
}

const TopoDS_Face& faceAt(const Shape& shape, int index)
{
    return TopoDS::Face(shape.impl()->faces(index + 1));
}

const TopoDS_Edge& edgeAt(const Shape& shape, int index)
{
    return TopoDS::Edge(shape.impl()->edges(index + 1));
}

// --- Namer ---------------------------------------------------------------

Namer::Namer(const TopoDS_Shape& result)
    : result_(result)
{
    if (!result.IsNull()) {
        TopExp::MapShapes(result, TopAbs_FACE, faces_);
    }
    names_.resize(std::size_t(faces_.Extent()));
}

void Namer::name(const TopoDS_Shape& faces, const std::string& name)
{
    for (TopExp_Explorer ex(faces, TopAbs_FACE); ex.More(); ex.Next()) {
        const int i = faces_.FindIndex(ex.Current());
        if (i == 0) {
            continue;
        }
        std::string& current = names_[std::size_t(i - 1)];
        if (current.empty() || name < current) {
            current = name;
        }
    }
}

void Namer::inherit(const Shape& input, const BRepTools_History& history)
{
    if (!input.impl()) {
        return;
    }
    const auto& in = *input.impl();
    for (int i = 1; i <= in.faces.Extent(); ++i) {
        const TopoDS_Shape& face = in.faces(i);
        const std::string& faceName = in.faceNames[std::size_t(i - 1)];
        if (history.IsRemoved(face)) {
            continue;
        }
        const auto& modified = history.Modified(face);
        if (modified.IsEmpty()) {
            name(face, faceName);
        }
        for (const TopoDS_Shape& image : modified) {
            name(image, faceName);
        }
    }
}

void Namer::inherit(const Shape& input)
{
    if (!input.impl()) {
        return;
    }
    const auto& in = *input.impl();
    for (int i = 1; i <= in.faces.Extent(); ++i) {
        name(in.faces(i), in.faceNames[std::size_t(i - 1)]);
    }
}

void Namer::nameUnnamed(const std::string& name)
{
    for (std::string& current : names_) {
        if (current.empty()) {
            current = name;
        }
    }
}

Shape Namer::finish(const std::string& fallbackTag) const
{
    auto impl = std::make_shared<Shape::Impl>();
    impl->shape = result_;
    impl->faces = faces_;
    impl->faceNames = names_;
    for (std::size_t i = 0; i < impl->faceNames.size(); ++i) {
        if (impl->faceNames[i].empty()) {
            impl->faceNames[i] = fallbackTag + ":?" + std::to_string(i);
        }
    }
    if (!result_.IsNull()) {
        TopExp::MapShapes(result_, TopAbs_EDGE, impl->edges);
    }
    // Arista = las caras que separa, ordenadas y sin repetir.
    std::vector<std::set<std::string>> adjacent(std::size_t(impl->edges.Extent()));
    for (int f = 1; f <= impl->faces.Extent(); ++f) {
        for (TopExp_Explorer ex(impl->faces(f), TopAbs_EDGE); ex.More(); ex.Next()) {
            const int e = impl->edges.FindIndex(ex.Current());
            if (e > 0) {
                adjacent[std::size_t(e - 1)].insert(impl->faceNames[std::size_t(f - 1)]);
            }
        }
    }
    impl->edgeNames.reserve(adjacent.size());
    for (const auto& faces : adjacent) {
        std::string joined;
        for (const std::string& face : faces) {
            joined += (joined.empty() ? "" : "|") + face;
        }
        impl->edgeNames.push_back(joined.empty() ? fallbackTag + ":?arista" : joined);
    }
    return Shape(std::move(impl));
}

} // namespace nuf::geometry
