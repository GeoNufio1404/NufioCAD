#pragma once

// Adaptador del kernel geométrico.
// Regla de arquitectura: ninguna cabecera pública de este módulo incluye
// tipos de OpenCASCADE; OCCT solo aparece en los .cpp.

#include <string>

namespace nuf::geometry {

// Versión de OpenCASCADE con la que se compiló, ej. "7.9.3".
std::string kernelVersion();

// Prueba de humo del entorno: construye una caja con OCCT y devuelve su
// volumen. Lanza std::invalid_argument si alguna dimensión no es positiva.
double probeBoxVolume(double dx, double dy, double dz);

} // namespace nuf::geometry
