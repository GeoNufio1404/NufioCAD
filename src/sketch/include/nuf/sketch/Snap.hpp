#pragma once

// Ajuste del cursor (snap) a puntos notables del sketch y a la rejilla.

#include <nuf/sketch/Sketch.hpp>

namespace nuf::sketch {

enum class SnapKind : std::uint8_t {
    None,      // posición libre del cursor
    Grid,      // nodo de la rejilla
    Point,     // punto existente: extremo, punto suelto u origen
    Center,    // centro de un círculo o arco
    Midpoint,  // punto medio de una línea
};

struct Snap {
    Vec2 point;
    SnapKind kind = SnapKind::None;
    // Point/Center: el punto (Ref::start, Ref::center…); Midpoint: Ref::of(línea).
    Ref ref;
};

struct SnapOptions {
    // Distancia máxima (en mm del sketch) para atraer el cursor a un punto.
    double tolerance = 1.0;
    // Paso de la rejilla; 0 = sin ajuste a la rejilla.
    double gridStep = 0.0;
};

// Prioridad: puntos y centros, después puntos medios y por último la rejilla.
Snap snap(const Sketch& sketch, Vec2 cursor, const SnapOptions& options);

} // namespace nuf::sketch
