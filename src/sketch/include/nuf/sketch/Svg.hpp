#pragma once

// Lectura de dibujos SVG para importarlos a un sketch (G1), con el mismo resultado que un DXF: una lista de curvas que
// se añade al sketch con importDrawing().
//
// Se entienden path (M L H V C S Q T A Z, absolutos y relativos), rect (con esquinas redondeadas), circle, ellipse,
// line, polyline y polygon, dentro de grupos con transform (translate, scale, rotate, matrix, skewX, skewY). Las
// curvas de Bézier salen como B-splines exactas y los arcos como arcos (o arcos de elipse). El tamaño sale en milímetros:
// con width/height en unidades (mm, cm, in, pt, pc, px) y viewBox se escala a ellas; sin unidades, 1 px = 25,4/96 mm. El
// eje Y se invierte (en SVG crece hacia abajo) y el dibujo queda en el primer cuadrante, con la esquina inferior
// izquierda de la caja del documento en el origen. Se ignoran los rellenos, trazos y estilos; el texto, las imágenes y
// «use» no se importan (salen en «skipped»; convierte el texto a trazados antes de exportar el SVG).

#include <nuf/sketch/Dxf.hpp>

#include <istream>

namespace nuf::sketch {

DxfDrawing readSvg(std::istream& in);

} // namespace nuf::sketch
