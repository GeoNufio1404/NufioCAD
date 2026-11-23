// Recortar y extender con elipses y splines (categoría S de los pendientes): una línea que se corta o se alarga contra
// una elipse o una spline, un arco de elipse que se alarga y una spline que se recorta o se alarga.

#include <nuf/sketch/Edit.hpp>
#include <nuf/sketch/Sketch.hpp>
#include <nuf/sketch/Tools.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>

using namespace nuf::sketch;
using Catch::Matchers::WithinAbs;

namespace {

Ellipse horizontalEllipse(double major, double minor)
{
    Ellipse e;
    e.center = {0.0, 0.0};
    e.majorRadius = major;
    e.minorRadius = minor;
    e.rotation = 0.0;
    return e;
}

std::size_t count(const Sketch& sketch, ConstraintType type)
{
    return std::ranges::count(sketch.constraints(), type, &Constraint::type);
}

std::size_t countSplines(const Sketch& sketch)
{
    return std::ranges::count_if(sketch.entities(), [](const Entity& e) { return std::holds_alternative<BSpline>(e.geometry); });
}

} // namespace

TEST_CASE("Recortar una línea contra una elipse", "[sketch][edit][s]")
{
    Sketch sketch;
    sketch.addEllipse(horizontalEllipse(10.0, 5.0));
    const EntityId line = sketch.addLine({-20.0, 0.0}, {20.0, 0.0});

    // El clic fuera de la elipse quita el tramo exterior: la línea acaba en el borde de la elipse (x = 10).
    REQUIRE(trim(sketch, line, {15.0, 0.0}));
    const Line left = std::get<Line>(sketch.entity(line)->geometry);
    REQUIRE_THAT(left.start.x, WithinAbs(-20.0, 1e-9));
    REQUIRE_THAT(left.end.x, WithinAbs(10.0, 1e-2));
    REQUIRE(count(sketch, ConstraintType::PointOnObject) == 1);
}

TEST_CASE("Extender una línea hasta una elipse", "[sketch][edit][s]")
{
    Sketch sketch;
    sketch.addEllipse(horizontalEllipse(10.0, 5.0));
    const EntityId line = sketch.addLine({-30.0, 0.0}, {-20.0, 0.0});

    REQUIRE(extend(sketch, line, {-21.0, 0.0}));
    const Line grown = std::get<Line>(sketch.entity(line)->geometry);
    REQUIRE_THAT(grown.end.x, WithinAbs(-10.0, 1e-2));
    REQUIRE(count(sketch, ConstraintType::PointOnObject) == 1);
}

TEST_CASE("Extender un arco de elipse hasta una línea", "[sketch][edit][s]")
{
    Sketch sketch;
    // Arco de la elipse de 10 × 5 entre 0,2 y 1,0 rad; la línea x = −5 corta la elipse arriba en t = 2π/3.
    const EntityId arc = sketch.addEllipseArc({horizontalEllipse(10.0, 5.0), 0.2, 1.0});
    sketch.addLine({-5.0, -20.0}, {-5.0, 20.0});

    REQUIRE(extend(sketch, arc, std::get<EllipseArc>(sketch.entity(arc)->geometry).endPoint()));
    const EllipseArc grown = std::get<EllipseArc>(sketch.entity(arc)->geometry);
    REQUIRE_THAT(grown.startParam, WithinAbs(0.2, 1e-12));
    REQUIRE_THAT(grown.endParam, WithinAbs(2.0 * std::numbers::pi / 3.0, 1e-4));
    REQUIRE(count(sketch, ConstraintType::PointOnObject) == 1);
}

TEST_CASE("Extender una spline hasta una línea", "[sketch][edit][s]")
{
    Sketch sketch;
    const EntityId spline = sketch.addBSpline(interpolatingSpline({{0.0, 0.0}, {5.0, 2.0}, {10.0, 3.0}, {15.0, 2.0}, {20.0, 0.0}}));
    const EntityId wall = sketch.addLine({24.0, -30.0}, {24.0, 30.0});

    REQUIRE(extend(sketch, spline, {19.0, 0.5}));
    const BSpline grown = std::get<BSpline>(sketch.entity(spline)->geometry);
    REQUIRE_THAT(grown.poles.back().x, WithinAbs(24.0, 1e-6));
    REQUIRE_THAT(grown.poles.front().x, WithinAbs(0.0, 1e-9));
    const auto onWall = std::ranges::count_if(sketch.constraints(), [&](const Constraint& c) {
        return c.type == ConstraintType::PointOnObject && c.second == Ref::of(wall) && c.first == Ref::end(spline);
    });
    REQUIRE(onWall == 1);
}

TEST_CASE("Extender una spline por el inicio", "[sketch][edit][s]")
{
    Sketch sketch;
    const EntityId spline = sketch.addBSpline(interpolatingSpline({{0.0, 0.0}, {5.0, 2.0}, {10.0, 3.0}, {15.0, 2.0}, {20.0, 0.0}}));
    sketch.addLine({-4.0, -30.0}, {-4.0, 30.0});

    REQUIRE(extend(sketch, spline, {0.5, 0.2}));
    const BSpline grown = std::get<BSpline>(sketch.entity(spline)->geometry);
    REQUIRE_THAT(grown.poles.front().x, WithinAbs(-4.0, 1e-6));
    REQUIRE_THAT(grown.poles.back().x, WithinAbs(20.0, 1e-9));
}

TEST_CASE("Recortar una spline: el tramo exterior, el del medio y toda sin cortes", "[sketch][edit][s]")
{
    const auto make = [](Sketch& sketch) {
        return sketch.addBSpline(interpolatingSpline({{0.0, 0.0}, {10.0, 6.0}, {20.0, -6.0}, {30.0, 0.0}}));
    };
    SECTION("un corte: se quita el tramo del clic")
    {
        Sketch sketch;
        const EntityId spline = make(sketch);
        sketch.addLine({15.0, -30.0}, {15.0, 30.0});
        REQUIRE(trim(sketch, spline, {27.0, -1.0}));
        const BSpline kept = std::get<BSpline>(sketch.entity(spline)->geometry);
        REQUIRE_THAT(kept.poles.front().x, WithinAbs(0.0, 1e-9));
        REQUIRE_THAT(kept.poles.back().x, WithinAbs(15.0, 1e-4));
        REQUIRE(countSplines(sketch) == 1);
    }
    SECTION("dos cortes: queda un tramo a cada lado")
    {
        Sketch sketch;
        const EntityId spline = make(sketch);
        sketch.addLine({10.0, -30.0}, {10.0, 30.0});
        sketch.addLine({20.0, -30.0}, {20.0, 30.0});
        REQUIRE(trim(sketch, spline, {15.0, 0.0}));
        REQUIRE(countSplines(sketch) == 2);
        const BSpline first = std::get<BSpline>(sketch.entity(spline)->geometry);
        REQUIRE_THAT(first.poles.front().x, WithinAbs(0.0, 1e-9));
        REQUIRE_THAT(first.poles.back().x, WithinAbs(10.0, 1e-4));
        // El otro tramo empieza en el segundo corte y acaba donde acababa la spline.
        for (const Entity& e : sketch.entities()) {
            if (e.id != spline && std::holds_alternative<BSpline>(e.geometry)) {
                const BSpline second = std::get<BSpline>(e.geometry);
                REQUIRE_THAT(second.poles.front().x, WithinAbs(20.0, 1e-4));
                REQUIRE_THAT(second.poles.back().x, WithinAbs(30.0, 1e-9));
            }
        }
    }
    SECTION("sin cortes se borra entera")
    {
        Sketch sketch;
        const EntityId spline = make(sketch);
        REQUIRE(trim(sketch, spline, {15.0, 0.0}));
        REQUIRE(sketch.entity(spline) == nullptr);
    }
}

TEST_CASE("Recortar una spline contra una elipse", "[sketch][edit][s]")
{
    Sketch sketch;
    const EntityId spline = sketch.addBSpline(interpolatingSpline({{-30.0, 1.0}, {-10.0, 0.5}, {10.0, 0.5}, {30.0, 1.0}}));
    sketch.addEllipse(horizontalEllipse(12.0, 6.0));
    // La spline atraviesa la elipse: el clic dentro de ella quita el tramo interior y quedan dos splines.
    REQUIRE(trim(sketch, spline, {0.0, 0.5}));
    REQUIRE(countSplines(sketch) == 2);
}

TEST_CASE("Volver a ligar lo que tocaba a unas entidades sustituidas (texto editado)", "[sketch][edit][s]")
{
    Sketch sketch;
    // Dos «letras» y un punto suelto de fuera, coincidente con el extremo de la primera; una cota entre las dos letras.
    const EntityId a = sketch.addLine({0.0, 0.0}, {10.0, 0.0});
    const EntityId b = sketch.addLine({20.0, 0.0}, {30.0, 0.0});
    const EntityId free = sketch.addPoint({10.0, 0.0});
    sketch.addConstraint(Constraint::coincident(Ref::of(free), Ref::end(a)));
    sketch.addConstraint(Constraint::distance(Ref::start(a), Ref::start(b), 20.0));

    const std::vector<Constraint> saved = constraintsOn(sketch, {a, b});
    REQUIRE(saved.size() == 2);
    sketch.removeEntity(a);
    sketch.removeEntity(b);
    REQUIRE(sketch.constraints().empty());

    // Letras nuevas (más grandes: la cota entre ellas ya no mide 20 y no cabe; la coincidencia sí).
    sketch.entity(free)->locked = false;
    const EntityId a2 = sketch.addLine({0.0, 0.0}, {14.0, 0.0});
    const EntityId b2 = sketch.addLine({30.0, 0.0}, {44.0, 0.0});
    sketch.entity(a2)->locked = true;
    sketch.entity(b2)->locked = true;
    const std::size_t kept = restoreConstraints(sketch, saved, {{a, a2}, {b, b2}});
    REQUIRE(kept == 1);
    REQUIRE(count(sketch, ConstraintType::Coincident) == 1);
    REQUIRE(count(sketch, ConstraintType::Distance) == 0);
    // El punto de fuera sigue al extremo de la letra nueva.
    REQUIRE_THAT(sketch.point(Ref::of(free))->x, WithinAbs(14.0, 1e-6));

    // Sin pareja, nada se vuelve a ligar.
    REQUIRE(restoreConstraints(sketch, saved, {}) == 0);
}
