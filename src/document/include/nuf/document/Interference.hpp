#pragma once

// Interferencias (Fase 21, A8): los cuerpos que se pisan, con el volumen que tienen en común, en la posición de ahora
// y a lo largo del giro de un enlace de movimiento.

#include <nuf/document/Document.hpp>

#include <functional>
#include <set>
#include <string>
#include <vector>

namespace nuf::document {

struct InterferencePair {
    std::string first;     // clave del cuerpo
    std::string second;
    std::string firstName;
    std::string secondName;
    double volume = 0.0;   // mm³ (en el peor momento, si se mira a lo largo de un giro)
    double at = 0.0;       // posición del enlace (rad) en la que es peor; 0 si se mira la de ahora
    geometry::Shape shape; // el volumen común en esa posición (para dibujarlo en rojo)
};

// Las parejas de cuerpos del estado que se pisan más de «minVolume» mm³; las más grandes primero. Los cuerpos fijos
// y los demás se comprueban igual. Con «hidden», esas claves se ignoran (los cuerpos ocultos no estorban).
std::vector<InterferencePair> findInterferences(const ModelState& state, double minVolume = 1e-3,
                                                const std::set<std::string>& hidden = {});

// Lo mismo en «steps» posiciones del enlace «link» entre 0 y «span» (rad): para cada pareja, el peor momento. Una
// lista vacía quiere decir que no se pisan en ningún punto del giro (probado en esas posiciones). Devuelve también
// en «checked» cuántas posiciones se miraron. «cancelled» se consulta entre posición y posición.
std::vector<InterferencePair> findInterferencesAlong(const Document& document, FeatureId link, double span, int steps,
                                                     int* checked = nullptr, double minVolume = 1e-3,
                                                     const std::function<bool()>& cancelled = {});

} // namespace nuf::document
