#include <nuf/geometry/Modeling.hpp>
#include <nuf/sketch/Profiles.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>

using namespace nuf;
using namespace nuf::geometry;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {

// Rectángulo de líneas unidas por sus extremos; devuelve la primera (y = y0).
sketch::EntityId rectangle(sketch::Sketch& s, double x0, double y0, double x1, double y1)
{
    const sketch::EntityId first = s.addLine({x0, y0}, {x1, y0});
    s.addLine({x1, y0}, {x1, y1});
    s.addLine({x1, y1}, {x0, y1});
    s.addLine({x0, y1}, {x0, y0});
    return first;
}

std::string e(sketch::EntityId id) { return "e" + std::to_string(id); }

SketchRegion allProfiles(const sketch::Sketch& s, sketch::Plane plane = sketch::Plane::xy())
{
    return {&s, plane, sketch::findProfiles(s)};
}

ExtrudeSpec distance(double d)
{
    ExtrudeSpec spec;
    spec.side1.distance = d;
    return spec;
}

int countNamed(const Shape& shape, const std::string& prefix)
{
    int n = 0;
    for (int i = 0; i < shape.faceCount(); ++i) {
        n += shape.faceName(i).starts_with(prefix) ? 1 : 0;
    }
    return n;
}

std::optional<int> faceStarting(const Shape& shape, const std::string& prefix)
{
    for (int i = 0; i < shape.faceCount(); ++i) {
        if (shape.faceName(i).starts_with(prefix)) {
            return i;
        }
    }
    return std::nullopt;
}

} // namespace

TEST_CASE("Extruir un rectángulo: volumen y nombres de caras", "[geometry][modeling]")
{
    sketch::Sketch s;
    const auto first = rectangle(s, 0, 0, 20, 10);
    const Shape box = extrude("f1", allProfiles(s), distance(5));
    REQUIRE(box.solidCount() == 1);
    REQUIRE_THAT(box.volume(), WithinRel(1000.0, 1e-9));
    REQUIRE(box.faceCount() == 6);
    REQUIRE(countNamed(box, "f1:lado:e") == 4);
    REQUIRE(countNamed(box, "f1:inicio:" + e(first)) == 1);
    REQUIRE(countNamed(box, "f1:fin:" + e(first)) == 1);
    // Cada línea genera su cara: la primera (y = 0) da la cara frontal.
    const auto front = box.findFace({"f1:lado:" + e(first), 0});
    REQUIRE(front);
    const auto plane = box.facePlane(*front);
    REQUIRE(plane);
    REQUIRE_THAT(plane->normal.y, WithinAbs(-1.0, 1e-9));
    // La tapa final mira hacia +Z, a 5 mm.
    const auto top = box.facePlane(*box.findFace({"f1:fin:" + e(first), 0}));
    REQUIRE_THAT(top->normal.z, WithinAbs(1.0, 1e-9));
    REQUIRE_THAT(top->origin.z, WithinAbs(5.0, 1e-9));
    // Las aristas se nombran por sus dos caras.
    REQUIRE(box.edgeCount() == 12);
    REQUIRE(box.findEdge({"f1:fin:" + e(first) + "|f1:lado:" + e(first), 0}));
}

TEST_CASE("Extruir: dirección negativa, simétrica, dos lados y desfase", "[geometry][modeling]")
{
    sketch::Sketch s;
    rectangle(s, 0, 0, 20, 10);

    const Shape down = extrude("f1", allProfiles(s), distance(-5));
    REQUIRE_THAT(down.volume(), WithinRel(1000.0, 1e-9));
    REQUIRE_THAT(down.bounds().min.z, WithinAbs(-5.0, 1e-6));

    ExtrudeSpec symmetric = distance(5);
    symmetric.direction = ExtrudeDirection::Symmetric;
    const Shape both = extrude("f1", allProfiles(s), symmetric);
    REQUIRE_THAT(both.volume(), WithinRel(2000.0, 1e-9));
    REQUIRE(both.faceCount() == 6);  // las laterales de los dos lados se unen
    REQUIRE_THAT(both.bounds().min.z, WithinAbs(-5.0, 1e-6));
    REQUIRE_THAT(both.bounds().max.z, WithinAbs(5.0, 1e-6));

    ExtrudeSpec two = distance(5);
    two.direction = ExtrudeDirection::Two;
    two.side2.distance = 3;
    const Shape twoSides = extrude("f1", allProfiles(s), two);
    REQUIRE_THAT(twoSides.volume(), WithinRel(1600.0, 1e-9));
    REQUIRE(countNamed(twoSides, "f1:fin2:") == 1);

    ExtrudeSpec offset = distance(5);
    offset.startOffset = 2;
    const Shape lifted = extrude("f1", allProfiles(s), offset);
    REQUIRE_THAT(lifted.bounds().min.z, WithinAbs(2.0, 1e-6));
    REQUIRE(countNamed(lifted, "f1:lado:e") == 4);
}

TEST_CASE("Extruir con ángulo: positivo ensancha, negativo estrecha", "[geometry][modeling]")
{
    sketch::Sketch s;
    rectangle(s, 0, 0, 20, 10);
    ExtrudeSpec spec = distance(5);
    spec.side1.taper = 0.1;
    const Shape wider = extrude("f1", allProfiles(s), spec);
    REQUIRE(wider.volume() > 1000.0);
    REQUIRE(countNamed(wider, "f1:lado:e") == 4);
    spec.side1.taper = -0.1;
    REQUIRE(extrude("f1", allProfiles(s), spec).volume() < 1000.0);
}

TEST_CASE("Extruir un perfil con agujero y dos perfiles a la vez", "[geometry][modeling]")
{
    sketch::Sketch s;
    rectangle(s, 0, 0, 20, 20);
    s.addCircle({10, 10}, 3);
    const auto profiles = sketch::findProfiles(s);
    REQUIRE(profiles.size() == 2);
    // Solo el anillo (el perfil con agujero).
    const auto ring = std::ranges::find_if(profiles, [](const auto& p) { return !p.holes.empty(); });
    const Shape withHole = extrude("f1", {&s, sketch::Plane::xy(), {*ring}}, distance(10));
    REQUIRE_THAT(withHole.volume(), WithinRel((400.0 - std::numbers::pi * 9.0) * 10.0, 1e-6));
    // Los dos perfiles juntos: el bloque entero, sin pared interior.
    const Shape full = extrude("f1", allProfiles(s), distance(10));
    REQUIRE_THAT(full.volume(), WithinRel(4000.0, 1e-6));
    REQUIRE(full.solidCount() == 1);
}

TEST_CASE("Revolución completa y parcial", "[geometry][modeling]")
{
    // Rectángulo en XZ de x = 10..15, z = 0..4, girado alrededor de Z.
    sketch::Sketch s;
    rectangle(s, 10, 0, 15, 4);
    const sketch::Plane xz = sketch::Plane::xz();
    const SketchRegion region{&s, xz, sketch::findProfiles(s)};
    const Axis z{{0, 0, 0}, {0, 0, 1}};
    RevolveSpec full;
    full.extent = RevolveExtent::Full;
    const Shape ring = revolve("f1", region, z, full);
    const double expected = std::numbers::pi * (15.0 * 15.0 - 10.0 * 10.0) * 4.0;
    REQUIRE_THAT(ring.volume(), WithinRel(expected, 1e-6));
    REQUIRE(countNamed(ring, "f1:inicio") == 0);

    RevolveSpec half;
    half.angle1 = std::numbers::pi;
    const Shape halfRing = revolve("f1", region, z, half);
    REQUIRE_THAT(halfRing.volume(), WithinRel(expected / 2.0, 1e-6));
    REQUIRE(countNamed(halfRing, "f1:inicio") == 1);
    REQUIRE(countNamed(halfRing, "f1:fin") == 1);

    half.direction = ExtrudeDirection::Symmetric;
    half.angle1 = std::numbers::pi / 4.0;
    REQUIRE_THAT(revolve("f1", region, z, half).volume(), WithinRel(expected / 4.0, 1e-6));
}

TEST_CASE("Booleanas: un corte hereda los nombres de la herramienta", "[geometry][modeling]")
{
    sketch::Sketch base;
    const auto first = rectangle(base, 0, 0, 20, 20);
    const Shape block = extrude("f1", allProfiles(base), distance(10));

    sketch::Sketch holeSketch;
    const sketch::EntityId circle = holeSketch.addCircle({10, 10}, 3);
    ExtrudeSpec through;
    through.side1.extent = Extent::ThroughAll;
    through.direction = ExtrudeDirection::Symmetric;
    const Shape tool = extrude("f2", allProfiles(holeSketch), through, {block});
    REQUIRE(touches(block, tool));

    const Shape cut = boolean(BooleanOp::Cut, block, tool);
    REQUIRE_THAT(cut.volume(), WithinRel(4000.0 - std::numbers::pi * 9.0 * 10.0, 1e-6));
    REQUIRE(countNamed(cut, "f2:lado:e" + std::to_string(circle)) == 1);
    // La tapa superior sigue llamándose igual, ahora con agujero.
    REQUIRE(cut.findFace({"f1:fin:" + e(first), 0}));
    REQUIRE(cut.faceCount() == 7);

    const Shape joined = boolean(BooleanOp::Fuse, block, extrude("f3", allProfiles(base), distance(-5)));
    REQUIRE_THAT(joined.volume(), WithinRel(6000.0, 1e-6));
    REQUIRE(joined.faceCount() == 6);  // las laterales coplanares se unen

    sketch::Sketch far;
    rectangle(far, 100, 100, 110, 110);
    REQUIRE_FALSE(touches(block, extrude("f4", allProfiles(far), distance(5))));
    REQUIRE(boolean(BooleanOp::Common, block, extrude("f4", allProfiles(far), distance(5))).isNull());
}

TEST_CASE("Nombres estables: la cara de arriba se encuentra tras cambiar la altura", "[geometry][modeling]")
{
    sketch::Sketch s;
    rectangle(s, 0, 0, 20, 10);
    const Shape low = extrude("f1", allProfiles(s), distance(5));
    const ElementRef top = low.faceRef(*faceStarting(low, "f1:fin:"));

    const Shape high = extrude("f1", allProfiles(s), distance(8));
    const auto found = high.findFace(top);
    REQUIRE(found);
    REQUIRE_THAT(high.facePlane(*found)->origin.z, WithinAbs(8.0, 1e-9));
}

TEST_CASE("Nombres estables: una cara partida se distingue por su posición", "[geometry][modeling]")
{
    sketch::Sketch base;
    const auto first = rectangle(base, 0, 0, 30, 10);
    const Shape block = extrude("f1", allProfiles(base), distance(10));
    // Ranura que cruza el bloque y parte la tapa superior en dos.
    sketch::Sketch slot;
    rectangle(slot, 10, -5, 20, 15);
    ExtrudeSpec down = distance(-4);
    down.startOffset = 10;
    const Shape cut = boolean(BooleanOp::Cut, block, extrude("f2", allProfiles(slot), down));
    const std::string top = "f1:fin:" + e(first);
    REQUIRE(countNamed(cut, top) == 2);
    const auto left = cut.findFace({top, 0});
    const auto right = cut.findFace({top, 1});
    REQUIRE(left);
    REQUIRE(right);
    REQUIRE(cut.faceRef(*left).occurrence == 0);
    REQUIRE(cut.faceRef(*right).occurrence == 1);
}

TEST_CASE("Extruir hasta una cara y empujar una cara", "[geometry][modeling]")
{
    // Un techo inclinado a z = 10 + x/10 y un poste desde z = 0 hasta él.
    sketch::Sketch roof;
    rectangle(roof, -10, -10, 10, 10);
    const sketch::Plane tilted = sketch::Plane::fromPointAndNormal({0, 0, 10}, {-0.1, 0, 1});
    const Shape slab = extrude("f1", allProfiles(roof, tilted), distance(2));
    const ElementRef under = slab.faceRef(*faceStarting(slab, "f1:inicio:"));

    sketch::Sketch post;
    rectangle(post, -1, -1, 1, 1);
    ExtrudeSpec spec;
    spec.side1.extent = Extent::ToFace;
    spec.side1.face = under;
    const Shape column = extrude("f2", allProfiles(post), spec, {slab});
    REQUIRE_THAT(column.volume(), WithinRel(4.0 * 10.0, 1e-3));
    REQUIRE(countNamed(column, "f2:fin:") == 1);

    // Empujar la cara superior de un bloque 3 mm más.
    sketch::Sketch base;
    const auto first = rectangle(base, 0, 0, 20, 10);
    const Shape block = extrude("f3", allProfiles(base), distance(5));
    const Shape extra = extrudeFace("f4", block, *faceStarting(block, "f3:fin:"), distance(3));
    REQUIRE_THAT(extra.volume(), WithinRel(600.0, 1e-9));
    REQUIRE(countNamed(extra, "f4:lado:f3:fin:" + e(first) + "|f3:lado:e") == 4);
}

TEST_CASE("Errores de modelado con mensaje", "[geometry][modeling]")
{
    sketch::Sketch s;
    rectangle(s, 0, 0, 20, 10);
    REQUIRE_THROWS_AS(extrude("f1", allProfiles(s), distance(0)), GeometryError);
    REQUIRE_THROWS_AS(extrude("f1", {&s, sketch::Plane::xy(), {}}, distance(5)), GeometryError);
    ExtrudeSpec spec;
    spec.side1.extent = Extent::ToFace;
    spec.side1.face = {"f9:nada", 0};
    REQUIRE_THROWS_AS(extrude("f1", allProfiles(s), spec), GeometryError);
}

TEST_CASE("Extruir solo una de las regiones que deja una línea que cruza", "[geometry][modeling]")
{
    sketch::Sketch s;
    rectangle(s, 0, 0, 40, 20);
    s.addLine({10, -5}, {10, 25});
    const auto profiles = sketch::findProfiles(s);
    REQUIRE(profiles.size() == 2);
    const auto small = std::ranges::min_element(profiles, {}, &sketch::Profile::area);
    const Shape part = extrude("f1", {&s, sketch::Plane::xy(), {*small}}, distance(5));
    REQUIRE_THAT(part.volume(), WithinRel(10.0 * 20.0 * 5.0, 1e-9));

    // Medio círculo (arco recortado de la circunferencia + tramo de recta).
    sketch::Sketch c;
    c.addCircle({0, 0}, 10);
    c.addLine({-20, 0}, {20, 0});
    for (const sketch::Profile& half : sketch::findProfiles(c)) {
        const Shape solid = extrude("f2", {&c, sketch::Plane::xy(), {half}}, distance(2));
        REQUIRE_THAT(solid.volume(), WithinRel(std::numbers::pi * 50.0 * 2.0, 1e-6));
    }
}

TEST_CASE("Revolución que corta desde el eje: el sólido queda cerrado", "[geometry][modeling]")
{
    sketch::Sketch base;
    rectangle(base, -20, -20, 20, 20);
    const Shape block = extrude("f1", allProfiles(base), distance(10));
    REQUIRE(block.isValid());

    // Rectángulo en XZ apoyado en el eje Z (x = 0..8, z = 4..14).
    sketch::Sketch s;
    rectangle(s, 0, 4, 8, 14);
    const SketchRegion region{&s, sketch::Plane::xz(), sketch::findProfiles(s)};
    const Axis z{{0, 0, 0}, {0, 0, 1}};
    for (const auto& [extent, angle] : {std::pair{RevolveExtent::Full, 0.0}, std::pair{RevolveExtent::Angle, 1.5},
                                        std::pair{RevolveExtent::Angle, std::numbers::pi}}) {
        RevolveSpec spec;
        spec.extent = extent;
        spec.angle1 = angle;
        const Shape tool = revolve("f2", region, z, spec);
        INFO("ángulo " << angle);
        REQUIRE(tool.isValid());
        const Shape cut = boolean(BooleanOp::Cut, block, tool);
        REQUIRE(cut.isValid());
    }
}

TEST_CASE("Un ángulo que cierra el perfil antes de la distancia se rechaza", "[geometry][modeling]")
{
    // Círculo de radio 10, 30 mm de alto: estrechar 16 mm lo haría pasar por
    // su vértice (un reloj de arena con la tapa del revés).
    sketch::Sketch c;
    c.addCircle({0, 0}, 10);
    ExtrudeSpec spec = distance(30);
    spec.side1.taper = -0.5;
    REQUIRE_THROWS_AS(extrude("f1", allProfiles(c), spec), GeometryError);
    spec.side1.taper = -0.2;  // estrecha 6 mm: bien
    REQUIRE(extrude("f1", allProfiles(c), spec).isValid());
    // Abrir nunca cierra.
    spec.side1.taper = 0.5;
    REQUIRE(extrude("f1", allProfiles(c), spec).isValid());
}

TEST_CASE("Las 5 regiones de un círculo inscrito en un cuadrado se extruyen", "[geometry][modeling]")
{
    sketch::Sketch s;
    rectangle(s, 0, 0, 20, 20);
    s.addCircle({10, 10}, 10);
    const auto profiles = sketch::findProfiles(s);
    REQUIRE(profiles.size() == 5);
    double total = 0.0;
    for (const sketch::Profile& p : profiles) {
        const Shape solid = extrude("f1", {&s, sketch::Plane::xy(), {p}}, distance(1));
        REQUIRE(solid.isValid());
        total += solid.volume();
    }
    REQUIRE_THAT(total, WithinRel(400.0, 1e-6));
    REQUIRE_THAT(extrude("f1", allProfiles(s), distance(1)).volume(), WithinRel(400.0, 1e-6));
}
