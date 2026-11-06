#pragma once

// Cuadro numérico que además acepta expresiones con parámetros del documento
// («grosor * 2», «largo / 3 + 1.5»). Mientras el texto use algún parámetro, el
// cuadro recuerda la expresión (expression()) y la operación la guarda: así el
// valor se recalcula solo cuando cambia la tabla de parámetros.

#include <QAbstractSpinBox>
#include <QDoubleSpinBox>
#include <QString>

namespace nuf::document {
class Document;
}

namespace nuf::app {

// Muestra la unidad («mm», «°») como una etiqueta fija a la derecha del número, dentro del cuadro: no se
// edita ni forma parte del texto (al cambiar el valor solo se reescribe el número).
void setUnit(QAbstractSpinBox* box, const QString& unit);
// La unidad puesta con setUnit (vacía si no hay).
QString unitOf(const QAbstractSpinBox* box);

class ExpressionSpinBox final : public QDoubleSpinBox {
    Q_OBJECT

public:
    explicit ExpressionSpinBox(QWidget* parent = nullptr);

    // El documento del que salen los parámetros (el de la ventana principal).
    static void setDocument(const document::Document* document);

    // La expresión que dio el valor actual; vacía si es un número a secas.
    QString expression() const { return expression_; }
    // Pone la expresión (la evalúa y la muestra).
    void setExpression(const QString& text);

    QValidator::State validate(QString& text, int& pos) const override;
    double valueFromText(const QString& text) const override;
    QString textFromValue(double value) const override;

private:
    mutable QString expression_;
    mutable double expressionValue_ = 0.0;
};

} // namespace nuf::app
