#pragma once

#include <QIcon>
#include <QString>

namespace nuf::app {

// Icono de la interfaz (SVG de `resources/icons/ui/<name>.svg`). Se dibuja al
// vuelo con los colores de la paleta actual, así sigue al tema claro/oscuro
// sin recargar nada: el trazo toma el color del texto y el acento, el de
// resalte; desactivado, todo se ve atenuado.
QIcon uiIcon(const QString& name);

} // namespace nuf::app
