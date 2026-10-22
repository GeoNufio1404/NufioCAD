#include <nuf/sketch/Annotations.hpp>
#include <nuf/sketch/FromSelection.hpp>
#include <nuf/sketch/Solver.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <numbers>

using namespace nuf::sketch;
using Catch::Matchers::WithinAbs;

TEST_CASE("Restricciones desde la selección", "[sketch][selection]")
{
    Sketch s;
    EntityId a = s.addLine({0, 0}, {10, 1});
    EntityId b = s.addLine({0, 5}, {10, 7});
    EntityId c = s.addLine({20, 0}, {21, 9});
    EntityId circle = s.addCircle({30, 30}, 4);
    EntityId arc = s.addArc({50, 0}, 5, 0, 2);
    EntityId p = s.addPoint({4, 4});

    SECTION("Horizontal: una por línea, o dos puntos")
    {
        auto r = constraintsFromSelection(ConstraintType::Horizontal, s, {Ref::of(a), Ref::of(b)});
        REQUIRE(r.size() == 2);
        REQUIRE(r[1].first.entity == b);
        REQUIRE(constraintsFromSelection(ConstraintType::Horizontal, s, {Ref::start(a), Ref::of(p)}).size() == 1);
        REQUIRE(constraintsFromSelection(ConstraintType::Horizontal, s, {Ref::of(circle)}).empty());
    }
    SECTION("Coincidente: dos puntos o punto sobre curva")
    {
        auto r = constraintsFromSelection(ConstraintType::Coincident, s, {Ref::end(a), Ref::start(c)});
        REQUIRE(r.size() == 1);
        REQUIRE(r[0].type == ConstraintType::Coincident);
        r = constraintsFromSelection(ConstraintType::Coincident, s, {Ref::of(p), Ref::of(circle)});
        REQUIRE(r.size() == 1);
        REQUIRE(r[0].type == ConstraintType::PointOnObject);
    }
    SECTION("Paralela e igual: la primera con cada una")
    {
        auto r = constraintsFromSelection(ConstraintType::Parallel, s, {Ref::of(a), Ref::of(b), Ref::of(c)});
        REQUIRE(r.size() == 2);
        REQUIRE(r[0].first.entity == a);
        REQUIRE(r[1].second.entity == c);
        REQUIRE(constraintsFromSelection(ConstraintType::Equal, s, {Ref::of(circle), Ref::of(arc)}).size() == 1);
        // Mezcla de línea y círculo: no.
        REQUIRE(constraintsFromSelection(ConstraintType::Equal, s, {Ref::of(a), Ref::of(circle)}).empty());
    }
    SECTION("Tangente, punto medio, simetría y fijo")
    {
        REQUIRE(constraintsFromSelection(ConstraintType::Tangent, s, {Ref::of(a), Ref::of(circle)}).size() == 1);
        REQUIRE(constraintsFromSelection(ConstraintType::Tangent, s, {Ref::of(a), Ref::of(b)}).empty());
        REQUIRE(constraintsFromSelection(ConstraintType::Midpoint, s, {Ref::of(p), Ref::of(c)}).size() == 1);
        REQUIRE(constraintsFromSelection(ConstraintType::Symmetric, s,
                                         {Ref::start(a), Ref::start(b), Ref::of(Sketch::kAxisY)})
                    .size() == 1);
        REQUIRE(constraintsFromSelection(ConstraintType::Fix, s, {Ref::of(p), Ref::of(c)}).size() == 2);
    }
}

TEST_CASE("Cotas desde la selección con el valor actual", "[sketch][selection]")
{
    Sketch s;
    EntityId a = s.addLine({0, 0}, {30, 40});
    EntityId h = s.addLine({0, 10}, {50, 10});
    EntityId h2 = s.addLine({0, 25}, {50, 25});
    EntityId circle = s.addCircle({0, 0}, 4);
    EntityId arc = s.addArc({0, 0}, 6, 0, 1);

    auto d = dimensionFromSelection(s, {Ref::of(a)});
    REQUIRE(d);
    REQUIRE(d->type == ConstraintType::Distance);
    REQUIRE_THAT(d->value, WithinAbs(50.0, 1e-9));

    REQUIRE(dimensionFromSelection(s, {Ref::of(circle)})->type == ConstraintType::Diameter);
    REQUIRE_THAT(dimensionFromSelection(s, {Ref::of(circle)})->value, WithinAbs(8.0, 1e-9));
    REQUIRE(dimensionFromSelection(s, {Ref::of(arc)})->type == ConstraintType::Radius);

    auto angle = dimensionFromSelection(s, {Ref::of(h), Ref::of(a)});
    REQUIRE(angle->type == ConstraintType::Angle);
    REQUIRE_THAT(angle->value, WithinAbs(std::atan2(4.0, 3.0), 1e-9));

    // Dos paralelas: distancia entre ellas.
    auto gap = dimensionFromSelection(s, {Ref::of(h), Ref::of(h2)});
    REQUIRE(gap->type == ConstraintType::Distance);
    REQUIRE_THAT(gap->value, WithinAbs(15.0, 1e-9));

    REQUIRE_FALSE(dimensionFromSelection(s, {Ref::of(a), Ref::of(circle)}));
}

TEST_CASE("Anotaciones: iconos apilados, cotas y selección", "[sketch][annotations]")
{
    Sketch s;
    EntityId line = s.addLine({0, 0}, {40, 0});
    ConstraintId h = s.addConstraint(Constraint::horizontal(line));
    ConstraintId fix = s.addConstraint(Constraint::fix(Ref::of(line)));
    ConstraintId len = s.addConstraint(Constraint::length(line, 40.0));
    Constraint reference = Constraint::length(line, 40.0);
    reference.driving = false;
    s.addConstraint(reference);

    const double px = 0.1;  // 1 px = 0.1 mm
    auto notes = layoutAnnotations(s, px);
    REQUIRE(notes.size() == 4);
    REQUIRE(notes[0].text == "H");
    REQUIRE(notes[1].text == "F");
    // Los dos iconos de la línea no se superponen.
    REQUIRE(std::abs(notes[1].anchor.x - notes[0].anchor.x) > 1.0);
    REQUIRE(notes[2].text == "40");
    REQUIRE(notes[2].dimension);
    REQUIRE(notes[3].text == "(40)");

    REQUIRE(pickAnnotation(notes, notes[0].anchor, px) == h);
    REQUIRE(pickAnnotation(notes, notes[1].anchor, px) == fix);
    REQUIRE(pickAnnotation(notes, notes[2].anchor, px) == len);
    REQUIRE(pickAnnotation(notes, {20, -30}, px) == 0);

    Constraint angle = Constraint::angle(1, 2, -std::numbers::pi / 6);
    REQUIRE(formatDimension(angle) == "30°");
    REQUIRE(formatDimension(Constraint::diameter(1, 12.5)) == "Ø12.5");
}
