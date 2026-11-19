#pragma once

// Marca en la interfaz los cuadros que tienen que ver con un error (rojo) o con un aviso (naranja): además del mensaje
// de abajo, el borde del cuadro cambia de color para que no haya que buscarlo entre los demás.
// Un mensaje no dice qué cuadro falló, así que se busca por palabras: cada campo tiene las suyas («hélice», «agujero»…)
// y se marcan los que el mensaje nombra. Varios cuadros pueden marcarse a la vez (un ángulo que no es congruente con
// una longitud).

#include <QAbstractSpinBox>
#include <QComboBox>
#include <QString>
#include <QWidget>

#include <string>
#include <utility>
#include <vector>

#include "Theme.hpp"

namespace nuf::app {

enum class Mark { None = 0, Warning = 1, Error = 2 };

inline QString markColor(Mark mark)
{
    if (mark == Mark::Error) {
        return toneColor(Tone::Error);
    }
    // El naranja de los avisos, más vivo que el del texto para que se vea como borde.
    return isDarkTheme() ? QStringLiteral("#F2A34B") : QStringLiteral("#E07B00");
}

inline Mark markOf(const QWidget* widget)
{
    return widget != nullptr ? Mark(widget->property("markLevel").toInt()) : Mark::None;
}

inline void markField(QWidget* widget, Mark mark)
{
    if (widget == nullptr || markOf(widget) == mark) {
        return;
    }
    if (mark == Mark::None) {
        widget->setStyleSheet(QString());
    } else {
        const QString type = qobject_cast<QComboBox*>(widget) != nullptr ? QStringLiteral("QComboBox") : QStringLiteral("QAbstractSpinBox");
        widget->setStyleSheet(QStringLiteral("%1 { border: 2px solid %2; border-radius: 3px; }").arg(type, markColor(mark)));
    }
    widget->setProperty("markLevel", int(mark));
}

// Las palabras que, si un mensaje las dice, señalan al campo de esa clave (en inglés las de las herramientas y en
// español las de los generadores).
inline std::vector<QString> problemKeywords(const std::string& keyText)
{
    const QString key = QString::fromStdString(keyText).toLower();
    std::vector<QString> words;
    const auto has = [&](const char* part) { return key.contains(QLatin1String(part)); };
    const auto add = [&](const char* word) { words.push_back(QString::fromUtf8(word)); };
    if (has("module") || has("modulo")) {
        add("módulo");
    }
    if (has("mateteeth")) {
        add("pareja");
    } else if (has("teeth") || has("dientes")) {
        add("dientes");
        add("diente");
    }
    if (has("entradas")) {
        add("entradas");
    }
    if (has("teeth")) {
        add("entradas");
    }
    if (has("pressure") || has("presion")) {
        add("presión");
        add("puntiagudo");
    }
    if (has("helix") || has("helice")) {
        add("hélice");
    }
    if (has("thickness")) {
        add("ancho");
        add("largo");
        add("grosor");
    }
    if (has("ancho_cara")) {
        add("ancho de cara");
    } else if (has("ancho")) {
        add("ancho");
    }
    if (has("largo")) {
        add("largo");
    }
    if (has("bore") || has("agujero")) {
        add("agujero");
    }
    if (has("rim") || key == QLatin1String("aro")) {
        add("aro");
    }
    if (key == QLatin1String("base")) {
        add("base");
    }
    if (has("backlash") || has("juego") || has("adelgazar")) {
        add("juego");
    }
    if (has("chamfer") || has("chaflan")) {
        add("chaflán");
    }
    if (has("shift") || has("correccion")) {
        add("corrección");
    }
    if (has("groove")) {
        add("surco");
    }
    if (has("wormdiameter") || has("diam_tornillo")) {
        add("tornillo");
    }
    if (has("espina")) {
        add("espina");
    }
    if (has("width")) {
        add("ancho");
    }
    if (has("depth")) {
        add("profundidad");
    }
    if (has("height")) {
        add("alto");
    }
    if (has("radius")) {
        add("radio");
    }
    if (has("diameter")) {
        add("diámetro");
    }
    if (has("distance")) {
        add("distancia");
    }
    return words;
}

// Marca los cuadros que el mensaje nombra con «mark» y quita la marca a los demás (los que ya estaban marcados por
// otra razón, como un aviso fijo, se vuelven a marcar después con markField).
inline void markByMessage(const std::vector<std::pair<std::string, QWidget*>>& widgets, const QString& message, Mark mark)
{
    const QString lower = message.toLower();
    for (const auto& [key, widget] : widgets) {
        Mark wanted = Mark::None;
        if (mark != Mark::None && !lower.isEmpty()) {
            for (const QString& word : problemKeywords(key)) {
                if (lower.contains(word)) {
                    wanted = mark;
                    break;
                }
            }
        }
        markField(widget, wanted);
    }
}

} // namespace nuf::app
