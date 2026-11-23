#include "Units.hpp"

#include <QLocale>
#include <QRegularExpression>
#include <QSettings>

namespace nuf::app {

Units::Units()
{
    const QString saved = QSettings().value(QStringLiteral("units/length"), QStringLiteral("mm")).toString();
    for (const LengthUnit unit : {LengthUnit::Mm, LengthUnit::Cm, LengthUnit::M, LengthUnit::In}) {
        if (label(unit) == saved) {
            length_ = unit;
        }
    }
}

Units& Units::instance()
{
    static Units units;
    return units;
}

void Units::setLength(LengthUnit unit)
{
    if (unit == length_) {
        return;
    }
    length_ = unit;
    QSettings().setValue(QStringLiteral("units/length"), label(unit));
    emit changed();
}

double Units::mmPerUnit(LengthUnit unit)
{
    switch (unit) {
    case LengthUnit::Mm: return 1.0;
    case LengthUnit::Cm: return 10.0;
    case LengthUnit::M: return 1000.0;
    case LengthUnit::In: return 25.4;
    }
    return 1.0;
}

QString Units::label(LengthUnit unit)
{
    switch (unit) {
    case LengthUnit::Mm: return QStringLiteral("mm");
    case LengthUnit::Cm: return QStringLiteral("cm");
    case LengthUnit::M: return QStringLiteral("m");
    case LengthUnit::In: return QStringLiteral("in");
    }
    return QStringLiteral("mm");
}

QString Units::name(LengthUnit unit)
{
    switch (unit) {
    case LengthUnit::Mm: return QStringLiteral("Milímetros (mm)");
    case LengthUnit::Cm: return QStringLiteral("Centímetros (cm)");
    case LengthUnit::M: return QStringLiteral("Metros (m)");
    case LengthUnit::In: return QStringLiteral("Pulgadas (in)");
    }
    return QString();
}

int Units::extraDecimals() const
{
    switch (length_) {
    case LengthUnit::Mm: return 0;
    case LengthUnit::Cm: return 1;
    case LengthUnit::In: return 2;
    case LengthUnit::M: return 3;
    }
    return 0;
}

QString Units::format(double mm, int decimals) const
{
    QLocale locale;
    locale.setNumberOptions(QLocale::OmitGroupSeparator);
    return locale.toString(toDisplay(mm), 'f', decimals + extraDecimals());
}

QString Units::formatWithUnit(double mm, int decimals) const
{
    return format(mm, decimals) + QLatin1Char(' ') + unit();
}

QString Units::formatVolume(double mm3, int decimals) const
{
    QLocale locale;
    locale.setNumberOptions(QLocale::OmitGroupSeparator);
    const double f = factor();
    return locale.toString(mm3 / (f * f * f), 'f', decimals + 3 * extraDecimals());
}

QString Units::localize(const QString& text) const
{
    if (length_ == LengthUnit::Mm || !text.contains(QLatin1String("mm"))) {
        return text;
    }
    // Un número (con coma o punto) y «mm» que no sea «mm²» ni «mm³» ni parte de una palabra.
    static const QRegularExpression number(QStringLiteral(R"((\d+(?:[.,]\d+)?)(\s*)mm(?![\w²³]))"));
    static const QRegularExpression bare(QStringLiteral(R"((?<![\w²³])mm(?![\w²³]))"));
    QString out;
    qsizetype last = 0;
    for (auto it = number.globalMatch(text); it.hasNext();) {
        const QRegularExpressionMatch m = it.next();
        out += text.mid(last, m.capturedStart() - last);
        QString digits = m.captured(1);
        digits.replace(QLatin1Char(','), QLatin1Char('.'));
        QString shown = QString::number(toDisplay(digits.toDouble()), 'f', 3);
        while (shown.contains(QLatin1Char('.')) && (shown.endsWith(QLatin1Char('0')) || shown.endsWith(QLatin1Char('.')))) {
            shown.chop(1);
        }
        shown.replace(QLatin1Char('.'), m.captured(1).contains(QLatin1Char('.')) ? QLatin1Char('.') : QLatin1Char(','));
        out += shown + m.captured(2) + unit();
        last = m.capturedEnd();
    }
    out += text.mid(last);
    out.replace(bare, unit());
    return out;
}

} // namespace nuf::app
