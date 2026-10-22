#pragma once

// Traduce lo que el usuario seleccionó a restricciones o cotas, como el
// flujo «seleccionar y aplicar» de Fusion 360.

#include <nuf/sketch/Sketch.hpp>

#include <optional>
#include <vector>

namespace nuf::sketch {

// Restricciones de tipo «type» para la selección (en orden de selección):
//   Coincident     2 puntos | punto + curva (punto sobre la curva)
//   Horizontal…    1 o más líneas (una por línea) | 2 puntos
//   Parallel, Perpendicular, Collinear, Tangent, Concentric   2 curvas
//   Parallel, Collinear, Equal   también más de 2 (la primera con cada otra)
//   Midpoint       punto + línea
//   Symmetric      2 puntos + línea (o + un tercer punto como centro)
//   Fix            cada punto o curva seleccionada
// Vacío si la selección no sirve. Las restricciones aún no tienen id.
std::vector<Constraint> constraintsFromSelection(ConstraintType type, const Sketch& sketch,
                                                 const std::vector<Ref>& selection);

// Cota para la selección, con el valor actual de la geometría:
//   línea → longitud; círculo → diámetro; arco → radio;
//   2 puntos → distancia; punto + línea → distancia a la recta;
//   2 líneas → ángulo (o distancia si son paralelas).
std::optional<Constraint> dimensionFromSelection(const Sketch& sketch, const std::vector<Ref>& selection);

} // namespace nuf::sketch
