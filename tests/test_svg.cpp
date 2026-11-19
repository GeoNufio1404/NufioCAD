// Importar dibujos SVG a un sketch (G1).
#include <nuf/sketch/Profiles.hpp>
#include <nuf/sketch/Sketch.hpp>
#include <nuf/sketch/Svg.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>
#include <sstream>
#include <string>

using namespace nuf::sketch;
using Catch::Approx;

namespace {

DxfDrawing read(const std::string& text)
{
    std::istringstream in(text);
    return readSvg(in);
}

template <typename T>
int countOf(const DxfDrawing& d)
{
    int n = 0;
    for (const Geometry& g : d.geometry) {
        n += std::holds_alternative<T>(g) ? 1 : 0;
    }
    return n;
}

struct Bounds {
    double minX = 1e300, minY = 1e300, maxX = -1e300, maxY = -1e300;
};

Bounds boundsOf(const DxfDrawing& d)
{
    Bounds b;
    for (const Geometry& g : d.geometry) {
        for (const Vec2& p : polyline(g)) {
            b.minX = std::min(b.minX, p.x);
            b.maxX = std::max(b.maxX, p.x);
            b.minY = std::min(b.minY, p.y);
            b.maxY = std::max(b.maxY, p.y);
        }
    }
    return b;
}

} // namespace

TEST_CASE("SVG: un rectángulo en milímetros con viewBox sale del tamaño pedido y con el eje Y invertido", "[svg]")
{
    const auto d = read(R"svg(<?xml version="1.0"?>
<svg xmlns="http://www.w3.org/2000/svg" width="40mm" height="20mm" viewBox="0 0 400 200">
  <rect x="100" y="50" width="200" height="100"/>
</svg>)svg");
    REQUIRE(countOf<Line>(d) == 4);
    const Bounds b = boundsOf(d);
    // 400 unidades = 40 mm: 0,1 mm por unidad; la base del viewBox es y = 0 y el rectángulo queda de y = 5 a 15
    CHECK(b.minX == Approx(10.0));
    CHECK(b.maxX == Approx(30.0));
    CHECK(b.minY == Approx(5.0));
    CHECK(b.maxY == Approx(15.0));
}

TEST_CASE("SVG: círculos, elipses y transformaciones de grupo", "[svg]")
{
    const auto d = read(R"svg(<svg width="100" height="100" viewBox="0 0 100 100" xmlns="http://www.w3.org/2000/svg">
  <g transform="translate(10 20) scale(2)">
    <circle cx="5" cy="5" r="3"/>
    <ellipse cx="30" cy="5" rx="6" ry="3"/>
  </g>
</svg>)svg");
    REQUIRE(countOf<Circle>(d) == 1);
    REQUIRE(countOf<Ellipse>(d) == 1);
    const double mm = 25.4 / 96.0;  // sin unidades: 1 px = 25,4/96 mm... con viewBox y sin ancho en mm el factor es el de px
    for (const Geometry& g : d.geometry) {
        if (const auto* c = std::get_if<Circle>(&g)) {
            CHECK(c->radius == Approx(6.0 * mm));
            CHECK(c->center.x == Approx((10 + 10) * mm));
            CHECK(c->center.y == Approx((100 - (20 + 10)) * mm));
        }
    }
}

TEST_CASE("SVG: trazados con líneas, curvas de Bézier, arcos y cierre", "[svg]")
{
    const auto d = read(R"svg(<svg width="100mm" height="100mm" viewBox="0 0 100 100" xmlns="http://www.w3.org/2000/svg">
  <path d="M10 10 L50 10 H60 v10 C70 30 80 30 90 20 S90 50 60 50 Q40 60 20 50 T10 30 A10 10 0 0 1 10 10 Z"/>
</svg>)svg");
    CHECK(countOf<Line>(d) >= 3);
    CHECK(countOf<BSpline>(d) == 4);  // C, S, Q y T
    CHECK(countOf<Arc>(d) == 1);
    // Los trozos encadenan: se pueden importar a un sketch y forman un perfil cerrado
    Sketch s;
    importDrawing(s, d);
    CHECK(!findProfiles(s).empty());
}

TEST_CASE("SVG: un arco circular y uno de elipse, y las abreviaturas de números pegados", "[svg]")
{
    // Media circunferencia de radio 10 por arriba, y un trazado con números pegados («.5.5», «-3-4»)
    const auto d = read(R"svg(<svg width="100mm" height="100mm" viewBox="0 0 100 100" xmlns="http://www.w3.org/2000/svg">
  <path d="M10 50 A10 10 0 0 1 30 50"/>
  <path d="M0 0 l.5.5-3-4"/>
  <path d="M60 50 a20 10 0 0 1 20 0"/>
</svg>)svg");
    REQUIRE(countOf<Arc>(d) == 1);
    REQUIRE(countOf<EllipseArc>(d) == 1);
    REQUIRE(countOf<Line>(d) == 2);
    for (const Geometry& g : d.geometry) {
        if (const auto* a = std::get_if<Arc>(&g)) {
            CHECK(a->radius == Approx(10.0));
            CHECK(a->center.x == Approx(20.0));
            CHECK(a->center.y == Approx(50.0));  // el arco va por encima en SVG (y menor): tras voltear queda por encima (y mayor)
            const Vec2 mid = polyline(g)[polyline(g).size() / 2];
            CHECK(mid.y > 50.0);
        }
    }
}

TEST_CASE("SVG: lo que no se importa se avisa y lo oculto o los defs se ignoran", "[svg]")
{
    const auto d = read(R"svg(<svg xmlns="http://www.w3.org/2000/svg" width="10mm" height="10mm" viewBox="0 0 10 10">
  <defs><path id="a" d="M0 0 L5 5"/></defs>
  <text x="1" y="5">hola</text>
  <line x1="0" y1="0" x2="10" y2="0" style="display:none"/>
  <polygon points="0,0 10,0 10,10"/>
  <use href="#a"/>
</svg>)svg");
    CHECK(countOf<Line>(d) == 3);  // solo el polígono
    REQUIRE(d.skipped.size() == 2);
    CHECK(d.skipped[0] == "text");
    CHECK(d.skipped[1] == "use");
}

TEST_CASE("SVG: un rectángulo con esquinas redondeadas y una escala desigual", "[svg]")
{
    const auto round = read(R"svg(<svg width="30mm" height="20mm" viewBox="0 0 30 20" xmlns="http://www.w3.org/2000/svg">
  <rect x="0" y="0" width="30" height="20" rx="4"/>
</svg>)svg");
    CHECK(countOf<Line>(round) == 4);
    CHECK(countOf<Arc>(round) == 4);
    Sketch s;
    importDrawing(s, round);
    CHECK(findProfiles(s).size() == 1);

    // Escala desigual: el círculo pasa a ser una spline por sus puntos
    const auto stretched = read(R"svg(<svg width="100mm" height="100mm" viewBox="0 0 100 100" xmlns="http://www.w3.org/2000/svg">
  <circle transform="scale(2 1)" cx="10" cy="10" r="5"/>
</svg>)svg");
    CHECK(countOf<Circle>(stretched) == 0);
    CHECK(countOf<BSpline>(stretched) == 1);
    const Bounds b = boundsOf(stretched);
    CHECK((b.maxX - b.minX) == Approx(20.0).margin(0.2));
    CHECK((b.maxY - b.minY) == Approx(10.0).margin(0.2));
}
