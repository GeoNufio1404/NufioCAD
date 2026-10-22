#pragma once

// Lectura de dibujos DXF (ASCII) para importarlos a un sketch.
//
// Entidades: LINE, CIRCLE, ARC, POINT, LWPOLYLINE y POLYLINE (con arcos por
// «bulge»), ELLIPSE y SPLINE (que se reinterpola con nudos uniformes). Se
// ignora la coordenada Z y se suponen milímetros.

#include <nuf/sketch/Sketch.hpp>

#include <istream>
#include <string>
#include <vector>

namespace nuf::sketch {

struct DxfDrawing {
    std::vector<Geometry> geometry;
    // Tipos de entidad que no se pudieron importar (sin repetir).
    std::vector<std::string> skipped;
};

DxfDrawing readDxf(std::istream& in);

// Añade el dibujo al sketch, desplazado por «offset», y une con
// coincidencias los extremos que caen en el mismo sitio. Devuelve las
// entidades creadas.
std::vector<EntityId> importDrawing(Sketch& sketch, const DxfDrawing& drawing, Vec2 offset = {});

} // namespace nuf::sketch
