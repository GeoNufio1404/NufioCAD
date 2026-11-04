#include <nuf/geometry/Exchange.hpp>

#include <nuf/geometry/Occt.hpp>

#include <BRepBuilderAPI_Copy.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRep_Tool.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <Poly_Triangulation.hxx>
#include <STEPControl_StepModelType.hxx>
#include <STEPControl_Writer.hxx>
#include <TopAbs_Orientation.hxx>
#include <TopExp_Explorer.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <BRep_Builder.hxx>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <locale>

namespace nuf::geometry {

namespace {

using Point = std::array<double, 3>;

struct Triangle {
    Point a, b, c;  // sentido antihorario visto desde fuera del sólido
};

std::vector<Triangle> triangulate(const std::vector<Shape>& shapes, const MeshQuality& quality)
{
    std::vector<Triangle> triangles;
    for (const Shape& shape : shapes) {
        if (shape.isNull()) {
            continue;
        }
        // La copia no lleva la malla del visor, y la que se calcule aquí no
        // se queda en la forma original.
        BRepBuilderAPI_Copy copy(occtShape(shape), true, false);
        BRepMesh_IncrementalMesh mesher(copy.Shape(), quality.linearDeflection, false,
                                        quality.angularDeflection, true);
        for (TopExp_Explorer faces(copy.Shape(), TopAbs_FACE); faces.More(); faces.Next()) {
            const TopoDS_Face face = TopoDS::Face(faces.Current());
            TopLoc_Location location;
            const auto mesh = BRep_Tool::Triangulation(face, location);
            if (mesh.IsNull()) {
                continue;
            }
            const bool reversed = face.Orientation() == TopAbs_REVERSED;
            const gp_Trsf transform = location.Transformation();
            const auto node = [&](int index) {
                gp_Pnt p = mesh->Node(index);
                p.Transform(transform);
                return Point{p.X(), p.Y(), p.Z()};
            };
            for (int i = 1; i <= mesh->NbTriangles(); ++i) {
                int a = 0, b = 0, c = 0;
                mesh->Triangle(i).Get(a, b, c);
                if (reversed) {
                    std::swap(b, c);
                }
                triangles.push_back({node(a), node(b), node(c)});
            }
        }
    }
    return triangles;
}

Point cross(const Point& u, const Point& v)
{
    return {u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]};
}

Point minus(const Point& p, const Point& q)
{
    return {p[0] - q[0], p[1] - q[1], p[2] - q[2]};
}

// Normal unitaria (cero si el triángulo es degenerado).
Point normalOf(const Triangle& t)
{
    Point n = cross(minus(t.b, t.a), minus(t.c, t.a));
    const double length = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    if (length > 0.0) {
        n = {n[0] / length, n[1] / length, n[2] / length};
    }
    return n;
}

std::string utf8(const std::filesystem::path& path)
{
    const std::u8string text = path.u8string();
    return {text.begin(), text.end()};
}

std::ofstream openForWriting(const std::filesystem::path& path)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        throw GeometryError("No se pudo crear el archivo " + utf8(path));
    }
    return out;
}

void requireShapes(const std::vector<Shape>& shapes)
{
    for (const Shape& shape : shapes) {
        if (!shape.isNull()) {
            return;
        }
    }
    throw GeometryError("No hay ningún cuerpo que exportar");
}

void finish(std::ofstream& out, const std::filesystem::path& path)
{
    out.flush();
    if (!out) {
        throw GeometryError("No se pudo escribir el archivo " + utf8(path));
    }
}

template <typename T>
void putLittleEndian(std::ofstream& out, T value)
{
    static_assert(std::endian::native == std::endian::little, "STL binario asume little-endian");
    out.write(reinterpret_cast<const char*>(&value), sizeof(T));
}

} // namespace

std::size_t countTriangles(const std::vector<Shape>& shapes, const MeshQuality& quality)
{
    return triangulate(shapes, quality).size();
}

double meshVolume(const std::vector<Shape>& shapes, const MeshQuality& quality)
{
    double volume = 0.0;
    for (const Triangle& t : triangulate(shapes, quality)) {
        const Point n = cross(t.b, t.c);
        volume += (t.a[0] * n[0] + t.a[1] * n[1] + t.a[2] * n[2]) / 6.0;
    }
    return volume;
}

void writeStl(const std::filesystem::path& path, const std::vector<Shape>& shapes, const MeshQuality& quality)
{
    requireShapes(shapes);
    const std::vector<Triangle> triangles = triangulate(shapes, quality);
    if (triangles.empty()) {
        throw GeometryError("No se pudo mallar el cuerpo");
    }
    std::ofstream out = openForWriting(path);
    std::array<char, 80> header{};
    constexpr char kTitle[] = "NufioCAD STL (mm)";
    std::memcpy(header.data(), kTitle, sizeof(kTitle) - 1);
    out.write(header.data(), header.size());
    putLittleEndian<std::uint32_t>(out, static_cast<std::uint32_t>(triangles.size()));
    for (const Triangle& t : triangles) {
        const Point n = normalOf(t);
        for (const Point* p : {&n, &t.a, &t.b, &t.c}) {
            for (double v : *p) {
                putLittleEndian<float>(out, static_cast<float>(v));
            }
        }
        putLittleEndian<std::uint16_t>(out, 0);
    }
    finish(out, path);
}

void writeObj(const std::filesystem::path& path, const std::vector<Shape>& shapes, const MeshQuality& quality)
{
    requireShapes(shapes);
    const std::vector<Triangle> triangles = triangulate(shapes, quality);
    if (triangles.empty()) {
        throw GeometryError("No se pudo mallar el cuerpo");
    }
    std::ofstream out = openForWriting(path);
    out.imbue(std::locale::classic());
    out.precision(8);
    out << "# NufioCAD (mm)\n";
    // Cada triángulo con sus tres vértices y su normal; sin soldar vértices
    // (los slicers lo hacen al importar) para no mezclar aristas vivas.
    for (const Triangle& t : triangles) {
        const Point n = normalOf(t);
        out << "vn " << n[0] << ' ' << n[1] << ' ' << n[2] << '\n';
        for (const Point* p : {&t.a, &t.b, &t.c}) {
            out << "v " << (*p)[0] << ' ' << (*p)[1] << ' ' << (*p)[2] << '\n';
        }
    }
    for (std::size_t i = 0; i < triangles.size(); ++i) {
        const std::size_t v = 3 * i + 1;
        const std::size_t n = i + 1;
        out << "f " << v << "//" << n << ' ' << v + 1 << "//" << n << ' ' << v + 2 << "//" << n << '\n';
    }
    finish(out, path);
}

void writeStep(const std::filesystem::path& path, const std::vector<Shape>& shapes)
{
    requireShapes(shapes);
    TopoDS_Compound compound;
    BRep_Builder builder;
    builder.MakeCompound(compound);
    const TopoDS_Shape* single = nullptr;
    int count = 0;
    for (const Shape& shape : shapes) {
        if (!shape.isNull()) {
            builder.Add(compound, occtShape(shape));
            single = &occtShape(shape);
            ++count;
        }
    }
    // Un solo cuerpo se exporta tal cual, sin envolverlo en un compuesto.
    const TopoDS_Shape& whole = count == 1 ? *single : static_cast<const TopoDS_Shape&>(compound);

    STEPControl_Writer writer;
    if (writer.Transfer(whole, STEPControl_AsIs) != IFSelect_RetDone) {
        throw GeometryError("No se pudo convertir el cuerpo a STEP");
    }
    // Por flujo, no por nombre: así las rutas con acentos funcionan en Windows.
    std::ofstream out = openForWriting(path);
    if (writer.WriteStream(out) != IFSelect_RetDone) {
        throw GeometryError("No se pudo escribir el archivo STEP");
    }
    finish(out, path);
}

} // namespace nuf::geometry
