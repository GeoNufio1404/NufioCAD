#pragma once

// Unidad de longitud con la que se muestran y se escriben las medidas. El modelo, el archivo y los cálculos
// siguen siempre en milímetros: esto solo cambia lo que ve y escribe el usuario.

#include <QObject>
#include <QString>

namespace nuf::app {

enum class LengthUnit { Mm, Cm, M, In };

class Units final : public QObject {
    Q_OBJECT

public:
    static Units& instance();

    LengthUnit length() const { return length_; }
    void setLength(LengthUnit unit);  // la guarda y avisa con changed()

    static double mmPerUnit(LengthUnit unit);
    static QString label(LengthUnit unit);  // «mm», «cm», «m», «in»
    static QString name(LengthUnit unit);   // «Milímetros (mm)»…

    double factor() const { return mmPerUnit(length_); }  // milímetros por unidad
    QString unit() const { return label(length_); }
    // Decimales de más al mostrar (un centímetro o una pulgada piden más para llegar a la misma precisión).
    int extraDecimals() const;
    double toDisplay(double mm) const { return mm / factor(); }
    double fromDisplay(double value) const { return value * factor(); }
    // El número en la unidad actual, sin la unidad («12.70»), o con ella («12.70 mm»).
    QString format(double mm, int decimals = 2) const;
    QString formatWithUnit(double mm, int decimals = 2) const;

signals:
    void changed();

private:
    Units();
    LengthUnit length_ = LengthUnit::Mm;
};

} // namespace nuf::app
