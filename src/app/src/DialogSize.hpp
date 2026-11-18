#pragma once

#include <QDialog>
#include <QGuiApplication>
#include <QRect>
#include <QScreen>
#include <QSettings>
#include <QSize>
#include <QString>

#include <algorithm>

namespace nuf::app {

// Fase 20, R2: un diálogo nunca es más grande que la pantalla y recuerda el tamaño con que el usuario lo dejó.
// «key» identifica al diálogo en los ajustes («dialogs/<key>»). Se llama al final del constructor.
inline void fitDialogToScreen(QDialog* dialog, const QString& key)
{
    const QScreen* screen = dialog->parentWidget() != nullptr && dialog->parentWidget()->screen() != nullptr
                                ? dialog->parentWidget()->screen()
                                : QGuiApplication::primaryScreen();
    const QRect available = screen != nullptr ? screen->availableGeometry() : QRect(0, 0, 1280, 720);
    const QSize saved = QSettings().value(QStringLiteral("dialogs/") + key).toSize();
    QSize wanted = saved.isValid() && !saved.isEmpty() ? saved : dialog->size();
    // Nunca más que el 90 % de la pantalla (un poco menos que el espacio libre: queda el marco de la ventana).
    wanted.setWidth(std::min(wanted.width(), int(available.width() * 0.9)));
    wanted.setHeight(std::min(wanted.height(), int(available.height() * 0.9)));
    wanted = wanted.expandedTo(dialog->minimumSizeHint().boundedTo(available.size() * 0.9));
    dialog->resize(wanted);
    // Al cerrarlo se guarda el tamaño (solo si cabía en pantalla, para no guardar uno absurdo).
    QObject::connect(dialog, &QDialog::finished, dialog, [dialog, key] {
        QSettings().setValue(QStringLiteral("dialogs/") + key, dialog->size());
    });
}

} // namespace nuf::app
