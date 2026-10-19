#pragma once

#include <QString>

class QApplication;

namespace nuf::app {

// Tema de la interfaz. "Sistema" sigue el modo claro/oscuro del sistema
// operativo cuando Qt puede detectarlo (Qt 6.5+); si no, usa el claro.
enum class Theme { System, Light, Dark };

// Prepara el estilo (Fusion, igual en Windows y Linux) y aplica el tema
// guardado en la configuración.
void initTheme(QApplication& app);

// Cambia el tema, lo aplica a toda la app y lo guarda para la próxima vez.
void setTheme(Theme theme);
Theme currentTheme();

} // namespace nuf::app
