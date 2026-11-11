#pragma once

// Ajuste del cursor (snap) a puntos notables del sketch y a la rejilla.

#include <nuf/sketch/Sketch.hpp>

namespace nuf::sketch {

enum class SnapKind : std::uint8_t {
    None,          // posición libre del cursor
    Grid,          // nodo de la rejilla
    Point,         // punto existente: extremo, punto suelto u origen
    Center,        // centro de un círculo o arco
    Midpoint,      // punto medio de una línea
    Intersection,  // cruce de dos curvas (también con los ejes)
    OnCurve,       // punto sobre una curva (el más cercano al cursor)
};

// Recta guía de una inferencia (la UI la dibuja discontinua y naranja).
struct Guide {
    Vec2 from;
    Vec2 to;
};

struct Snap {
    Snap() = default;
    Snap(Vec2 p, SnapKind k = SnapKind::None, Ref r = {})
        : point(p)
        , kind(k)
        , ref(r)
    {}

    Vec2 point;
    SnapKind kind = SnapKind::None;
    // Point/Center: el punto (Ref::start, Ref::center…); Midpoint, OnCurve e
    // Intersection: Ref::of(curva).
    Ref ref;
    // Intersection: la otra curva.
    Ref ref2;
    // Sin inferencias (Ctrl): las herramientas no enderezan ni restringen.
    bool free = false;
    // Inferencias de alineación: si el cursor se pegó a la horizontal o la
    // vertical de un punto de referencia, ese punto (alignY: misma Y →
    // horizontal; alignX: misma X → vertical) y las rectas guía a dibujar.
    // Al hacer clic se crea la restricción correspondiente.
    Ref alignX;
    Ref alignY;
    std::vector<Guide> guides;
};

// Punto de referencia para las alineaciones (el origen, un punto «despierto»).
struct AlignPoint {
    Vec2 position;
    Ref ref;
};

struct SnapOptions {
    // Distancia máxima (en mm del sketch) para atraer el cursor a un punto.
    double tolerance = 1.0;
    // Paso de la rejilla; 0 = sin ajuste a la rejilla.
    double gridStep = 0.0;
    // false: devuelve el cursor tal cual (kind None, free = true).
    bool enabled = true;
    // Puntos con los que se infiere la alineación horizontal/vertical, y a
    // qué distancia (en mm) el cursor se pega a su recta. Sin puntos o con
    // alignTolerance 0 no hay alineaciones.
    std::vector<AlignPoint> alignPoints;
    double alignTolerance = 0.0;
    // Histéresis: la alineación que ya tenía el cursor (alignX/alignY de la
    // posición anterior) se mantiene hasta alignRelease (≥ alignTolerance), para
    // que la guía no tiemble al borde del radio de atracción.
    Ref stickyX;
    Ref stickyY;
    double alignRelease = 0.0;
};

// Prioridad: puntos y centros, intersecciones, puntos medios, sobre una
// curva, alineaciones con los puntos de referencia y por último la rejilla.
Snap snap(const Sketch& sketch, Vec2 cursor, const SnapOptions& options);

} // namespace nuf::sketch
