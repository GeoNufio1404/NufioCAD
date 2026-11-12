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

// Colores de estado (error, aviso, correcto, tenue) para textos sobre el fondo de los paneles. Cambian con el
// tema porque el mismo rojo o ámbar que se lee bien sobre el oscuro no llega al contraste mínimo (4,5:1) sobre
// el claro. Devuelven «#RRGGBB» para usar en HTML o en QColor.
enum class Tone { Error, Warning, Success, Muted };
QString toneColor(Tone tone);

// Si el tema que se está mostrando es el oscuro (el elegido, o el del sistema si se sigue a este).
bool isDarkTheme();

// Cambia el tema, lo aplica a toda la app y lo guarda para la próxima vez.
void setTheme(Theme theme);
Theme currentTheme();

} // namespace nuf::app
