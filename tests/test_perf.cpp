// Medidas de rendimiento (no son pruebas de corrección): no se ejecutan con el
// resto. Uso: nuf_tests "[perf]"  →  imprime tiempos en milisegundos.
// Ojo: en Debug todo es varias veces más lento; sirve para comparar y ver cómo
// crece con el tamaño, no como cifra absoluta.

#include <nuf/document/Document.hpp>
#include <nuf/document/Features.hpp>
#include <nuf/geometry/Modeling.hpp>
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

using namespace nuf;
using Clock = std::chrono::steady_clock;

namespace {

double millis(const std::function<void()>& work, int repeat = 1)
{
    const auto start = Clock::now();
    for (int i = 0; i < repeat; ++i) {
        work();
    }
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count() / repeat;
}

// Una cuadrícula de n×n rectángulos separados, con sus restricciones
// (coincidencias, horizontales, verticales y un par de cotas).
sketch::Sketch grid(int n)
{
    sketch::Sketch s;
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < n; ++j) {
            const double x = i * 15.0;
            const double y = j * 15.0;
            const auto a = s.addLine({x, y}, {x + 10, y});
            const auto b = s.addLine({x + 10, y}, {x + 10, y + 10});
            const auto c = s.addLine({x + 10, y + 10}, {x, y + 10});
            const auto d = s.addLine({x, y + 10}, {x, y});
            s.addConstraint(sketch::Constraint::coincident(sketch::Ref::end(a), sketch::Ref::start(b)));
            s.addConstraint(sketch::Constraint::coincident(sketch::Ref::end(b), sketch::Ref::start(c)));
            s.addConstraint(sketch::Constraint::coincident(sketch::Ref::end(c), sketch::Ref::start(d)));
            s.addConstraint(sketch::Constraint::coincident(sketch::Ref::end(d), sketch::Ref::start(a)));
            s.addConstraint(sketch::Constraint::horizontal(a));
            s.addConstraint(sketch::Constraint::horizontal(c));
            s.addConstraint(sketch::Constraint::vertical(b));
            s.addConstraint(sketch::Constraint::vertical(d));
            s.addConstraint(sketch::Constraint::length(a, 10.0));
            s.addConstraint(sketch::Constraint::length(b, 10.0));
        }
    }
    return s;
}

} // namespace

TEST_CASE("Rendimiento del sketch según su tamaño", "[.][perf]")
{
    std::printf("\n%-10s %8s %8s %9s %9s %9s %9s %9s %9s\n", "rectáng.", "entid.", "restr.", "construir", "resolver",
                "perfiles", "anotac.", "pick+snap", "copiar");
    for (const int n : {3, 5, 10, 14}) {
        sketch::Sketch s;
        const double build = millis([&] { s = grid(n); });
        auto solver = sketch::makePlaneGcsSolver();
        const double solve = millis([&] { solver->solve(s); });
        const double profiles = millis([&] { sketch::findProfiles(s); });
        const double notes = millis([&] { sketch::layoutAnnotations(s, 0.1); }, 5);
        sketch::SnapOptions options;
        options.tolerance = 1.0;
        const double pointer = millis([&] {
            sketch::snap(s, {n * 7.0, n * 7.0}, options);
            sketch::pick(s, {n * 7.0, n * 7.0}, 1.0);
        }, 20);
        const double copy = millis([&] { sketch::Sketch other = s; (void)other; }, 20);
        std::printf("%-10d %8zu %8zu %9.2f %9.2f %9.2f %9.2f %9.3f %9.3f\n", n * n, s.entities().size(),
                    s.constraints().size(), build, solve, profiles, notes, pointer, copy);
    }
}

TEST_CASE("Rendimiento del historial según su largo", "[.][perf]")
{
    std::printf("\n%-14s %10s %14s %14s %12s\n", "operaciones", "construir", "editar la 1ª", "editar la últ.", "caras");
    for (const int pillars : {2, 8, 20}) {
        document::Document doc;
        document::SketchAttachment at;
        at.origin = document::OriginPlane::XY;
        const auto addBox = [&](double x, double y, double w, double h, double depth, document::Operation op) {
            sketch::Sketch s;
            s.addLine({x, y}, {x + w, y});
            s.addLine({x + w, y}, {x + w, y + h});
            s.addLine({x + w, y + h}, {x, y + h});
            s.addLine({x, y + h}, {x, y});
            const auto id = doc.addFeature(std::make_unique<document::SketchFeature>(s, at));
            const auto found = sketch::findProfiles(static_cast<const document::SketchFeature*>(doc.findFeature(id))->sketch());
            auto e = std::make_unique<document::ExtrudeFeature>();
            e->profiles.push_back(document::profileRef(id, found.front()));
            e->spec.side1.distance = depth;
            e->operation = op;
            return doc.addFeature(std::move(e));
        };
        const double build = millis([&] {
            addBox(0, 0, 200, 100, 10, document::Operation::NewBody);
            for (int i = 0; i < pillars; ++i) {
                // Pilares sobre la base (sumados) y cajas hundidas (cortadas), alternando.
                addBox(5 + (i % 10) * 19.0, 5 + (i / 10) * 40.0, 8, 8, 20, document::Operation::Join);
                addBox(9 + (i % 10) * 19.0, 9 + (i / 10) * 40.0, 3, 3, 6, document::Operation::Cut);
            }
        });
        const auto firstExtrude = doc.feature(1).id();
        auto changed = std::unique_ptr<document::Feature>(doc.findFeature(firstExtrude)->clone());
        static_cast<document::ExtrudeFeature&>(*changed).spec.side1.distance = 12;
        const double editFirst = millis([&] { doc.replaceFeature(firstExtrude, std::move(changed)); });
        const auto lastExtrude = doc.feature(doc.featureCount() - 1).id();
        auto tweaked = std::unique_ptr<document::Feature>(doc.findFeature(lastExtrude)->clone());
        static_cast<document::ExtrudeFeature&>(*tweaked).spec.side1.distance = 7;
        const double editLast = millis([&] { doc.replaceFeature(lastExtrude, std::move(tweaked)); });
        std::printf("%-14zu %10.1f %14.1f %14.1f %12d\n", doc.featureCount(), build, editFirst, editLast,
                    doc.result().bodies.empty() ? 0 : doc.result().bodies.front().shape.faceCount());
    }
}

TEST_CASE("Dónde se va el tiempo de una operación sobre un cuerpo complejo", "[.][perf]")
{
    document::Document doc;
    document::SketchAttachment at;
    at.origin = document::OriginPlane::XY;
    const auto addBox = [&](double x, double y, double w, double h, double depth, document::Operation op) {
        sketch::Sketch s;
        s.addLine({x, y}, {x + w, y});
        s.addLine({x + w, y}, {x + w, y + h});
        s.addLine({x + w, y + h}, {x, y + h});
        s.addLine({x, y + h}, {x, y});
        const auto id = doc.addFeature(std::make_unique<document::SketchFeature>(s, at));
        const auto found = sketch::findProfiles(static_cast<const document::SketchFeature*>(doc.findFeature(id))->sketch());
        auto e = std::make_unique<document::ExtrudeFeature>();
        e->profiles.push_back(document::profileRef(id, found.front()));
        e->spec.side1.distance = depth;
        e->operation = op;
        return doc.addFeature(std::move(e));
    };
    addBox(0, 0, 200, 100, 10, document::Operation::NewBody);
    for (int i = 0; i < 20; ++i) {
        addBox(5 + (i % 10) * 19.0, 5 + (i / 10) * 40.0, 8, 8, 20, document::Operation::Join);
    }
    const geometry::Shape body = doc.result().bodies.front().shape;

    sketch::Sketch s;
    s.addLine({150, 60}, {158, 60});
    s.addLine({158, 60}, {158, 68});
    s.addLine({158, 68}, {150, 68});
    s.addLine({150, 68}, {150, 60});
    geometry::SketchRegion region{&s, sketch::Plane::xy(), sketch::findProfiles(s)};
    geometry::ExtrudeSpec spec;
    spec.side1.distance = 20;
    geometry::Shape tool;
    const double makeTool = millis([&] { tool = geometry::extrude("f9", region, spec); });
    bool touching = false;
    const double touchesMs = millis([&] { touching = geometry::touches(body, tool); });
    geometry::Shape fused;
    const double fuseMs = millis([&] { fused = geometry::boolean(geometry::BooleanOp::Fuse, body, tool); });
    geometry::Shape cut;
    const double cutMs = millis([&] { cut = geometry::boolean(geometry::BooleanOp::Cut, body, tool); });
    double validMs = millis([&] { (void)body.isValid(); });
    std::printf("\ncuerpo de %d caras\n  crear el sólido de la operación %8.1f ms\n  touches (¿toca el cuerpo?)       %8.1f ms (toca: %d)\n"
                "  booleana unir                    %8.1f ms\n  booleana cortar                  %8.1f ms\n  isValid                          %8.1f ms\n",
                body.faceCount(), makeTool, touchesMs, int(touching), fuseMs, cutMs, validMs);
}
