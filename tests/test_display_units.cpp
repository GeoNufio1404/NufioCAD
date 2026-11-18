// Unidad de visualización del núcleo (Fase 15, U3): Medir, avisos de impresión y resúmenes de los generadores.
#include <nuf/document/Generators.hpp>
#include <nuf/geometry/DisplayUnits.hpp>
#include <nuf/geometry/Measure.hpp>
#include <nuf/geometry/Modeling.hpp>
#include <nuf/io/DocumentFile.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace nuf;
namespace display = nuf::geometry::display;

namespace {

// La unidad es global: cada prueba la deja otra vez en milímetros.
struct RestoreUnit {
    ~RestoreUnit() { display::setUnit(display::Unit::Mm); }
};

bool contains(const std::string& text, const std::string& part)
{
    return text.find(part) != std::string::npos;
}

} // namespace

TEST_CASE("Los textos del núcleo siguen la unidad de visualización", "[units][display]")
{
    const RestoreUnit restore;
    REQUIRE(display::length(12.5) == "12.50 mm");
    REQUIRE(display::area(100.0) == "100.00 mm²");
    REQUIRE(display::volume(1000.0, 1) == "1000.0 mm³");

    display::setUnit(display::Unit::Cm);
    REQUIRE(display::length(25.0) == "2.500 cm");
    REQUIRE(display::area(100.0) == "1.000 cm²");  // 100 mm² = 1 cm²
    REQUIRE(display::volume(1000.0, 1) == "1.00 cm³");  // 1000 mm³ = 1 cm³

    display::setUnit(display::Unit::In);
    REQUIRE(display::length(25.4) == "1.0000 in");
    REQUIRE(display::number(50.8) == "2.0000");
}

TEST_CASE("Medir y el resumen de un generador salen en la unidad elegida", "[units][display]")
{
    const RestoreUnit restore;
    const auto plane = sketch::Plane::fromPointAndNormal({0, 0, 0}, {0, 0, 1});
    const geometry::Shape box = geometry::box("c", plane, 20.0, 20.0, 10.0, false);
    // La cara de arriba de la caja: 20 × 20 mm.
    int top = -1;
    for (int i = 0; i < box.faceCount(); ++i) {
        if (box.faceName(i).find("+z") != std::string::npos) {
            top = i;
        }
    }
    REQUIRE(top >= 0);
    const auto describe = [&] {
        const geometry::Measurement measurement = geometry::measure({{box, true, top}});
        std::string all;
        for (const std::string& line : measurement.lines) {
            all += line + "\n";
        }
        return all;
    };
    REQUIRE(contains(describe(), "Área: 400.00 mm²"));
    display::setUnit(display::Unit::Cm);
    REQUIRE(contains(describe(), "cm²"));
    REQUIRE(!contains(describe(), "mm"));

    const document::Generator* generator = document::findGenerator("par-engranajes");
    REQUIRE(generator != nullptr);
    const std::string summary = generator->summary(generator->defaults());
    REQUIRE(contains(summary, "distancia entre ejes 3.615 cm"));
    REQUIRE(!contains(summary, " mm"));
}

TEST_CASE("La unidad con que se guardó un diseño viaja en el archivo y no cuenta como cambio", "[units][io]")
{
    io::ViewState a;
    a.displayUnit = "in";
    io::ViewState b;
    b.displayUnit = "mm";
    REQUIRE(a == b);  // la unidad es solo de visualización
}
