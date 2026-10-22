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
};

struct SnapOptions {
    // Distancia máxima (en mm del sketch) para atraer el cursor a un punto.
    double tolerance = 1.0;
    // Paso de la rejilla; 0 = sin ajuste a la rejilla.
    double gridStep = 0.0;
    // false: devuelve el cursor tal cual (kind None, free = true).
    bool enabled = true;
};

// Prioridad: puntos y centros, intersecciones, puntos medios, sobre una
// curva y por último la rejilla.
Snap snap(const Sketch& sketch, Vec2 cursor, const SnapOptions& options);

} // namespace nuf::sketch
