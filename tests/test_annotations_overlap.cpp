#include <nuf/sketch/Annotations.hpp>
#include <nuf/sketch/Solver.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>

using namespace nuf::sketch;

namespace {

// Caja del texto como la calcula la colocación: unos 8 px por carácter + 8 px
// de margen, 18 px de alto, centrada en el ancla.
struct Box {
    double x0, y0, x1, y1;
};

Box boxOf(const Annotation& n, double px)
{
    std::size_t chars = 0;
    for (const char ch : n.text) {
        chars += (static_cast<unsigned char>(ch) & 0xC0) != 0x80 ? 1 : 0;
    }
    const double hw = 0.5 * (double(chars) * 8.0 + 8.0) * px;
    const double hh = 9.0 * px;
    return {n.anchor.x - hw, n.anchor.y - hh, n.anchor.x + hw, n.anchor.y + hh};
}

int overlappingPairs(const std::vector<Annotation>& notes, double px)
{
    int count = 0;
    for (std::size_t i = 0; i < notes.size(); ++i) {
        for (std::size_t j = i + 1; j < notes.size(); ++j) {
            const Box a = boxOf(notes[i], px);
            const Box b = boxOf(notes[j], px);
            if (std::min(a.x1, b.x1) - std::max(a.x0, b.x0) > 1e-9 && std::min(a.y1, b.y1) - std::max(a.y0, b.y0) > 1e-9) {
                ++count;
            }
        }
    }
    return count;
}

} // namespace

TEST_CASE("Cotas: dos líneas paralelas a 1 mm no se pisan a ningún zoom", "[sketch][annotations]")
{
    Sketch s;
    const EntityId a = s.addLine({0, 0}, {40, 0});
    const EntityId b = s.addLine({0, 1}, {40, 1});
    s.addConstraint(Constraint::length(a, 40.0));
    s.addConstraint(Constraint::length(b, 40.0));
    for (const double px : {0.02, 0.1, 0.5, 2.0}) {
        const auto notes = layoutAnnotations(s, px);
        REQUIRE(notes.size() == 2);
        REQUIRE(overlappingPairs(notes, px) == 0);
    }
}

TEST_CASE("Cotas: un rectángulo pequeño con sus cuatro cotas y sus iconos no se pisa", "[sketch][annotations]")
{
    Sketch s;
    const EntityId bottom = s.addLine({0, 0}, {6, 0});
    const EntityId right = s.addLine({6, 0}, {6, 4});
    const EntityId top = s.addLine({6, 4}, {0, 4});
    const EntityId left = s.addLine({0, 4}, {0, 0});
    s.addConstraint(Constraint::horizontal(bottom));
    s.addConstraint(Constraint::horizontal(top));
    s.addConstraint(Constraint::vertical(right));
    s.addConstraint(Constraint::vertical(left));
    for (const EntityId line : {bottom, right, top, left}) {
        s.addConstraint(Constraint::length(line, line == bottom || line == top ? 6.0 : 4.0));
    }
    for (const double px : {0.02, 0.1, 0.5}) {
        const auto notes = layoutAnnotations(s, px);
        REQUIRE(notes.size() == 8);
        REQUIRE(overlappingPairs(notes, px) == 0);
    }
}

TEST_CASE("Cotas: una cota y un icono de la misma línea no se pisan", "[sketch][annotations]")
{
    Sketch s;
    const EntityId line = s.addLine({0, 0}, {40, 0});
    s.addConstraint(Constraint::horizontal(line));
    s.addConstraint(Constraint::fix(Ref::of(line)));
    s.addConstraint(Constraint::length(line, 40.0));
    const double px = 0.1;
    REQUIRE(overlappingPairs(layoutAnnotations(s, px), px) == 0);
}

TEST_CASE("Cotas: diez círculos concéntricos no se pisan", "[sketch][annotations]")
{
    Sketch s;
    for (int i = 0; i < 10; ++i) {
        const EntityId c = s.addCircle({0, 0}, 10.0 + 0.5 * i);
        s.addConstraint(Constraint::diameter(c, 2.0 * (10.0 + 0.5 * i)));
    }
    for (const double px : {0.05, 0.2}) {
        const auto notes = layoutAnnotations(s, px);
        REQUIRE(notes.size() == 10);
        REQUIRE(overlappingPairs(notes, px) == 0);
    }
}

TEST_CASE("Cotas: 500 cotas se colocan en poco tiempo y sin pisarse entre sí en lo posible", "[sketch][annotations]")
{
    Sketch s;
    for (int i = 0; i < 500; ++i) {
        const double x = 20.0 * (i % 25);
        const double y = 20.0 * (i / 25);
        const EntityId line = s.addLine({x, y}, {x + 10, y});
        s.addConstraint(Constraint::length(line, 10.0));
    }
    const auto start = std::chrono::steady_clock::now();
    const auto notes = layoutAnnotations(s, 0.1);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    REQUIRE(notes.size() == 500);
    REQUIRE(overlappingPairs(notes, 0.1) == 0);
    REQUIRE(ms < 2000.0);  // holgado (Debug): lo que se vigila es que no sea cuadrático
}
