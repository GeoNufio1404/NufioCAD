#include "Theme.hpp"

#include <QApplication>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QProxyStyle>
#include <QSettings>
#include <QStyleFactory>
#include <QStyleHints>
#include <QStyleOption>

namespace nuf::app {

namespace {

const QString kSettingsKey = QStringLiteral("ui/theme");

Theme g_theme = Theme::System;

// Superficies en capas (de más hundida a más elevada), para que cada panel se distinga del vecino sin
// depender solo de una línea fina: el marco de la ventana (Window, gris azulado), las barras elevadas como la
// cinta (Button, casi blanco), y el contenido y los campos de entrada (Base, blanco). Los bordes de los
// controles (Dark) llegan a 3:1 de contraste contra el blanco, como pide WCAG para componentes de interfaz.
QPalette lightPalette()
{
    const QColor window(0xE4, 0xE8, 0xEE);
    const QColor base(0xFF, 0xFF, 0xFF);
    const QColor raised(0xF6, 0xF7, 0xF9);
    const QColor text(0x1B, 0x20, 0x27);
    const QColor muted(0x55, 0x5F, 0x6B);
    const QColor border(0xB6, 0xBE, 0xC9);
    const QColor accent(0x1F, 0x6F, 0xEB);

    QPalette p;
    p.setColor(QPalette::Window, window);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, base);
    p.setColor(QPalette::AlternateBase, QColor(0xF1, 0xF3, 0xF6));
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::PlaceholderText, muted);
    p.setColor(QPalette::Button, raised);
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::BrightText, Qt::white);
    p.setColor(QPalette::ToolTipBase, text);
    p.setColor(QPalette::ToolTipText, base);
    p.setColor(QPalette::Highlight, accent);
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::Link, accent);
    p.setColor(QPalette::Light, base);
    p.setColor(QPalette::Midlight, QColor(0xEE, 0xF0, 0xF4));
    p.setColor(QPalette::Mid, border);
    p.setColor(QPalette::Dark, QColor(0x8A, 0x94, 0xA1));
    p.setColor(QPalette::Shadow, QColor(0x3D, 0x45, 0x50));
    for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText}) {
        p.setColor(QPalette::Disabled, role, QColor(0x9A, 0xA2, 0xAD));
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
    const QColor border(0x43, 0x49, 0x52);
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
    p.setColor(QPalette::Dark, QColor(0x6E, 0x77, 0x82));
    p.setColor(QPalette::Shadow, Qt::black);
    for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText}) {
        p.setColor(QPalette::Disabled, role, QColor(0x5F, 0x66, 0x70));
    }
    return p;
}

// Fusion dibuja el borde de las casillas y los botones de opción oscureciendo el color de la ventana: en el tema
// oscuro queda casi negro sobre casi negro y la casilla desaparece. Aquí se dibujan con el color de borde de la
// paleta (Dark) y el de selección al marcarlas. Solo cambia estos dos indicadores; lo demás es Fusion.
class NufioStyle final : public QProxyStyle {
public:
    NufioStyle()
        : QProxyStyle(QStyleFactory::create(QStringLiteral("Fusion")))
    {
    }

    void drawPrimitive(PrimitiveElement element, const QStyleOption* option, QPainter* painter,
                       const QWidget* widget) const override
    {
        if (element != PE_IndicatorCheckBox && element != PE_IndicatorRadioButton) {
            QProxyStyle::drawPrimitive(element, option, painter, widget);
            return;
        }
        const bool enabled = option->state & State_Enabled;
        const bool on = option->state & State_On;
        const bool partial = option->state & State_NoChange;
        const bool hover = enabled && (option->state & State_MouseOver);
        const QPalette& pal = option->palette;
        QColor fill = (on || partial) ? pal.color(QPalette::Highlight) : pal.color(QPalette::Base);
        QColor border = (on || partial) ? pal.color(QPalette::Highlight).darker(125)
                                        : (hover ? pal.color(QPalette::Highlight) : pal.color(QPalette::Dark));
        if (!enabled) {
            fill.setAlphaF(0.45f);
            border.setAlphaF(0.45f);
        }
        const QRectF box = QRectF(option->rect).adjusted(0.5, 0.5, -0.5, -0.5);
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(QPen(border, 1.0));
        painter->setBrush(fill);
        const bool radio = element == PE_IndicatorRadioButton;
        if (radio) {
            painter->drawEllipse(box);
        } else {
            painter->drawRoundedRect(box, 3.0, 3.0);
        }
        painter->setBrush(Qt::NoBrush);
        const QColor mark = pal.color(QPalette::HighlightedText);
        if (radio && on) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(mark);
            painter->drawEllipse(box.center(), box.width() * 0.2, box.height() * 0.2);
        } else if (!radio && (on || partial)) {
            painter->setPen(QPen(mark, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            QPainterPath path;
            if (partial) {
                path.moveTo(box.left() + box.width() * 0.25, box.center().y());
                path.lineTo(box.right() - box.width() * 0.25, box.center().y());
            } else {
                path.moveTo(box.left() + box.width() * 0.24, box.top() + box.height() * 0.54);
                path.lineTo(box.left() + box.width() * 0.43, box.top() + box.height() * 0.73);
                path.lineTo(box.left() + box.width() * 0.77, box.top() + box.height() * 0.30);
            }
            painter->drawPath(path);
        }
        painter->restore();
    }
};

bool systemPrefersDark()
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
#else
    return false;
#endif
}

}  // namespace

bool isDarkTheme()
{
    return g_theme == Theme::Dark || (g_theme == Theme::System && systemPrefersDark());
}

QString toneColor(Tone tone)
{
    // Sobre el marco claro (#E4E8EE) y sobre el blanco: 4,7:1 o más; sobre el oscuro (#1E2126): 4,5:1 o más.
    const bool dark = isDarkTheme();
    switch (tone) {
    case Tone::Error: return dark ? QStringLiteral("#F2777A") : QStringLiteral("#B42318");
    case Tone::Warning: return dark ? QStringLiteral("#F2A34B") : QStringLiteral("#8A5300");
    case Tone::Success: return dark ? QStringLiteral("#3FB950") : QStringLiteral("#1E7B34");
    case Tone::Muted: break;
    }
    return dark ? QStringLiteral("#9AA3AE") : QStringLiteral("#55606C");
}

namespace {

void apply()
{
    const bool dark = isDarkTheme();
    QApplication::setPalette(dark ? darkPalette() : lightPalette());
    // Los paneles (Navegador, Propiedades, Timeline) llevan una barra de título propia, un poco más elevada que
    // el marco y en negrita, para saber de un vistazo qué panel es cuál. Solo afecta a esa barra.
    qApp->setStyleSheet(QStringLiteral(
        "QDockWidget::title { background: palette(midlight); padding: 5px 8px; font-weight: 600;"
        " border-bottom: 1px solid palette(mid); }"));
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
    QApplication::setStyle(new NufioStyle);
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
