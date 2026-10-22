#pragma once

// Qué hay del sketch bajo el cursor, para seleccionar.

#include <nuf/sketch/Sketch.hpp>

namespace nuf::sketch {

// Devuelve el punto (Ref::start/end/center, o Ref::of de un Point) o la
// curva (Ref::of) más cercana a menos de «tolerance» mm; los puntos tienen
// prioridad. Incluye el origen y los ejes, que se tratan como rectas
// infinitas. Ref vacío si no hay nada.
Ref pick(const Sketch& sketch, Vec2 cursor, double tolerance);

} // namespace nuf::sketch
