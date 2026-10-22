#pragma once

// Qué hay del sketch bajo el cursor, para seleccionar.

#include <nuf/sketch/Sketch.hpp>

namespace nuf::sketch {

// Devuelve el punto (Ref::start/end/center, o Ref::of de un Point) o la
// curva (Ref::of) más cercana a menos de «tolerance» mm; los puntos tienen
// prioridad. Incluye el origen y los ejes, que se tratan como rectas
// infinitas. Ref vacío si no hay nada.
Ref pick(const Sketch& sketch, Vec2 cursor, double tolerance);

// Distancia de p a la curva de una entidad (línea, círculo o arco; los ejes
// como rectas infinitas). Infinito si la entidad no es una curva.
double distanceToCurve(const Entity& entity, Vec2 p);

} // namespace nuf::sketch
