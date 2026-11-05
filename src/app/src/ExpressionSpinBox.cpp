#include "ExpressionSpinBox.hpp"

#include <nuf/document/Document.hpp>
#include <nuf/document/Expression.hpp>

#include <QLineEdit>
#include <QLocale>

#include <cmath>

namespace nuf::app {

namespace {
const document::Document* gDocument = nullptr;
}

ExpressionSpinBox::ExpressionSpinBox(QWidget* parent)
    : QDoubleSpinBox(parent)
{
    // Sin separador de miles: «1,000.50» no se puede volver a leer.
    QLocale locale;
    locale.setNumberOptions(QLocale::OmitGroupSeparator);
    setLocale(locale);
}

void ExpressionSpinBox::setDocument(const document::Document* document)
{
    gDocument = document;
}

QValidator::State ExpressionSpinBox::validate(QString& text, int&) const
{
    return text.trimmed().isEmpty() ? QValidator::Intermediate : QValidator::Acceptable;
}

double ExpressionSpinBox::valueFromText(const QString& text) const
{
    QString t = text.trimmed();
    const QString unit = suffix().trimmed();
    if (!unit.isEmpty() && t.endsWith(unit)) {
        t.chop(unit.size());
        t = t.trimmed();
    }
    if (t.isEmpty()) {
        return value();
    }
    auto* self = const_cast<ExpressionSpinBox*>(this);
    try {
        const std::string source = t.toStdString();
        const document::ParameterLookup lookup =
            gDocument != nullptr ? gDocument->lookup() : document::ParameterLookup([](std::string_view) { return std::nullopt; });
        const double result = document::evaluateExpression(source, lookup);
        const bool plain = document::expressionNames(source).empty();
        expression_ = plain ? QString() : t;
        expressionValue_ = result;
        self->setToolTip(plain ? QString() : QStringLiteral("= %1").arg(result, 0, 'g', 8));
        return result;
    } catch (const std::exception& e) {
        self->setToolTip(QString::fromUtf8(e.what()));
        return value();
    }
}

QString ExpressionSpinBox::textFromValue(double v) const
{
    // Si el valor ya no es el de la expresión (se cambió a mano o con la
    // flecha de la vista), la expresión deja de valer.
    if (!expression_.isEmpty() && std::abs(v - expressionValue_) > 0.5 * std::pow(10.0, -decimals()) + 1e-9) {
        expression_.clear();
    }
    return expression_.isEmpty() ? QDoubleSpinBox::textFromValue(v) : expression_;
}

void ExpressionSpinBox::setExpression(const QString& text)
{
    const double result = valueFromText(text);
    setValue(result);
    if (!expression_.isEmpty() && lineEdit() != nullptr) {
        lineEdit()->setText(expression_ + suffix());  // aunque el valor no haya cambiado
    }
}

} // namespace nuf::app
