#include <nuf/geometry/Exchange.hpp>
#include <nuf/geometry/Modeling.hpp>
#include <nuf/sketch/Profiles.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

using namespace nuf;
using namespace nuf::geometry;
using Catch::Matchers::WithinRel;

namespace {

Shape extrudedRectangle(double w, double h, double depth)
{
    sketch::Sketch s;
    s.addLine({0, 0}, {w, 0});
    s.addLine({w, 0}, {w, h});
    s.addLine({w, h}, {0, h});
    s.addLine({0, h}, {0, 0});
    SketchRegion region{&s, sketch::Plane::xy(), sketch::findProfiles(s)};
    ExtrudeSpec spec;
    spec.side1.distance = depth;
    return extrude("f1", region, spec);
}

// Cilindro: sus caras curvas obligan a mallar de verdad.
Shape cylinder(double radius, double depth)
{
    sketch::Sketch s;
    s.addCircle({0, 0}, radius);
    SketchRegion region{&s, sketch::Plane::xy(), sketch::findProfiles(s)};
    ExtrudeSpec spec;
    spec.side1.distance = depth;
    return extrude("f1", region, spec);
}

std::filesystem::path tempFile(const std::string& name)
{
    // Con acentos a propósito: las rutas deben ir en UTF-8 en Windows.
    const auto path = std::filesystem::temp_directory_path() / (std::u8string(u8"nuf_exportación_") + std::u8string(name.begin(), name.end()));
    std::filesystem::remove(path);
    return path;
}

std::string readAll(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

float floatAt(const std::string& data, std::size_t offset)
{
    float value = 0.0f;
    std::memcpy(&value, data.data() + offset, sizeof(float));
    return value;
}

// Volumen con signo de un STL binario.
double stlVolume(const std::string& data, std::uint32_t& count)
{
    std::memcpy(&count, data.data() + 80, sizeof(count));
    double volume = 0.0;
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::size_t base = 84 + std::size_t(i) * 50 + 12;  // salta la normal
        double p[3][3];
        for (int v = 0; v < 3; ++v) {
            for (int k = 0; k < 3; ++k) {
                p[v][k] = floatAt(data, base + std::size_t(v * 3 + k) * 4);
            }
        }
        const double cx = p[1][1] * p[2][2] - p[1][2] * p[2][1];
        const double cy = p[1][2] * p[2][0] - p[1][0] * p[2][2];
        const double cz = p[1][0] * p[2][1] - p[1][1] * p[2][0];
        volume += (p[0][0] * cx + p[0][1] * cy + p[0][2] * cz) / 6.0;
    }
    return volume;
}

} // namespace

TEST_CASE("La malla encierra el volumen del sólido", "[exchange]")
{
    const Shape box = extrudedRectangle(10, 20, 30);
    REQUIRE_THAT(meshVolume({box}, MeshQuality::medium()), WithinRel(6000.0, 1e-6));

    // Con curvas, más fina = más cerca del volumen real (π r² h).
    const Shape tube = cylinder(10.0, 20.0);
    const double exact = 3.14159265358979 * 100.0 * 20.0;
    const double coarse = meshVolume({tube}, MeshQuality::low());
    const double fine = meshVolume({tube}, MeshQuality::high());
    REQUIRE(std::abs(fine - exact) < std::abs(coarse - exact));
    REQUIRE_THAT(fine, WithinRel(exact, 5e-3));
    REQUIRE(countTriangles({tube}, MeshQuality::high()) > countTriangles({tube}, MeshQuality::low()));
}

TEST_CASE("Mallar para exportar no altera el cuerpo", "[exchange]")
{
    const Shape tube = cylinder(10.0, 20.0);
    const double before = tube.volume();
    const auto n1 = countTriangles({tube}, MeshQuality::low());
    const auto n2 = countTriangles({tube}, MeshQuality::low());
    REQUIRE(n1 == n2);  // no acumula ni reutiliza una malla anterior
    REQUIRE_THAT(tube.volume(), WithinRel(before, 1e-12));
}

TEST_CASE("STL binario: caja con 12 triángulos y volumen exacto", "[exchange]")
{
    const auto path = tempFile("caja.stl");
    writeStl(path, {extrudedRectangle(10, 20, 30)}, MeshQuality::medium());
    const std::string data = readAll(path);
    std::filesystem::remove(path);

    std::uint32_t count = 0;
    REQUIRE(data.size() > 84);
    const double volume = stlVolume(data, count);
    REQUIRE(count == 12);
    REQUIRE(data.size() == 84 + std::size_t(count) * 50);
    REQUIRE_THAT(volume, WithinRel(6000.0, 1e-5));  // positivo: normales hacia fuera
}

TEST_CASE("STL con varios cuerpos los junta todos", "[exchange]")
{
    const auto path = tempFile("dos.stl");
    writeStl(path, {extrudedRectangle(10, 10, 10), extrudedRectangle(5, 5, 5)}, MeshQuality::medium());
    const std::string data = readAll(path);
    std::filesystem::remove(path);
    std::uint32_t count = 0;
    REQUIRE_THAT(stlVolume(data, count), WithinRel(1125.0, 1e-5));
    REQUIRE(count == 24);
}

TEST_CASE("OBJ: vértices, normales y caras coherentes", "[exchange]")
{
    const auto path = tempFile("caja.obj");
    writeObj(path, {extrudedRectangle(10, 20, 30)}, MeshQuality::medium());
    const std::string text = readAll(path);
    std::filesystem::remove(path);

    std::istringstream lines(text);
    std::string line;
    int vertices = 0, normals = 0, faces = 0;
    while (std::getline(lines, line)) {
        vertices += line.starts_with("v ") ? 1 : 0;
        normals += line.starts_with("vn ") ? 1 : 0;
        faces += line.starts_with("f ") ? 1 : 0;
    }
    REQUIRE(faces == 12);
    REQUIRE(vertices == 36);
    REQUIRE(normals == 12);
}

TEST_CASE("STEP: el archivo tiene un sólido y se puede abrir en otros programas", "[exchange]")
{
    const auto path = tempFile("caja.step");
    writeStep(path, {extrudedRectangle(10, 20, 30)});
    const std::string text = readAll(path);
    std::filesystem::remove(path);
    REQUIRE(text.starts_with("ISO-10303-21;"));
    REQUIRE(text.find("MANIFOLD_SOLID_BREP") != std::string::npos);
    REQUIRE(text.find("END-ISO-10303-21;") != std::string::npos);
}

TEST_CASE("Exportar sin cuerpos o a una ruta imposible da error claro", "[exchange]")
{
    REQUIRE_THROWS_AS(writeStl(tempFile("nada.stl"), {}, MeshQuality::medium()), GeometryError);
    REQUIRE_THROWS_AS(writeStep(tempFile("nada.step"), {Shape()}), GeometryError);
    const auto missing = std::filesystem::temp_directory_path() / "no_existe_carpeta_nuf" / "x.stl";
    REQUIRE_THROWS_AS(writeStl(missing, {extrudedRectangle(1, 1, 1)}, MeshQuality::medium()), GeometryError);
}

TEST_CASE("Importar un STEP da el mismo sólido, con caras nombradas y estables", "[exchange][import]")
{
    const auto path = tempFile("importar.step");
    writeStep(path, {extrudedRectangle(20, 10, 5)});
    const StepImport imported = readStep(path);
    std::filesystem::remove(path);

    REQUIRE(imported.solids == 1);
    REQUIRE_FALSE(imported.brep.empty());
    const std::vector<Shape> shapes = shapesFromBrep(imported.brep, "f7");
    REQUIRE(shapes.size() == 1);
    REQUIRE(shapes[0].isValid());
    REQUIRE(shapes[0].faceCount() == 6);
    REQUIRE_THAT(shapes[0].volume(), WithinRel(1000.0, 1e-6));
    for (int i = 0; i < shapes[0].faceCount(); ++i) {
        REQUIRE(shapes[0].faceName(i).starts_with("f7:?"));
    }
    // Los mismos datos dan siempre los mismos nombres y el mismo orden.
    const std::vector<Shape> again = shapesFromBrep(imported.brep, "f7");
    for (int i = 0; i < shapes[0].faceCount(); ++i) {
        REQUIRE(again[0].faceName(i) == shapes[0].faceName(i));
        REQUIRE(again[0].faceCenter(i).x == shapes[0].faceCenter(i).x);
    }
}

TEST_CASE("Un STEP con varios sólidos da un cuerpo por sólido, sin nombres repetidos", "[exchange][import]")
{
    const auto path = tempFile("varios.step");
    writeStep(path, {extrudedRectangle(20, 10, 5), cylinder(3, 8)});
    const StepImport imported = readStep(path);
    std::filesystem::remove(path);

    REQUIRE(imported.solids == 2);
    const std::vector<Shape> shapes = shapesFromBrep(imported.brep, "f4");
    REQUIRE(shapes.size() == 2);
    for (const Shape& shape : shapes) {
        REQUIRE(shape.isValid());
    }
    REQUIRE(shapes[0].faceName(0).starts_with("f4:c1:?"));
    REQUIRE(shapes[1].faceName(0).starts_with("f4:c2:?"));
    // Entre los dos no hay ni un nombre en común.
    for (int i = 0; i < shapes[0].faceCount(); ++i) {
        REQUIRE_FALSE(shapes[1].findFace(shapes[0].faceRef(i)));
    }
}

TEST_CASE("Importar un archivo que no sirve da un error claro", "[exchange][import]")
{
    const auto garbage = tempFile("basura.step");
    {
        std::ofstream out(garbage, std::ios::binary);
        out << "esto no es un STEP";
    }
    REQUIRE_THROWS_AS(readStep(garbage), GeometryError);
    std::filesystem::remove(garbage);

    const auto missing = std::filesystem::temp_directory_path() / "no_existe_carpeta_nuf" / "x.step";
    REQUIRE_THROWS_AS(readStep(missing), GeometryError);
    REQUIRE_THROWS_AS(shapesFromBrep("datos dañados", "f1"), GeometryError);
    REQUIRE_THROWS_AS(shapesFromBrep("", "f1"), GeometryError);
}

// Genera un STEP de muestra para probar a mano «Importar STEP»; no se ejecuta
// con el resto: NUF_SAMPLE_STEP=ruta nuf_tests "[sample]"
TEST_CASE("Escribir un STEP de muestra", "[.][sample]")
{
    const char* target = std::getenv("NUF_SAMPLE_STEP");
    REQUIRE(target != nullptr);
    writeStep(std::filesystem::path(target), {extrudedRectangle(40, 25, 12)});
}

TEST_CASE("3MF: un ZIP con un objeto por cuerpo, nombres, materiales y volumen fiel", "[exchange][3mf]")
{
    const auto path = tempFile("dos.3mf");
    writeThreeMf(path, {{extrudedRectangle(10, 20, 30), "Caja <1>", 0}, {cylinder(5, 8), "Tapa", 0x112233}}, MeshQuality::medium());
    const std::string bytes = readAll(path);
    std::filesystem::remove(path);

    // Cabecera de ZIP, tres archivos y el directorio central al final
    REQUIRE(bytes.size() > 100);
    REQUIRE(bytes.compare(0, 4, std::string("PK\x03\x04", 4)) == 0);
    REQUIRE(bytes.find("PK\x05\x06") != std::string::npos);
    REQUIRE(bytes.find("[Content_Types].xml") != std::string::npos);
    REQUIRE(bytes.find("_rels/.rels") != std::string::npos);
    REQUIRE(bytes.find("3D/3dmodel.model") != std::string::npos);
    const std::size_t model = bytes.find("<?xml", bytes.find("3D/3dmodel.model"));
    REQUIRE(model != std::string::npos);
    const std::string xml = bytes.substr(model);
    REQUIRE(xml.find("name=\"Caja &lt;1&gt;\"") != std::string::npos);  // el nombre va escapado
    REQUIRE(xml.find("displaycolor=\"#112233FF\"") != std::string::npos);
    REQUIRE(xml.find("Filamento 1") != std::string::npos);
    REQUIRE(xml.find("<item objectid=\"3\"/>") != std::string::npos);

    // El volumen de cada objeto, calculado de sus vértices y triángulos
    const auto number = [&](const std::string& tag, const std::string& attr, std::size_t& at) {
        const std::size_t open = xml.find(tag, at);
        REQUIRE(open != std::string::npos);
        const std::size_t quote = xml.find(attr + "=\"", open) + attr.size() + 2;
        at = open + 1;
        return std::string(xml.substr(quote, xml.find('"', quote) - quote));
    };
    std::vector<double> volumes;
    std::size_t objectAt = 0;
    for (int k = 0; k < 2; ++k) {
        const std::size_t begin = xml.find("<object ", objectAt);
        const std::size_t end = xml.find("</object>", begin);
        REQUIRE(begin != std::string::npos);
        objectAt = end;
        std::vector<std::array<double, 3>> v;
        double volume = 0.0;
        std::size_t at = begin;
        while (true) {
            const std::size_t next = xml.find("<vertex ", at);
            if (next == std::string::npos || next > end) {
                break;
            }
            std::size_t a = next;
            const double x = std::stod(number("<vertex ", "x", a));
            a = next;
            const double y = std::stod(number("<vertex ", "y", a));
            a = next;
            const double z = std::stod(number("<vertex ", "z", a));
            v.push_back({x, y, z});
            at = next + 1;
        }
        at = begin;
        while (true) {
            const std::size_t next = xml.find("<triangle ", at);
            if (next == std::string::npos || next > end) {
                break;
            }
            std::size_t a = next;
            const auto i = std::stoul(number("<triangle ", "v1", a));
            a = next;
            const auto j = std::stoul(number("<triangle ", "v2", a));
            a = next;
            const auto l = std::stoul(number("<triangle ", "v3", a));
            REQUIRE(i < v.size());
            REQUIRE(j < v.size());
            REQUIRE(l < v.size());
            const auto& p = v[i];
            const auto& q = v[j];
            const auto& r = v[l];
            volume += (p[0] * (q[1] * r[2] - q[2] * r[1]) - p[1] * (q[0] * r[2] - q[2] * r[0]) + p[2] * (q[0] * r[1] - q[1] * r[0])) / 6.0;
            at = next + 1;
        }
        volumes.push_back(volume);
    }
    REQUIRE_THAT(volumes[0], WithinRel(10.0 * 20.0 * 30.0, 1e-6));
    REQUIRE_THAT(volumes[1], WithinRel(3.14159265358979 * 25.0 * 8.0, 0.01));
    REQUIRE_THROWS_AS(writeThreeMf(tempFile("nada.3mf"), {}, MeshQuality::medium()), GeometryError);
}

TEST_CASE("Revisión de la malla: una caja y un cilindro salen cerrados, bien orientados y fieles", "[exchange][mesh]")
{
    for (const Shape& shape : {extrudedRectangle(10, 20, 30), cylinder(5, 8)}) {
        const MeshReport report = analyzeMesh(shape, MeshQuality::medium());
        CHECK(report.triangles >= 12);
        CHECK(report.openEdges == 0);
        CHECK(report.nonManifoldEdges == 0);
        CHECK(report.flippedEdges == 0);
        CHECK(report.degenerateTriangles == 0);
        CHECK(report.volumeDeviation() < 0.01);
    }
    // Con calidad baja el cilindro se aparta más del volumen del sólido que con calidad alta
    const Shape round = cylinder(5, 8);
    CHECK(analyzeMesh(round, MeshQuality::low()).volumeDeviation() > analyzeMesh(round, MeshQuality::high()).volumeDeviation());
    CHECK(analyzeMesh(Shape{}, MeshQuality::medium()).triangles == 0);
}

TEST_CASE("Voladizos: una caja apoyada no tiene; un escalón en voladizo sí", "[exchange][overhang]")
{
    // Una caja apoyada en la cama: su cara de abajo está en la Z mínima, no es voladizo
    const OverhangReport box = analyzeOverhang(extrudedRectangle(10, 20, 30), MeshQuality::medium(), 45.0);
    CHECK(box.overhangArea == 0.0);
    CHECK(box.totalArea == Catch::Approx(2.0 * (10 * 20 + 10 * 30 + 20 * 30)).epsilon(1e-6));
    // Dos cajas, la de arriba más ancha: su cara de abajo (el techo) mira hacia abajo sin apoyarse
    const Shape base = extrudedRectangle(10, 10, 10);
    const Shape wide = extrudedRectangle(30, 10, 10);
    const auto planeUp = sketch::Plane::fromPointAndNormal({0, 0, 10}, {0, 0, 1});
    const Shape lifted = placeBody("w", wide, sketch::Plane::xy(), planeUp);
    const Shape both = boolean(BooleanOp::Fuse, base, lifted);
    const OverhangReport step = analyzeOverhang(both, MeshQuality::medium(), 45.0);
    CHECK(step.overhangArea == Catch::Approx(20.0 * 10.0).epsilon(1e-6));  // la parte de la placa que sobresale de la base
}
