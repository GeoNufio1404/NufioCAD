#pragma once

namespace nuf::app {

// Si el programa se cierra por un fallo (acceso a memoria inválido, etc.),
// guarda un informe de texto con la pila de llamadas y un volcado (.dmp) en
// «<datos de la aplicación>/cierres». Solo en Windows por ahora; en otros
// sistemas no hace nada. Llamar una vez al arrancar.
void installCrashHandler();

} // namespace nuf::app
