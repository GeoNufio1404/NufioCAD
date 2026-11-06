#include "Units.hpp"

#include <QLocale>
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

} // namespace nuf::app
