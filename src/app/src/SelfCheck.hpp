#pragma once

#include <ostream>

namespace nuf::app {

// Verifica el stack completo sin abrir ventanas (útil en CI):
// OCCT construye una caja, SQLite crea y reabre un .nuf, y se imprimen
// las versiones. Devuelve 0 si todo está bien.
int runSelfCheck(std::ostream& out);

} // namespace nuf::app
