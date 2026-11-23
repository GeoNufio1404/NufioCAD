#include <nuf/sketch/Annotations.hpp>
#include <nuf/sketch/Solver.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <variant>

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

TEST_CASE("Cotas: círculos cercanos (no concéntricos) no se pisan", "[sketch][annotations]")
{
    Sketch s;
    for (int i = 0; i < 6; ++i) {
        const EntityId c = s.addCircle({4.0 * i, 0.0}, 5.0);
        s.addConstraint(Constraint::diameter(c, 10.0));
    }
    for (const double px : {0.05, 0.2}) {
        const auto notes = layoutAnnotations(s, px);
        REQUIRE(notes.size() == 6);
        REQUIRE(overlappingPairs(notes, px) == 0);
    }
}

TEST_CASE("Cotas: el ángulo se dibuja en la esquina de los segmentos, sea cual sea su sentido", "[sketch][annotations]")
{
    // El primer segmento llega al vértice y el segundo sale de él: el ángulo
    // entre sus direcciones es el de la esquina, pero el arco debe quedar
    // entre los segmentos (x > 0, y > 0), no en el cuadrante opuesto.
    Sketch s;
    const EntityId a = s.addLine({10, 0}, {0, 0});
    const EntityId b = s.addLine({0, 0}, {0, 10});
    s.addConstraint(Constraint::angle(a, b, 3.14159265358979 / 2));
    const auto notes = layoutAnnotations(s, 0.05);
    REQUIRE(notes.size() == 1);
    const Arc* arc = std::get_if<Arc>(&notes.front().lines.front());
    REQUIRE(arc != nullptr);
    REQUIRE(std::abs(arc->center.x) < 1e-9);
    REQUIRE(std::abs(arc->center.y) < 1e-9);
    REQUIRE(notes.front().anchor.x > 0.0);
    REQUIRE(notes.front().anchor.y > 0.0);

    // Con los dos segmentos saliendo del vértice, igual.
    Sketch t;
    const EntityId c = t.addLine({0, 0}, {10, 0});
    const EntityId d = t.addLine({0, 0}, {0, 10});
    t.addConstraint(Constraint::angle(c, d, 3.14159265358979 / 2));
    const auto other = layoutAnnotations(t, 0.05);
    REQUIRE(other.front().anchor.x > 0.0);
    REQUIRE(other.front().anchor.y > 0.0);
}

TEST_CASE("Cotas: el texto se aparta de las líneas del dibujo", "[sketch][annotations]")
{
    const double px = 0.1;
    Sketch s;
    const EntityId line = s.addLine({0, 0}, {10, 0});
    s.addConstraint(Constraint::length(line, 10.0));
    const Vec2 natural = layoutAnnotations(s, px).front().anchor;
    // Una línea larga justo donde caería el texto.
    s.addLine({natural.x - 6.0, natural.y}, {natural.x + 6.0, natural.y});
    const auto notes = layoutAnnotations(s, px);
    REQUIRE(notes.size() == 1);
    const Vec2 moved = notes.front().anchor;
    REQUIRE(std::abs(moved.y - natural.y) > 9.0 * px * 0.99);  // ya no la tapa
}

TEST_CASE("Cotas: la posición elegida se recuerda y la seleccionada gana el hueco", "[sketch][annotations]")
{
    Sketch s;
    const EntityId a = s.addLine({0, 0}, {40, 0});
    const EntityId b = s.addLine({0, 1}, {40, 1});
    const ConstraintId ca = s.addConstraint(Constraint::length(a, 40.0));
    const ConstraintId cb = s.addConstraint(Constraint::length(b, 40.0));
    const double px = 0.1;
    const auto anchorOf = [](const std::vector<Annotation>& notes, ConstraintId id) {
        for (const Annotation& n : notes) {
            if (n.constraint == id) {
                return n.anchor;
            }
        }
        return Vec2{1e9, 1e9};
    };

    // Sin prioridad la primera cota ocupa su sitio natural y la segunda se aparta.
    LayoutMemory memory;
    const auto first = layoutAnnotations(s, px, &memory);
    const Vec2 naturalA = anchorOf(first, ca);
    const Vec2 movedB = anchorOf(first, cb);
    REQUIRE(memory.levels[ca] == 0);
    REQUIRE(memory.levels[cb] > 0);

    // Repintar igual no mueve nada.
    const auto again = layoutAnnotations(s, px, &memory);
    REQUIRE(anchorOf(again, ca).x == naturalA.x);
    REQUIRE(anchorOf(again, ca).y == naturalA.y);
    REQUIRE(anchorOf(again, cb).x == movedB.x);
    REQUIRE(anchorOf(again, cb).y == movedB.y);

    // Seleccionando la segunda, esta pasa a su sitio y la primera cede.
    memory.priority = {cb};
    const auto picked = layoutAnnotations(s, px, &memory);
    REQUIRE(memory.levels[cb] == 0);
    REQUIRE(memory.levels[ca] > 0);
    REQUIRE(overlappingPairs(picked, px) == 0);
}

TEST_CASE("Iconos de restricciones: se dibujan con líneas dentro de su caja y la curvatura usa texto", "[sketch][annotations]")
{
    const double px = 0.1;
    for (const ConstraintType type :
         {ConstraintType::Horizontal, ConstraintType::Vertical, ConstraintType::Parallel, ConstraintType::Perpendicular,
          ConstraintType::Tangent, ConstraintType::Equal, ConstraintType::Collinear, ConstraintType::Concentric,
          ConstraintType::Midpoint, ConstraintType::Symmetric, ConstraintType::Fix, ConstraintType::PointOnObject}) {
        const auto icon = glyphGeometry(type, {5.0, 5.0}, px);
        REQUIRE(!icon.empty());
        for (const Geometry& g : icon) {
            for (const Vec2& p : polyline(g)) {
                REQUIRE(std::abs(p.x - 5.0) <= 8.0 * px + 1e-9);
                REQUIRE(std::abs(p.y - 5.0) <= 8.0 * px + 1e-9);
            }
        }
    }
    REQUIRE(glyphGeometry(ConstraintType::Curvature, {0, 0}, px).empty());

    Sketch s;
    const EntityId a = s.addLine({0, 0}, {10, 0});
    s.addConstraint(Constraint::horizontal(a));
    const auto notes = layoutAnnotations(s, px);
    REQUIRE(notes.size() == 1);
    REQUIRE(!notes[0].icon.empty());
}

TEST_CASE("Cotas: sin hueco libre el texto sale de su línea con una línea de llamada", "[sketch][annotations]")
{
    Sketch s;
    // Veinte paralelas a 0,3 mm: no caben todas las cotas en sus ocho renglones.
    for (int i = 0; i < 20; ++i) {
        const EntityId line = s.addLine({0, 0.3 * i}, {40, 0.3 * i});
        s.addConstraint(Constraint::length(line, 40.0));
    }
    const double px = 0.1;
    const auto notes = layoutAnnotations(s, px);
    REQUIRE(notes.size() == 20);
    int callouts = 0;
    for (const Annotation& n : notes) {
        // Una cota alineada lleva tres líneas y dos flechas de dos trazos cada una; la llamada es una más.
        if (n.lines.size() == 8) {
            ++callouts;
            const auto* leader = std::get_if<Line>(&n.lines.back());
            REQUIRE(leader != nullptr);
            REQUIRE(std::hypot(leader->end.x - leader->start.x, leader->end.y - leader->start.y) > 0.0);
        }
    }
    REQUIRE(callouts >= 1);
}

TEST_CASE("Cotas: el texto de una cota no cae sobre la línea de cota de otra", "[sketch][annotations]")
{
    // Tres segmentos paralelos y muy juntos: sus líneas de cota (la tercera línea de cada anotación) quedan
    // cerca de los textos de las demás si no se tienen en cuenta.
    Sketch s;
    for (int i = 0; i < 3; ++i) {
        const EntityId line = s.addLine({0, 0.8 * i}, {30, 0.8 * i});
        s.addConstraint(Constraint::length(line, 30.0));
    }
    for (const double px : {0.05, 0.2, 0.6}) {
        const auto notes = layoutAnnotations(s, px);
        REQUIRE(notes.size() == 3);
        for (std::size_t i = 0; i < notes.size(); ++i) {
            for (std::size_t j = 0; j < notes.size(); ++j) {
                if (i == j || notes[j].lines.size() < 3) {
                    continue;
                }
                const auto* base = std::get_if<Line>(&notes[j].lines[2]);
                REQUIRE(base != nullptr);
                const Box box = boxOf(notes[i], px);
                // La línea de cota es horizontal aquí: no puede cruzar la caja del texto de otra cota.
                const double y = base->start.y;
                const bool within = y > box.y0 + 1e-9 && y < box.y1 - 1e-9;
                const bool overX = std::max(base->start.x, base->end.x) > box.x0 && std::min(base->start.x, base->end.x) < box.x1;
                REQUIRE_FALSE((within && overX));
            }
        }
    }
}

TEST_CASE("Cotas: arcos con el mismo centro (y círculos entre ellos) no se pisan", "[sketch][annotations]")
{
    Sketch s;
    // Cuatro arcos concéntricos que cubren el mismo trozo de circunferencia, y un círculo con el mismo centro.
    for (int i = 0; i < 4; ++i) {
        const EntityId a = s.addArc({0.0, 0.0}, 10.0 + 0.4 * i, 0.2, 1.4);
        s.addConstraint(Constraint::radius(a, 10.0 + 0.4 * i));
    }
    const EntityId c = s.addCircle({0.0, 0.0}, 14.0);
    s.addConstraint(Constraint::diameter(c, 28.0));
    for (const double px : {0.05, 0.2}) {
        const auto notes = layoutAnnotations(s, px);
        REQUIRE(notes.size() == 5);
        REQUIRE(overlappingPairs(notes, px) == 0);
    }
}

TEST_CASE("Cotas: la que sigue a una expresión lleva una ƒ delante", "[sketch][annotations]")
{
    Constraint plain = Constraint::length(2, 12.0);
    REQUIRE(formatDimension(plain) == "12");
    Constraint linked = plain;
    linked.expression = "grosor * 2";
    REQUIRE(formatDimension(linked) == "ƒ12");
    // Una cota de referencia no se liga a nada: sigue entre paréntesis.
    Constraint reference = plain;
    reference.driving = false;
    REQUIRE(formatDimension(reference) == "(12)");
}
