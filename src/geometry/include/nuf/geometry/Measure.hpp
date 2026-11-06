#pragma once

// Medición de caras y aristas: lo que el usuario quiere saber al señalar una
// o dos cosas del modelo (área, longitud, radio, distancia, ángulo).

#include <nuf/geometry/Shape.hpp>

#include <string>
#include <vector>

namespace nuf::geometry {

// Una cara o una arista de una forma.
struct MeasureElement {
    Shape shape;
    bool isFace = true;
    int index = 0;
};

struct Measurement {
    std::string title;               // «Cara plana», «Arista circular», «Entre cara y arista»…
    std::vector<std::string> lines;  // «Área: 12,50 mm²»…
    // Segmento más corto entre dos elementos (para dibujarlo); vacío con uno solo.
    bool hasSegment = false;
    sketch::Vec3 from;
    sketch::Vec3 to;
    double distance = 0.0;
};

// Mide un elemento, o la relación entre dos. Con más de dos usa los dos
// últimos; con ninguno devuelve una medición vacía.
Measurement measure(const std::vector<MeasureElement>& elements);

} // namespace nuf::geometry
