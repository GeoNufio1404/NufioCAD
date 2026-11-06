#pragma once

// Dónde y cómo se dibujan las restricciones (iconos junto a la geometría) y
// las cotas (líneas de cota y valor). Las distancias en pantalla se pasan a
// mm del sketch con pixelSize, para que no cambien de tamaño con el zoom.

#include <nuf/sketch/Sketch.hpp>

#include <string>
#include <vector>

namespace nuf::sketch {

struct Annotation {
    ConstraintId constraint = 0;
    Vec2 anchor;                  // centro del texto
    std::string text;             // UTF-8: "H", "//", "40", "R5", "30°"…
    std::vector<Geometry> lines;  // líneas de cota, de referencia y flechas
    bool dimension = false;
    bool reference = false;       // cota de referencia (se muestra entre paréntesis)
};

// Una anotación por cota y una por cada entidad afectada por una restricción
// geométrica (las coincidentes no se muestran: se ven como puntos unidos).
std::vector<Annotation> layoutAnnotations(const Sketch& sketch, double pixelSize);

// Restricción cuya anotación está bajo el cursor (0 si ninguna).
ConstraintId pickAnnotation(const std::vector<Annotation>& annotations, Vec2 cursor, double pixelSize);

// Texto de una cota: "40", "12.5", "30°", "R5", "Ø10"; entre paréntesis si es
// de referencia.
std::string formatDimension(const Constraint& constraint);

// Las longitudes de las cotas se muestran divididas por «mmPerUnit» (la unidad elegida), con «decimals»
// como máximo. El sketch sigue en milímetros.
void setDimensionLengthScale(double mmPerUnit, int decimals);

} // namespace nuf::sketch
