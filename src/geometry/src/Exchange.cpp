#include <nuf/geometry/Exchange.hpp>

#include <nuf/geometry/Occt.hpp>

#include "Naming.hpp"

#include <BRepBuilderAPI_Copy.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeSolid.hxx>
#include <BRepBuilderAPI_Sewing.hxx>
#include <BRepLib.hxx>
#include <BinTools.hxx>
#include <BinTools_FormatVersion.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRep_Tool.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <Poly_Triangulation.hxx>
#include <STEPControl_StepModelType.hxx>
#include <STEPControl_Reader.hxx>
#include <NCollection_Sequence.hxx>
#include <TCollection_AsciiString.hxx>
#include <Standard_Failure.hxx>
#include <STEPControl_Writer.hxx>
#include <Standard_Version.hxx>
#include <ShapeFix_Shape.hxx>
#include <TopoDS_Solid.hxx>
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
#include <map>
#include <cstdio>
#include <fstream>
#include <locale>
#include <sstream>
#include <system_error>

namespace nuf::geometry {

namespace {

using Point = std::array<double, 3>;

struct Triangle {
    Point a, b, c;  // sentido antihorario visto desde fuera del sólido
};

// Malla cada forma y recorre sus caras, UNA FORMA A LA VEZ: la copia mallada se suelta antes de pasar a la siguiente. Antes
// se guardaban todos los triángulos de todas las formas en un vector (72 bytes cada uno, más de 150 MB con dos millones de
// triángulos) solo para contarlos o escribirlos.
template <typename FaceVisitor>
void forEachMeshedFace(const std::vector<Shape>& shapes, const MeshQuality& quality, FaceVisitor&& visit)
{
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
            visit(*mesh, location.Transformation(), face.Orientation() == TopAbs_REVERSED);
        }
    }
}

// Cada triángulo mallado, sin guardarlos.
template <typename TriangleVisitor>
void forEachTriangle(const std::vector<Shape>& shapes, const MeshQuality& quality, TriangleVisitor&& visit)
{
    forEachMeshedFace(shapes, quality, [&](const Poly_Triangulation& mesh, const gp_Trsf& transform, bool reversed) {
        const auto node = [&](int index) {
            gp_Pnt p = mesh.Node(index);
            p.Transform(transform);
            return Point{p.X(), p.Y(), p.Z()};
        };
        for (int i = 1; i <= mesh.NbTriangles(); ++i) {
            int a = 0, b = 0, c = 0;
            mesh.Triangle(i).Get(a, b, c);
            if (reversed) {
                std::swap(b, c);
            }
            visit(Triangle{node(a), node(b), node(c)});
        }
    });
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
    std::size_t count = 0;
    forEachMeshedFace(shapes, quality, [&](const Poly_Triangulation& mesh, const gp_Trsf&, bool) { count += std::size_t(mesh.NbTriangles()); });
    return count;
}

double meshVolume(const std::vector<Shape>& shapes, const MeshQuality& quality)
{
    double volume = 0.0;
    forEachTriangle(shapes, quality, [&](const Triangle& t) {
        const Point n = cross(t.b, t.c);
        volume += (t.a[0] * n[0] + t.a[1] * n[1] + t.a[2] * n[2]) / 6.0;
    });
    return volume;
}

OverhangReport analyzeOverhang(const Shape& shape, const MeshQuality& quality, double maxAngleDegrees)
{
    OverhangReport report;
    if (shape.isNull()) {
        return report;
    }
    const double bedZ = shape.bounds().min.z;
    // Una cara se apoya en la cama si está pegada a la Z mínima; las demás miran hacia abajo «de más» si su normal baja de
    // −sen(ángulo): con 45°, la normal tiene que ser más vertical que −0,707.
    const double limit = -std::sin(maxAngleDegrees * 3.14159265358979323846 / 180.0);
    forEachTriangle({shape}, quality, [&](const Triangle& t) {
        const Point ab{t.b[0] - t.a[0], t.b[1] - t.a[1], t.b[2] - t.a[2]};
        const Point ac{t.c[0] - t.a[0], t.c[1] - t.a[1], t.c[2] - t.a[2]};
        const Point n = cross(ab, ac);
        const double twice = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        if (twice < 1e-12) {
            return;
        }
        const double area = 0.5 * twice;
        report.totalArea += area;
        const double nz = n[2] / twice;
        const double top = std::max({t.a[2], t.b[2], t.c[2]});
        if (nz < limit && top > bedZ + 1e-3) {
            report.overhangArea += area;
        }
    });
    return report;
}

MeshReport analyzeMesh(const Shape& shape, const MeshQuality& quality)
{
    MeshReport report;
    if (shape.isNull()) {
        return report;
    }
    std::map<std::array<long long, 3>, std::size_t> index;
    const auto weld = [&](const Point& p) {
        const std::array<long long, 3> key{std::llround(p[0] * 1e5), std::llround(p[1] * 1e5), std::llround(p[2] * 1e5)};
        return index.emplace(key, index.size()).first->second;
    };
    // Cuántas veces se recorre cada arista en cada sentido.
    std::map<std::pair<std::size_t, std::size_t>, int> directed;
    forEachTriangle({shape}, quality, [&](const Triangle& t) {
        ++report.triangles;
        const Point n = cross(t.b, t.c);
        report.meshVolume += (t.a[0] * n[0] + t.a[1] * n[1] + t.a[2] * n[2]) / 6.0;
        const std::size_t v[3] = {weld(t.a), weld(t.b), weld(t.c)};
        // Triángulo degenerado: dos vértices soldados juntos o área casi nula.
        const Point ab{t.b[0] - t.a[0], t.b[1] - t.a[1], t.b[2] - t.a[2]};
        const Point ac{t.c[0] - t.a[0], t.c[1] - t.a[1], t.c[2] - t.a[2]};
        const Point cr = cross(ab, ac);
        const double twice = std::sqrt(cr[0] * cr[0] + cr[1] * cr[1] + cr[2] * cr[2]);
        if (v[0] == v[1] || v[1] == v[2] || v[0] == v[2] || twice < 1e-9) {
            ++report.degenerateTriangles;
            return;
        }
        for (int k = 0; k < 3; ++k) {
            ++directed[{v[k], v[(k + 1) % 3]}];
        }
    });
    report.solidVolume = shape.volume();
    for (const auto& [edge, count] : directed) {
        const auto opposite = directed.find({edge.second, edge.first});
        const int back = opposite == directed.end() ? 0 : opposite->second;
        if (count > 1) {
            ++report.flippedEdges;  // el mismo sentido dos veces
        }
        if (edge.first < edge.second || opposite == directed.end()) {
            const int total = count + back;
            if (total == 1) {
                ++report.openEdges;
            } else if (total > 2) {
                ++report.nonManifoldEdges;
            }
        }
    }
    return report;
}

void writeStl(const std::filesystem::path& path, const std::vector<Shape>& shapes, const MeshQuality& quality)
{
    requireShapes(shapes);
    std::ofstream out = openForWriting(path);
    try {
        std::array<char, 80> header{};
        constexpr char kTitle[] = "NufioCAD STL (mm)";
        std::memcpy(header.data(), kTitle, sizeof(kTitle) - 1);
        out.write(header.data(), header.size());
        // El número de triángulos va antes que ellos: se deja en cero y se corrige al terminar (así no hace falta
        // guardar la malla entera para saber cuántos son).
        putLittleEndian<std::uint32_t>(out, 0);
        std::uint32_t count = 0;
        forEachTriangle(shapes, quality, [&](const Triangle& t) {
            const Point n = normalOf(t);
            for (const Point* p : {&n, &t.a, &t.b, &t.c}) {
                for (double v : *p) {
                    putLittleEndian<float>(out, static_cast<float>(v));
                }
            }
            putLittleEndian<std::uint16_t>(out, 0);
            ++count;
        });
        if (count == 0) {
            throw GeometryError("No se pudo mallar el cuerpo");
        }
        out.seekp(static_cast<std::streamoff>(header.size()));
        putLittleEndian<std::uint32_t>(out, count);
        out.seekp(0, std::ios::end);
        finish(out, path);
    } catch (...) {
        out.close();
        std::error_code ignored;
        std::filesystem::remove(path, ignored);  // sin archivo a medias
        throw;
    }
}

void writeObj(const std::filesystem::path& path, const std::vector<Shape>& shapes, const MeshQuality& quality)
{
    requireShapes(shapes);
    std::ofstream out = openForWriting(path);
    try {
        out.imbue(std::locale::classic());
        out.precision(8);
        out << "# NufioCAD (mm)\n";
        // Cada triángulo con sus tres vértices y su normal; sin soldar vértices
        // (los slicers lo hacen al importar) para no mezclar aristas vivas. Los triángulos no se guardan: se escriben
        // según salen, y las caras (que solo dependen del número de triángulos) se escriben al final.
        std::size_t count = 0;
        forEachTriangle(shapes, quality, [&](const Triangle& t) {
            const Point n = normalOf(t);
            out << "vn " << n[0] << ' ' << n[1] << ' ' << n[2] << '\n';
            for (const Point* p : {&t.a, &t.b, &t.c}) {
                out << "v " << (*p)[0] << ' ' << (*p)[1] << ' ' << (*p)[2] << '\n';
            }
            ++count;
        });
        if (count == 0) {
            throw GeometryError("No se pudo mallar el cuerpo");
        }
        for (std::size_t i = 0; i < count; ++i) {
            const std::size_t v = 3 * i + 1;
            const std::size_t n = i + 1;
            out << "f " << v << "//" << n << ' ' << v + 1 << "//" << n << ' ' << v + 2 << "//" << n << '\n';
        }
        finish(out, path);
    } catch (...) {
        out.close();
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
        throw;
    }
}

namespace {

// --- 3MF: un ZIP sin comprimir con tres archivos de texto --------------------------------------------------

std::uint32_t crc32Of(const std::string& data)
{
    static const auto table = [] {
        std::array<std::uint32_t, 256> t{};
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k) {
                c = (c & 1U) != 0 ? 0xEDB88320U ^ (c >> 1) : c >> 1;
            }
            t[i] = c;
        }
        return t;
    }();
    std::uint32_t crc = 0xFFFFFFFFU;
    for (const char ch : data) {
        crc = table[(crc ^ static_cast<unsigned char>(ch)) & 0xFFU] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFU;
}

void put16(std::string& out, std::uint32_t v)
{
    out.push_back(char(v & 0xFF));
    out.push_back(char((v >> 8) & 0xFF));
}

void put32(std::string& out, std::uint32_t v)
{
    put16(out, v & 0xFFFF);
    put16(out, (v >> 16) & 0xFFFF);
}

std::string zipStored(const std::vector<std::pair<std::string, std::string>>& files)
{
    std::string out;
    std::string directory;
    for (const auto& [name, data] : files) {
        const std::uint32_t crc = crc32Of(data);
        const std::uint32_t offset = std::uint32_t(out.size());
        // Cabecera local: versión 2.0, sin compresión, fecha 1980-01-01.
        put32(out, 0x04034B50U);
        put16(out, 20);
        put16(out, 0x0800);  // nombres en UTF-8
        put16(out, 0);
        put16(out, 0);
        put16(out, 0x0021);
        put32(out, crc);
        put32(out, std::uint32_t(data.size()));
        put32(out, std::uint32_t(data.size()));
        put16(out, std::uint32_t(name.size()));
        put16(out, 0);
        out += name;
        out += data;
        put32(directory, 0x02014B50U);
        put16(directory, 20);
        put16(directory, 20);
        put16(directory, 0x0800);
        put16(directory, 0);
        put16(directory, 0);
        put16(directory, 0x0021);
        put32(directory, crc);
        put32(directory, std::uint32_t(data.size()));
        put32(directory, std::uint32_t(data.size()));
        put16(directory, std::uint32_t(name.size()));
        put16(directory, 0);
        put16(directory, 0);
        put16(directory, 0);
        put16(directory, 0);
        put32(directory, 0);
        put32(directory, offset);
        directory += name;
    }
    const std::uint32_t directoryOffset = std::uint32_t(out.size());
    out += directory;
    put32(out, 0x06054B50U);
    put16(out, 0);
    put16(out, 0);
    put16(out, std::uint32_t(files.size()));
    put16(out, std::uint32_t(files.size()));
    put32(out, std::uint32_t(directory.size()));
    put32(out, directoryOffset);
    put16(out, 0);
    return out;
}

std::string xmlEscape(const std::string& text)
{
    std::string out;
    for (const char c : text) {
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        default: out.push_back(c); break;
        }
    }
    return out;
}

// Los colores de fábrica de los filamentos, en el orden de los cuerpos.
constexpr std::uint32_t kPalette[] = {0xE0592A, 0x2F80ED, 0x27AE60, 0xF2C94C, 0x9B51E0, 0xEB5757, 0x56CCF2, 0x4F4F4F};

} // namespace

void writeThreeMf(const std::filesystem::path& path, const std::vector<ThreeMfPart>& parts, const MeshQuality& quality)
{
    std::vector<const ThreeMfPart*> used;
    for (const ThreeMfPart& part : parts) {
        if (!part.shape.isNull()) {
            used.push_back(&part);
        }
    }
    if (used.empty()) {
        throw GeometryError("No hay ningún cuerpo que exportar");
    }
    std::ostringstream model;
    model.imbue(std::locale::classic());
    model.precision(8);
    model << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
             "<model unit=\"millimeter\" xml:lang=\"es-ES\" xmlns=\"http://schemas.microsoft.com/3dmanufacturing/core/2015/02\">\n"
             " <metadata name=\"Application\">NufioCAD</metadata>\n <resources>\n  <basematerials id=\"1\">\n";
    for (std::size_t i = 0; i < used.size(); ++i) {
        const std::uint32_t rgb = used[i]->rgb != 0 ? used[i]->rgb : kPalette[i % std::size(kPalette)];
        char color[16];
        std::snprintf(color, sizeof color, "#%06XFF", rgb & 0xFFFFFFU);
        model << "   <base name=\"Filamento " << i + 1 << "\" displaycolor=\"" << color << "\"/>\n";
    }
    model << "  </basematerials>\n";
    for (std::size_t i = 0; i < used.size(); ++i) {
        // Se sueldan los vértices (los que caen en el mismo sitio a 1e-5 mm) para que la malla sea indexada y cerrada.
        std::map<std::array<long long, 3>, std::size_t> index;
        std::vector<Point> vertices;
        std::vector<std::array<std::size_t, 3>> triangles;
        const auto weld = [&](const Point& p) {
            const std::array<long long, 3> key{std::llround(p[0] * 1e5), std::llround(p[1] * 1e5), std::llround(p[2] * 1e5)};
            const auto [it, added] = index.emplace(key, vertices.size());
            if (added) {
                vertices.push_back(p);
            }
            return it->second;
        };
        forEachTriangle({used[i]->shape}, quality, [&](const Triangle& t) {
            const std::size_t a = weld(t.a), b = weld(t.b), c = weld(t.c);
            if (a != b && b != c && a != c) {
                triangles.push_back({a, b, c});
            }
        });
        if (triangles.empty()) {
            throw GeometryError("No se pudo mallar el cuerpo «" + used[i]->name + "»");
        }
        model << "  <object id=\"" << i + 2 << "\" type=\"model\" pid=\"1\" pindex=\"" << i << "\" name=\"" << xmlEscape(used[i]->name)
              << "\">\n   <mesh>\n    <vertices>\n";
        for (const Point& p : vertices) {
            model << "     <vertex x=\"" << p[0] << "\" y=\"" << p[1] << "\" z=\"" << p[2] << "\"/>\n";
        }
        model << "    </vertices>\n    <triangles>\n";
        for (const auto& tri : triangles) {
            model << "     <triangle v1=\"" << tri[0] << "\" v2=\"" << tri[1] << "\" v3=\"" << tri[2] << "\"/>\n";
        }
        model << "    </triangles>\n   </mesh>\n  </object>\n";
    }
    model << " </resources>\n <build>\n";
    for (std::size_t i = 0; i < used.size(); ++i) {
        model << "  <item objectid=\"" << i + 2 << "\"/>\n";
    }
    model << " </build>\n</model>\n";

    const std::string types =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">\n"
        " <Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>\n"
        " <Default Extension=\"model\" ContentType=\"application/vnd.ms-package.3dmanufacturing-3dmodel+xml\"/>\n"
        "</Types>\n";
    const std::string rels =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">\n"
        " <Relationship Target=\"/3D/3dmodel.model\" Id=\"rel0\" "
        "Type=\"http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel\"/>\n"
        "</Relationships>\n";
    const std::string archive = zipStored({{"[Content_Types].xml", types}, {"_rels/.rels", rels}, {"3D/3dmodel.model", model.str()}});
    std::ofstream out = openForWriting(path);
    out.write(archive.data(), std::streamsize(archive.size()));
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
#if OCC_VERSION_HEX >= 0x070700
    // Por flujo, no por nombre: así las rutas con acentos funcionan en Windows.
    std::ofstream out = openForWriting(path);
    if (writer.WriteStream(out) != IFSelect_RetDone) {
        throw GeometryError("No se pudo escribir el archivo STEP");
    }
    finish(out, path);
#else
    // OCCT < 7.7 no tiene WriteStream: se escribe por nombre (en Linux la ruta UTF-8 sirve tal cual).
    if (writer.Write(utf8(path).c_str()) != IFSelect_RetDone) {
        throw GeometryError("No se pudo escribir el archivo STEP");
    }
#endif
}

StepImport readStep(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw GeometryError("No se pudo abrir el archivo " + utf8(path));
    }
    STEPControl_Reader reader;
    if (reader.ReadStream(utf8(path).c_str(), in) != IFSelect_RetDone) {
        throw GeometryError("No se pudo leer el archivo STEP: no es válido o está dañado");
    }
    if (reader.TransferRoots() <= 0) {
        throw GeometryError("El archivo STEP no contiene geometría que se pueda importar");
    }
    TopoDS_Shape whole = reader.OneShape();
    if (whole.IsNull()) {
        throw GeometryError("El archivo STEP no contiene geometría que se pueda importar");
    }

    StepImport result;
    // Las unidades del archivo: el lector las convierte a milímetros (xstep.cascade.unit); si no eran milímetros, se dice (O6).
    {
        NCollection_Sequence<TCollection_AsciiString> lengthUnits;
        NCollection_Sequence<TCollection_AsciiString> angleUnits;
        NCollection_Sequence<TCollection_AsciiString> solidAngleUnits;
        reader.FileUnits(lengthUnits, angleUnits, solidAngleUnits);
        if (!lengthUnits.IsEmpty()) {
            std::string unit = lengthUnits.First().ToCString();
            std::transform(unit.begin(), unit.end(), unit.begin(), [](unsigned char c) { return char(std::toupper(c)); });
            if (unit != "MM" && unit != "MILLIMETRE" && unit != "MILLIMETER" && !unit.empty()) {
                const bool inches = unit.find("INCH") != std::string::npos;
                result.warnings.push_back(std::string("El archivo estaba en ") + (inches ? "pulgadas" : "«" + unit + "»") +
                                          ": se convirtió a milímetros al importarlo.");
            }
        }
    }
    ShapeFix_Shape fixer(whole);
    fixer.Perform();
    if (!fixer.Shape().IsNull()) {
        whole = fixer.Shape();
    }

    // Solo los sólidos: las superficies sueltas no sirven para modelar.
    TopoDS_Compound solids;
    BRep_Builder builder;
    builder.MakeCompound(solids);
    for (TopExp_Explorer ex(whole, TopAbs_SOLID); ex.More(); ex.Next()) {
        builder.Add(solids, ex.Current());
        ++result.solids;
    }
    if (result.solids == 0) {
        throw GeometryError("El archivo STEP no contiene sólidos (solo superficies o curvas)");
    }
    if (TopExp_Explorer(whole, TopAbs_SHELL, TopAbs_SOLID).More() || TopExp_Explorer(whole, TopAbs_FACE, TopAbs_SHELL).More()) {
        result.warnings.push_back("Se ignoraron superficies sueltas que no forman un sólido.");
    }

    std::ostringstream out(std::ios::binary);
    BinTools::Write(solids, out, false, false, BinTools_FormatVersion_CURRENT);
    result.brep = out.str();
    if (result.brep.empty()) {
        throw GeometryError("No se pudo preparar el sólido importado");
    }
    return result;
}

namespace {

constexpr std::size_t kMaxMeshTriangles = 30000;

struct RawMesh {
    std::vector<std::array<double, 3>> points;
    std::vector<std::array<std::size_t, 3>> triangles;  // índices en points
};

// STL binario (80 bytes de cabecera, el número de triángulos y 50 bytes por triángulo) o de texto («vertex x y z»).
RawMesh readStlFile(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw GeometryError("No se pudo abrir el archivo " + utf8(path));
    }
    std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    RawMesh mesh;
    std::map<std::array<long long, 3>, std::size_t> welded;
    // Soldar: los vértices que coinciden (a 1e-5 mm) son uno solo, porque el STL repite cada vértice en cada triángulo.
    const auto vertex = [&](double x, double y, double z) {
        const std::array<long long, 3> key{std::llround(x * 1e5), std::llround(y * 1e5), std::llround(z * 1e5)};
        const auto [it, inserted] = welded.try_emplace(key, mesh.points.size());
        if (inserted) {
            mesh.points.push_back({x, y, z});
        }
        return it->second;
    };
    const auto flush = [&](const std::array<std::size_t, 3>& t) {
        if (t[0] != t[1] && t[1] != t[2] && t[0] != t[2]) {
            mesh.triangles.push_back(t);
        }
    };
    const auto floatAt = [&](std::size_t offset) {
        float value = 0.0f;
        std::memcpy(&value, bytes.data() + offset, sizeof(float));
        return double(value);
    };
    bool binary = false;
    if (bytes.size() >= 84) {
        std::uint32_t count = 0;
        std::memcpy(&count, bytes.data() + 80, sizeof(count));
        binary = bytes.size() == 84 + std::size_t(count) * 50;
        if (binary && count > kMaxMeshTriangles * 2) {
            throw GeometryError("La malla tiene demasiados triángulos (" + std::to_string(count) +
                                "): cada uno sería una cara del sólido. Simplifícala antes de importarla");
        }
    }
    if (binary) {
        std::uint32_t count = 0;
        std::memcpy(&count, bytes.data() + 80, sizeof(count));
        for (std::uint32_t i = 0; i < count; ++i) {
            const std::size_t base = 84 + std::size_t(i) * 50 + 12;  // se salta la normal
            std::array<std::size_t, 3> t{};
            for (int k = 0; k < 3; ++k) {
                const std::size_t at = base + std::size_t(k) * 12;
                t[std::size_t(k)] = vertex(floatAt(at), floatAt(at + 4), floatAt(at + 8));
            }
            flush(t);
        }
    } else {
        std::istringstream text(bytes);
        std::string word;
        std::array<std::size_t, 3> t{};
        int filled = 0;
        while (text >> word) {
            if (word == "vertex") {
                double x = 0.0, y = 0.0, z = 0.0;
                if (!(text >> x >> y >> z)) {
                    throw GeometryError("El archivo STL está dañado: un «vertex» sin sus tres coordenadas");
                }
                t[std::size_t(filled++)] = vertex(x, y, z);
                if (filled == 3) {
                    flush(t);
                    filled = 0;
                }
            }
        }
    }
    return mesh;
}

// OBJ: «v x y z» y «f a b c …» (índices desde 1, o negativos desde el final; con «/» se toma el primer número); un polígono
// de más de tres vértices se abre en abanico.
RawMesh readObjFile(const std::filesystem::path& path)
{
    std::ifstream in(path);
    if (!in) {
        throw GeometryError("No se pudo abrir el archivo " + utf8(path));
    }
    RawMesh mesh;
    std::vector<std::array<double, 3>> raw;
    std::map<std::array<long long, 3>, std::size_t> welded;
    std::vector<std::size_t> mapped;  // índice del OBJ → índice soldado
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream words(line);
        std::string kind;
        words >> kind;
        if (kind == "v") {
            double x = 0.0, y = 0.0, z = 0.0;
            words >> x >> y >> z;
            const std::array<long long, 3> key{std::llround(x * 1e5), std::llround(y * 1e5), std::llround(z * 1e5)};
            const auto [it, inserted] = welded.try_emplace(key, mesh.points.size());
            if (inserted) {
                mesh.points.push_back({x, y, z});
            }
            mapped.push_back(it->second);
        } else if (kind == "f") {
            std::vector<std::size_t> face;
            std::string token;
            while (words >> token) {
                long index = std::strtol(token.c_str(), nullptr, 10);
                if (index < 0) {
                    index += long(mapped.size()) + 1;
                }
                if (index < 1 || std::size_t(index) > mapped.size()) {
                    throw GeometryError("El archivo OBJ está dañado: una cara usa un vértice que no existe");
                }
                face.push_back(mapped[std::size_t(index) - 1]);
            }
            for (std::size_t i = 2; i < face.size(); ++i) {
                const std::array<std::size_t, 3> t{face[0], face[i - 1], face[i]};
                if (t[0] != t[1] && t[1] != t[2] && t[0] != t[2]) {
                    mesh.triangles.push_back(t);
                }
            }
        }
    }
    return mesh;
}

} // namespace

StepImport readMesh(const std::filesystem::path& path)
{
    std::string extension = utf8(path.extension());
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    const RawMesh mesh = extension == ".obj" ? readObjFile(path) : readStlFile(path);
    if (mesh.triangles.empty()) {
        throw GeometryError("El archivo no tiene triángulos que se puedan importar");
    }
    if (mesh.triangles.size() > kMaxMeshTriangles) {
        throw GeometryError("La malla tiene demasiados triángulos (" + std::to_string(mesh.triangles.size()) +
                            "): cada uno sería una cara del sólido y el programa se pondría muy lento. Simplifícala antes de importarla");
    }
    try {
        // El tamaño de la malla fija la tolerancia con que se cosen las caras.
        double lo[3] = {1e300, 1e300, 1e300}, hi[3] = {-1e300, -1e300, -1e300};
        for (const auto& p : mesh.points) {
            for (int k = 0; k < 3; ++k) {
                lo[k] = std::min(lo[k], p[std::size_t(k)]);
                hi[k] = std::max(hi[k], p[std::size_t(k)]);
            }
        }
        const double diagonal = std::sqrt((hi[0] - lo[0]) * (hi[0] - lo[0]) + (hi[1] - lo[1]) * (hi[1] - lo[1]) + (hi[2] - lo[2]) * (hi[2] - lo[2]));
        BRepBuilderAPI_Sewing sewing(std::max(1e-6, diagonal * 1e-7));
        int faces = 0;
        for (const auto& t : mesh.triangles) {
            const auto pnt = [&](std::size_t i) { return gp_Pnt(mesh.points[i][0], mesh.points[i][1], mesh.points[i][2]); };
            BRepBuilderAPI_MakePolygon polygon(pnt(t[0]), pnt(t[1]), pnt(t[2]), true);
            if (!polygon.IsDone()) {
                continue;
            }
            BRepBuilderAPI_MakeFace face(polygon.Wire(), true);
            if (face.IsDone()) {
                sewing.Add(face.Face());
                ++faces;
            }
        }
        if (faces == 0) {
            throw GeometryError("Los triángulos del archivo son todos degenerados (de área cero)");
        }
        sewing.Perform();
        const TopoDS_Shape sewed = sewing.SewedShape();
        TopoDS_Compound solids;
        BRep_Builder builder;
        builder.MakeCompound(solids);
        StepImport result;
        int open = 0;
        for (TopExp_Explorer ex(sewed, TopAbs_SHELL); ex.More(); ex.Next()) {
            const TopoDS_Shell shell = TopoDS::Shell(ex.Current());
            if (!BRep_Tool::IsClosed(shell)) {
                ++open;
                continue;
            }
            BRepBuilderAPI_MakeSolid maker(shell);
            if (!maker.IsDone()) {
                ++open;
                continue;
            }
            TopoDS_Solid solid = maker.Solid();
            BRepLib::OrientClosedSolid(solid);
            builder.Add(solids, solid);
            ++result.solids;
        }
        if (result.solids == 0) {
            throw GeometryError("La malla no es hermética (tiene huecos o aristas sueltas), así que no se puede convertir en un sólido. "
                                "Repárala antes (por ejemplo, en el programa donde la hiciste)");
        }
        if (open > 0) {
            result.warnings.push_back("Se ignoraron " + std::to_string(open) + " partes de la malla que no estaban cerradas.");
        }
        result.warnings.push_back("Una malla no lleva unidades: se tomó como milímetros. Cada triángulo es una cara del sólido (" +
                                  std::to_string(faces) + " caras).");
        std::ostringstream out(std::ios::binary);
        BinTools::Write(solids, out, false, false, BinTools_FormatVersion_CURRENT);
        result.brep = out.str();
        if (result.brep.empty()) {
            throw GeometryError("No se pudo preparar el sólido importado");
        }
        return result;
    } catch (const Standard_Failure& e) {
        throw GeometryError(std::string("No se pudo convertir la malla en un sólido: ") + e.what());
    }
}

std::vector<Shape> shapesFromBrep(const std::string& brep, const std::string& tag)
{
    TopoDS_Shape compound;
    try {
        std::istringstream in(brep, std::ios::binary);
        BinTools::Read(compound, in);
    } catch (const Standard_Failure&) {
        throw GeometryError("Los datos del sólido importado están dañados");
    }
    if (compound.IsNull()) {
        throw GeometryError("Los datos del sólido importado están dañados");
    }
    std::vector<TopoDS_Shape> solids;
    for (TopExp_Explorer ex(compound, TopAbs_SOLID); ex.More(); ex.Next()) {
        solids.push_back(ex.Current());
    }
    if (solids.empty()) {
        throw GeometryError("El sólido importado no tiene cuerpos");
    }
    std::vector<Shape> shapes;
    for (std::size_t i = 0; i < solids.size(); ++i) {
        const std::string name = solids.size() == 1 ? tag : tag + ":c" + std::to_string(i + 1);
        shapes.push_back(Namer(solids[i]).finish(name));
    }
    return shapes;
}

} // namespace nuf::geometry
