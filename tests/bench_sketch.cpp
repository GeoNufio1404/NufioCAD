// Banco de medidas del sketch (oculto: se ejecuta con «nuf_tests [.bench]»).
// Imprime cuánto tarda cada operación con sketches grandes; sirve para
// comparar antes y después de cambiar algo (y entre Debug y Release).

#include <nuf/sketch/Annotations.hpp>
#include <nuf/sketch/Pick.hpp>
#include <nuf/sketch/Profiles.hpp>
#include <nuf/sketch/Sketch.hpp>
#include <nuf/sketch/Snap.hpp>
#include <nuf/sketch/Solver.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdio>
#include <functional>

using namespace nuf::sketch;

namespace {

double millis(const std::function<void()>& work, int repeat = 1)
{
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < repeat; ++i) {
        work();
    }
    const auto end = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(end - start).count() / repeat;
}

// Rejilla de columns × rows casillas con sus líneas unidas por los extremos.
Sketch grid(int columns, int rows)
{
    Sketch s;
    for (int i = 0; i <= columns; ++i) {
        for (int j = 0; j < rows; ++j) {
            s.addLine({5.0 * i, 5.0 * j}, {5.0 * i, 5.0 * (j + 1)});
        }
    }
    for (int j = 0; j <= rows; ++j) {
        for (int i = 0; i < columns; ++i) {
            s.addLine({5.0 * i, 5.0 * j}, {5.0 * (i + 1), 5.0 * j});
        }
    }
    return s;
}

} // namespace

TEST_CASE("Banco del sketch", "[.bench]")
{
    const Sketch big = grid(30, 30);  // 1860 líneas
    std::printf("\n== Banco del sketch (%zu entidades) ==\n", big.entities().size());

    SnapOptions options;
    options.tolerance = 0.6;
    options.gridStep = 1.0;
    options.alignTolerance = 0.5;
    options.alignPoints = {{{0, 0}, Ref::of(Sketch::kOrigin)}};
    std::printf("snap por movimiento del cursor: %.3f ms\n", millis([&] {
                    for (int k = 0; k < 200; ++k) {
                        snap(big, {0.37 * k, 0.51 * k}, options);
                    }
                }) / 200.0);
    std::printf("pick por clic: %.3f ms\n", millis([&] {
                    for (int k = 0; k < 200; ++k) {
                        pick(big, {0.37 * k, 0.51 * k}, 0.6);
                    }
                }) / 200.0);
    std::printf("copiar el sketch (un paso de Deshacer): %.3f ms\n", millis([&] { Sketch copy = big; (void)copy; }, 50));
    std::printf("perfiles (findProfiles): %.1f ms\n", millis([&] { findProfiles(big); }, 3));

    Sketch dims = grid(20, 20);  // 840 líneas
    for (const Entity& e : dims.entities()) {
        if (std::holds_alternative<Line>(e.geometry) && e.id % 2 == 0) {
            dims.addConstraint(Constraint::length(e.id, 5.0));
        }
    }
    std::printf("colocar las cotas (%zu restricciones, zoom 0,05): %.1f ms\n", dims.constraints().size(),
                millis([&] { layoutAnnotations(dims, 0.05); }, 5));
    std::printf("colocar las cotas (zoom 1): %.1f ms\n", millis([&] { layoutAnnotations(dims, 1.0); }, 5));

    auto solver = makePlaneGcsSolver();
    for (const int count : {25, 50, 100, 150}) {
        Sketch chain;
        EntityId previous = 0;
        for (int i = 0; i < count; ++i) {
            const EntityId line = chain.addLine({5.0 * i, 0.3 * (i % 3)}, {5.0 * (i + 1), 0.3 * ((i + 1) % 3)});
            if (previous != 0) {
                chain.addConstraint(Constraint::coincident(Ref::start(line), Ref::end(previous)));
            }
            chain.addConstraint(Constraint::length(line, 5.0));
            previous = line;
        }
        std::printf("resolver %d líneas encadenadas con %zu restricciones: %.1f ms\n", count, chain.constraints().size(),
                    millis([&] { Sketch copy = chain; solver->solve(copy); }, 2));
    }
}
