#include "Theme.hpp"

#include <QApplication>
#include <QPalette>
#include <QSettings>
#include <QStyleFactory>
#include <QStyleHints>

namespace nuf::app {

namespace {

const QString kSettingsKey = QStringLiteral("ui/theme");

Theme g_theme = Theme::System;

// Colores tomados de los mockups de la Fase 0.
QPalette lightPalette()
{
    const QColor window(0xF4, 0xF5, 0xF7);
    const QColor base(0xFF, 0xFF, 0xFF);
    const QColor text(0x1F, 0x23, 0x29);
    const QColor muted(0x5B, 0x64, 0x70);
    const QColor border(0xD9, 0xDD, 0xE3);
    const QColor accent(0x1F, 0x6F, 0xEB);

    QPalette p;
    p.setColor(QPalette::Window, window);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, base);
    p.setColor(QPalette::AlternateBase, window);
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::PlaceholderText, muted);
    p.setColor(QPalette::Button, base);
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::BrightText, Qt::white);
    p.setColor(QPalette::ToolTipBase, text);
    p.setColor(QPalette::ToolTipText, base);
    p.setColor(QPalette::Highlight, accent);
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::Link, accent);
    p.setColor(QPalette::Light, base);
    p.setColor(QPalette::Midlight, window);
    p.setColor(QPalette::Mid, border);
    p.setColor(QPalette::Dark, QColor(0xAE, 0xB6, 0xBF));
    p.setColor(QPalette::Shadow, QColor(0x3D, 0x45, 0x50));
    for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText}) {
        p.setColor(QPalette::Disabled, role, QColor(0xA0, 0xA7, 0xB0));
    }
    return p;
}

QPalette darkPalette()
{
    const QColor window(0x1E, 0x21, 0x26);
    const QColor base(0x16, 0x18, 0x1C);
    const QColor button(0x2A, 0x2E, 0x35);
    const QColor text(0xE6, 0xE9, 0xED);
    const QColor muted(0x8B, 0x94, 0x9E);
    const QColor border(0x3A, 0x3F, 0x47);
    const QColor accent(0x3B, 0x82, 0xF6);

    QPalette p;
    p.setColor(QPalette::Window, window);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, base);
    p.setColor(QPalette::AlternateBase, QColor(0x22, 0x26, 0x2C));
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::PlaceholderText, muted);
    p.setColor(QPalette::Button, button);
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::BrightText, Qt::white);
    p.setColor(QPalette::ToolTipBase, button);
    p.setColor(QPalette::ToolTipText, text);
    p.setColor(QPalette::Highlight, accent);
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::Link, QColor(0x6C, 0xA6, 0xFF));
    p.setColor(QPalette::Light, QColor(0x3A, 0x3F, 0x47));
    p.setColor(QPalette::Midlight, button);
    p.setColor(QPalette::Mid, border);
    p.setColor(QPalette::Dark, QColor(0x0F, 0x11, 0x14));
    p.setColor(QPalette::Shadow, Qt::black);
    for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText}) {
        p.setColor(QPalette::Disabled, role, QColor(0x5F, 0x66, 0x70));
    }
    return p;
}

bool systemPrefersDark()
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
#else
    return false;
#endif
}

void apply()
{
    const bool dark = g_theme == Theme::Dark || (g_theme == Theme::System && systemPrefersDark());
    QApplication::setPalette(dark ? darkPalette() : lightPalette());
}

QString toString(Theme theme)
{
    switch (theme) {
    case Theme::Light: return QStringLiteral("light");
    case Theme::Dark: return QStringLiteral("dark");
    case Theme::System: break;
    }
    return QStringLiteral("system");
}

Theme fromString(const QString& value)
{
    if (value == QLatin1String("light")) {
        return Theme::Light;
    }
    if (value == QLatin1String("dark")) {
        return Theme::Dark;
    }
    return Theme::System;
}

} // namespace

void initTheme(QApplication& app)
{
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    g_theme = fromString(QSettings().value(kSettingsKey).toString());

#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    QObject::connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, &app, [] {
        if (g_theme == Theme::System) {
            apply();
        }
    });
#else
    (void)app;
#endif
    apply();
}

void setTheme(Theme theme)
{
    g_theme = theme;
    QSettings().setValue(kSettingsKey, toString(theme));
    apply();
}

Theme currentTheme()
{
    return g_theme;
}

} // namespace nuf::app
