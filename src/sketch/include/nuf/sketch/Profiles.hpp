#pragma once

// Perfiles cerrados del sketch: las regiones que se pueden extruir.
//
// Se buscan las caras del grafo plano que forman las líneas, arcos y círculos
// (sin construcción) unidos por sus extremos. Una región puede tener agujeros
// (otras regiones dentro, como un círculo dentro de un rectángulo); el
// agujero es además una región propia. Las curvas que se cruzan se parten en
// el cruce: una línea que atraviesa un polígono lo divide en dos regiones.

#include <nuf/sketch/Sketch.hpp>

#include <optional>
#include <vector>

namespace nuf::sketch {

// Tramo de una curva partida por sus cruces: de «start» a «end» en el
// sentido propio de la curva (el de su parámetro).
struct Trim {
    Vec2 start;
    Vec2 end;
};

struct ProfileEdge {
    EntityId entity = 0;
    // true si la curva (o su tramo) se recorre de su fin a su inicio.
    bool reversed = false;
    // Solo un tramo de la curva (si se cruza con otras).
    std::optional<Trim> trim;
};

// Contorno cerrado, recorrido en sentido antihorario.
struct Loop {
    std::vector<ProfileEdge> edges;
    double area = 0.0;  // positiva
    // Polígono aproximado (para dibujarlo y para saber qué contiene a qué).
    std::vector<Vec2> polygon;
};

struct Profile {
    Loop outer;
    std::vector<Loop> holes;
    double area = 0.0;  // del contorno exterior menos la de los agujeros
};

// Puntos a menos de «tolerance» se consideran el mismo vértice.
std::vector<Profile> findProfiles(const Sketch& sketch, double tolerance = 1e-6);

} // namespace nuf::sketch
