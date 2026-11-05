#pragma once

// Revisión antes de imprimir: avisa de lo que el slicer no va a perdonar
// (sólidos abiertos, paredes más finas que la boquilla, cuerpos que se solapan,
// piezas que no caben en la cama). No corrige nada: son decisiones de diseño.

#include <nuf/geometry/Shape.hpp>

#include <string>
#include <utility>
#include <vector>

namespace nuf::geometry {

// Las medidas de la impresora del usuario (mm).
struct PrintSettings {
    double nozzle = 0.4;   // diámetro de la boquilla
    double bedX = 256.0;   // cama
    double bedY = 256.0;
    double bedZ = 256.0;   // altura máxima
};

struct PrintIssue {
    enum class Level {
        Error,    // el slicer probablemente fallará o la pieza saldrá mal
        Warning,  // se puede imprimir, pero conviene revisarlo
        Info,     // algo que no se pudo comprobar
    };
    Level level = Level::Warning;
    std::string text;
};

// «bodies»: nombre y forma de cada cuerpo que se va a exportar.
std::vector<PrintIssue> checkPrintable(const std::vector<std::pair<std::string, Shape>>& bodies,
                                       const PrintSettings& settings);

} // namespace nuf::geometry
