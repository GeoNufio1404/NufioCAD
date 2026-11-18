// Edición del sketch (Fase 2c).

#include <nuf/sketch/Edit.hpp>
#include <nuf/sketch/Profiles.hpp>
#include <nuf/sketch/Sketch.hpp>
#include <nuf/sketch/Solver.hpp>
#include <nuf/sketch/Tools.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>

using namespace nuf::sketch;
using Catch::Matchers::WithinAbs;

namespace {

constexpr double kTol = 1e-6;
constexpr double kPi = std::numbers::pi;

SolveResult solve(Sketch& s)
{
    auto result = makePlaneGcsSolver()->solve(s);
    REQUIRE(result.status == SolveStatus::Solved);
    REQUIRE(result.conflicting.empty());
    REQUIRE(result.redundant.empty());
    return result;
}

std::vector<EntityId> curves(const Sketch& s)
{
    std::vector<EntityId> ids;
    for (const Entity& e : s.entities()) {
        if (!e.reference && !std::holds_alternative<Point>(e.geometry)) {
            ids.push_back(e.id);
        }
    }
    return ids;
}

const Line& line(const Sketch& s, EntityId id) { return std::get<Line>(s.entity(id)->geometry); }

std::size_t count(const Sketch& s, ConstraintType type)
{
    return std::ranges::count(s.constraints(), type, &Constraint::type);
}

// Rectángulo 40 × 25 desde el origen, como lo crea la herramienta.
std::vector<EntityId> rectangle(Sketch& s)
{
    auto tool = makeRectangleTool();
    tool->click(s, Snap{{0, 0}, SnapKind::Point, Ref::of(Sketch::kOrigin)});
    tool->click(s, Snap{{40, 25}});
    return curves(s);
}

// Dos verticales en x = 10 y x = 20 cruzando una horizontal de 0 a 30.
struct Crossing {
    Sketch s;
    EntityId h, v1, v2;
    Crossing()
    {
        h = s.addLine({0, 0}, {30, 0});
        v1 = s.addLine({10, -5}, {10, 5});
        v2 = s.addLine({20, -5}, {20, 5});
        for (EntityId v : {v1, v2}) {
            s.addConstraint(Constraint::fix(Ref::of(v)));
        }
        s.addConstraint(Constraint::horizontal(h));
    }
};

} // namespace

TEST_CASE("Recortar el tramo central de una línea", "[sketch][edit]")
{
    Crossing x;
    REQUIRE(trim(x.s, x.h, {15, 0.2}));
    const auto ids = curves(x.s);
    REQUIRE(ids.size() == 4);
    const Line& left = line(x.s, x.h);
    REQUIRE_THAT(left.end.x, WithinAbs(10.0, kTol));
    const Line& right = line(x.s, ids.back());
    REQUIRE_THAT(right.start.x, WithinAbs(20.0, kTol));
    REQUIRE(count(x.s, ConstraintType::Collinear) == 1);
    REQUIRE(count(x.s, ConstraintType::PointOnObject) == 2);
    solve(x.s);
}

TEST_CASE("Recortar un extremo, una línea suelta y un círculo", "[sketch][edit]")
{
    SECTION("Extremo")
    {
        Crossing x;
        REQUIRE(trim(x.s, x.h, {26, 0.2}));
        REQUIRE_THAT(line(x.s, x.h).end.x, WithinAbs(20.0, kTol));
        solve(x.s);
    }
    SECTION("Sin cortes: se borra")
    {
        Sketch s;
        EntityId l = s.addLine({0, 0}, {5, 5});
        REQUIRE(trim(s, l, {2, 2}));
        REQUIRE(s.entity(l) == nullptr);
    }
    SECTION("Círculo cortado por una línea: queda un arco")
    {
        Sketch s;
        EntityId c = s.addCircle({0, 0}, 10);
        EntityId l = s.addLine({-20, 6}, {20, 6});
        s.addConstraint(Constraint::fix(Ref::of(l)));
        REQUIRE(trim(s, c, {0, 10}));  // se quita la parte de arriba
        const auto& a = std::get<Arc>(s.entity(c)->geometry);
        REQUIRE_THAT(a.startPoint().y, WithinAbs(6.0, kTol));
        REQUIRE_THAT(a.endPoint().y, WithinAbs(6.0, kTol));
        REQUIRE(a.endAngle - a.startAngle > kPi);  // queda la parte grande
        solve(s);
    }
}

TEST_CASE("Partir una línea por sus cruces", "[sketch][edit]")
{
    Crossing x;
    REQUIRE(breakCurve(x.s, x.h, {15, 0.2}));
    REQUIRE(curves(x.s).size() == 5);
    REQUIRE(count(x.s, ConstraintType::Coincident) == 2);
    solve(x.s);
}

TEST_CASE("Extender una línea y un arco", "[sketch][edit]")
{
    Sketch s;
    EntityId l = s.addLine({0, 0}, {5, 0});
    EntityId wall = s.addLine({12, -5}, {12, 5});
    s.addConstraint(Constraint::fix(Ref::of(wall)));
    REQUIRE(extend(s, l, {4.5, 0}));
    REQUIRE_THAT(line(s, l).end.x, WithinAbs(12.0, kTol));
    solve(s);

    // Lejos de los ejes (extender también se detiene en ellos).
    EntityId arc = s.addArc({30, 30}, 10, 0.0, kPi / 4);
    EntityId floor = s.addLine({10, 30}, {50, 30});
    s.addConstraint(Constraint::fix(Ref::of(floor)));
    // Por su final (45°) sigue antihorario hasta cortar el suelo en 180°.
    REQUIRE(extend(s, arc, {37, 37}));
    const auto& a = std::get<Arc>(s.entity(arc)->geometry);
    REQUIRE_THAT(a.endAngle, WithinAbs(kPi, kTol));
}

TEST_CASE("Empalme y chaflán en una esquina de rectángulo", "[sketch][edit]")
{
    SECTION("Empalme")
    {
        Sketch s;
        rectangle(s);
        s.addConstraint(Constraint::length(curves(s)[0], 40));
        REQUIRE(fillet(s, {40, 25}, 5).has_value());
        solve(s);
        auto profiles = findProfiles(s);
        REQUIRE(profiles.size() == 1);
        REQUIRE_THAT(profiles[0].area, WithinAbs(1000.0 - 25.0 * (1.0 - kPi / 4.0), 0.05));
        REQUIRE_FALSE(fillet(s, {7, 7}, 5).has_value());  // ahí no hay esquina
    }
    SECTION("Chaflán")
    {
        Sketch s;
        rectangle(s);
        REQUIRE(chamfer(s, {0, 25}, 5).has_value());
        solve(s);
        REQUIRE_THAT(findProfiles(s)[0].area, WithinAbs(1000.0 - 12.5, 1e-6));
    }
    SECTION("Demasiado grande: no cabe")
    {
        Sketch s;
        rectangle(s);
        REQUIRE_FALSE(fillet(s, {40, 25}, 30).has_value());
    }
}

TEST_CASE("Desfase de un rectángulo y de una cadena línea-arco", "[sketch][edit]")
{
    SECTION("Rectángulo hacia dentro")
    {
        Sketch s;
        const auto rect = rectangle(s);
        const auto chain = chainOf(s, rect[0]);
        REQUIRE(chain.size() == 4);
        const auto inner = offset(s, chain, 5);
        REQUIRE(inner.size() == 4);
        solve(s);
        auto profiles = findProfiles(s);
        REQUIRE(profiles.size() == 2);
        std::vector<double> areas{profiles[0].area, profiles[1].area};
        std::ranges::sort(areas);
        REQUIRE_THAT(areas[0], WithinAbs(30.0 * 15.0, 1e-6));
    }
    SECTION("Cadena con tangencia")
    {
        Sketch s;
        EntityId l = s.addLine({0, 0}, {20, 0});
        EntityId a = s.addArc({20, 10}, 10, -kPi / 2, 0.0);
        s.addConstraint(Constraint::tangentAt(Ref::end(l), Ref::start(a)));
        const auto out = offset(s, chainOf(s, l), -3);  // a la derecha: por fuera
        REQUIRE(out.size() == 2);
        REQUIRE_THAT(std::get<Arc>(s.entity(out[1])->geometry).radius, WithinAbs(13.0, kTol));
        solve(s);
    }
}

TEST_CASE("Simetría respecto al eje Y", "[sketch][edit]")
{
    Sketch s;
    EntityId l = s.addLine({2, 1}, {10, 6});
    EntityId c = s.addCircle({5, -4}, 2);
    EntityId a = s.addArc({8, 8}, 3, 0.0, kPi / 2);
    const auto copies = mirror(s, {l, c, a}, Sketch::kAxisY);
    REQUIRE(copies.size() == 3);
    REQUIRE_THAT(line(s, copies[0]).end.x, WithinAbs(-10.0, kTol));
    solve(s);
    // Al mover el original, la copia lo sigue.
    s.entity(l)->geometry = Line{{3, 1}, {12, 6}};
    s.addConstraint(Constraint::fix(Ref::of(l)));
    solve(s);
    REQUIRE_THAT(line(s, copies[0]).end.x, WithinAbs(-12.0, kTol));
}

TEST_CASE("Patrones y transformaciones", "[sketch][edit]")
{
    SECTION("Patrón rectangular 3 × 2")
    {
        Sketch s;
        EntityId c = s.addCircle({0, 0}, 1);
        s.addConstraint(Constraint::diameter(c, 2));
        const auto copies = rectangularPattern(s, {c}, 3, 2, {10, 5});
        REQUIRE(copies.size() == 5);
        REQUIRE(count(s, ConstraintType::Diameter) == 6);  // cada copia conserva su cota
        solve(s);
    }
    SECTION("Patrón circular de 6 en vuelta completa")
    {
        Sketch s;
        EntityId p = s.addPoint({10, 0});
        const auto copies = circularPattern(s, {p}, {0, 0}, 6, 2 * kPi);
        REQUIRE(copies.size() == 5);
        const auto q = s.point(Ref::of(copies[0]));
        REQUIRE_THAT(std::atan2(q->y, q->x), WithinAbs(kPi / 3, kTol));
    }
    SECTION("Girar 30° quita horizontales y verticales; escalar escala las cotas")
    {
        Sketch s;
        const auto rect = rectangle(s);
        s.addConstraint(Constraint::length(rect[0], 40));
        Similarity rot;
        rot.angle = kPi / 6;
        transform(s, rect, rot, false);
        REQUIRE(count(s, ConstraintType::Horizontal) == 0);

        Sketch t;
        const auto r2 = rectangle(t);
        t.addConstraint(Constraint::length(r2[0], 40));
        Similarity twice;
        twice.scale = 2;
        const auto copy = transform(t, r2, twice, true);
        REQUIRE(copy.size() == 4);
        const auto lengths = std::ranges::count_if(t.constraints(), [](const Constraint& k) {
            return k.type == ConstraintType::Distance && std::abs(k.value - 80.0) < 1e-9;
        });
        REQUIRE(lengths == 1);
        solve(t);
    }
}

TEST_CASE("Recortar una elipse contra una recta deja un arco de elipse", "[sketch][edit][ellipse]")
{
    // Elipse de semiejes 10 y 5 en el origen; la recta x = 0 la corta en (0, ±5).
    Sketch s;
    const EntityId ellipse = s.addEllipse({{0, 0}, 10.0, 5.0, 0.0});
    s.addLine({0, -10}, {0, 10});
    // Clic a la derecha (parámetro 0): se quita la mitad derecha y queda la izquierda.
    REQUIRE(trim(s, ellipse, {10.0, 0.0}));
    const auto* arc = std::get_if<EllipseArc>(&s.entity(ellipse)->geometry);
    REQUIRE(arc != nullptr);
    const double pi = std::numbers::pi;
    REQUIRE_THAT(arc->startParam, Catch::Matchers::WithinAbs(pi / 2.0, 1e-3));
    REQUIRE_THAT(arc->endParam, Catch::Matchers::WithinAbs(3.0 * pi / 2.0, 1e-3));
    // Los extremos nuevos quedan sobre la recta.
    REQUIRE(std::abs(arc->startPoint().x) < 1e-3);
    REQUIRE(std::abs(arc->endPoint().x) < 1e-3);
    // Y se unieron a la recta con «punto sobre objeto».
    int onObject = 0;
    for (const Constraint& c : s.constraints()) {
        onObject += c.type == ConstraintType::PointOnObject ? 1 : 0;
    }
    REQUIRE(onObject == 2);
}

TEST_CASE("Recortar un arco de elipse por dos cortes parte el arco en dos y sin cortes lo borra", "[sketch][edit][ellipse]")
{
    const double pi = std::numbers::pi;
    Sketch s;
    // Arco de elipse de 0 a 270° y dos rectas verticales que lo cortan (x = 6 y x = -6).
    const EntityId arc = s.addEllipseArc({{{0, 0}, 10.0, 5.0, 0.0}, 0.0, 1.5 * pi});
    s.addLine({6, -10}, {6, 10});
    s.addLine({-6, -10}, {-6, 10});
    // Clic en la parte alta del arco (x ≈ 0, y ≈ 5): se quita ese tramo entre x = 6 y x = -6.
    REQUIRE(trim(s, arc, {0.0, 5.0}));
    int arcs = 0;
    for (const Entity& e : s.entities()) {
        arcs += std::holds_alternative<EllipseArc>(e.geometry) ? 1 : 0;
    }
    REQUIRE(arcs == 2);

    // Un arco sin ningún corte se borra entero.
    Sketch lone;
    const EntityId only = lone.addEllipseArc({{{0, 0}, 10.0, 5.0, 0.0}, 0.0, pi});
    REQUIRE(trim(lone, only, {0.0, 5.0}));
    REQUIRE(lone.entity(only) == nullptr);
}
