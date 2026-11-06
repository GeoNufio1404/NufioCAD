#include "ExpressionSpinBox.hpp"

#include <nuf/document/Document.hpp>
#include <nuf/document/Expression.hpp>

#include "Units.hpp"

#include <QEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QObject>
#include <QPointer>

#include <algorithm>
#include <cmath>

namespace nuf::app {

namespace {
const document::Document* gDocument = nullptr;

QLineEdit* editOf(QAbstractSpinBox* box)
{
    return box->findChild<QLineEdit*>(QString(), Qt::FindDirectChildrenOnly);
}

// Coloca la etiqueta de la unidad sobre el extremo derecho del campo de texto del cuadro.
class UnitLabel final : public QObject {
public:
    UnitLabel(QAbstractSpinBox* box, const QString& unit)
        : QObject(box)
        , box_(box)
    {
        setObjectName(QStringLiteral("nufUnitLabel"));
        label_ = new QLabel(unit, box);
        label_->setAttribute(Qt::WA_TransparentForMouseEvents);
        label_->setStyleSheet(QStringLiteral("QLabel { background: transparent; color: palette(placeholder-text); }"));
        box->installEventFilter(this);
        if (QLineEdit* edit = editOf(box)) {
            edit->installEventFilter(this);
        }
        place();
    }

    void setUnit(const QString& unit)
    {
        label_->setText(unit);
        place();
    }

    bool eventFilter(QObject*, QEvent* event) override
    {
        switch (event->type()) {
        case QEvent::Resize:
        case QEvent::Move:
        case QEvent::Show:
        case QEvent::FontChange:
        case QEvent::StyleChange:
        case QEvent::LayoutRequest:
            place();
            break;
        default:
            break;
        }
        return false;
    }

private:
    void place()
    {
        QLineEdit* edit = box_ ? editOf(box_) : nullptr;
        if (edit == nullptr) {
            return;
        }
        const int width = label_->fontMetrics().horizontalAdvance(label_->text()) + 8;
        edit->setTextMargins(0, 0, width, 0);
        label_->setGeometry(edit->geometry().right() - width - 2, edit->geometry().top(), width,
                            edit->geometry().height());
        label_->raise();
        label_->setVisible(!label_->text().isEmpty());
    }

    QPointer<QAbstractSpinBox> box_;
    QLabel* label_ = nullptr;
};
}

void setUnit(QAbstractSpinBox* box, const QString& unit)
{
    if (QObject* existing = box->findChild<QObject*>(QStringLiteral("nufUnitLabel"), Qt::FindDirectChildrenOnly)) {
        static_cast<UnitLabel*>(existing)->setUnit(unit);
    } else {
        new UnitLabel(box, unit);
    }
    box->setProperty("nufUnit", unit);
}

QString unitOf(const QAbstractSpinBox* box)
{
    return box->property("nufUnit").toString();
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
    const QString unit = unitOf(this).trimmed();  // por si se escribe «10 mm»
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
        // En un cuadro de longitud, un número a secas (sin letras: ni unidades ni parámetros) está en la
        // unidad elegida; con unidad o parámetros ya viene en milímetros.
        const bool hasLetters = std::any_of(t.begin(), t.end(), [](QChar c) { return c.isLetter(); });
        const double scaled = length_ && !hasLetters ? Units::instance().fromDisplay(result) : result;
        expression_ = plain ? QString() : t;
        expressionValue_ = scaled;
        self->setToolTip(plain ? QString() : QStringLiteral("= %1").arg(scaled, 0, 'g', 8));
        return scaled;
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
    if (!expression_.isEmpty()) {
        return expression_;
    }
    if (length_) {
        return locale().toString(Units::instance().toDisplay(v), 'f', decimals() + Units::instance().extraDecimals());
    }
    return QDoubleSpinBox::textFromValue(v);
}

void ExpressionSpinBox::stepBy(int steps)
{
    if (!length_) {
        QDoubleSpinBox::stepBy(steps);
        return;
    }
    // Un paso es singleStep() unidades de la unidad elegida.
    expression_.clear();
    setValue(value() + steps * singleStep() * Units::instance().factor());
}

void ExpressionSpinBox::setLength(bool on)
{
    length_ = on;
    if (!on) {
        return;
    }
    setUnit(this, Units::instance().unit());
    // Al cambiar la unidad en Configuración, se reescribe el número y la etiqueta.
    connect(&Units::instance(), &Units::changed, this, [this] {
        setUnit(this, Units::instance().unit());
        if (expression_.isEmpty() && lineEdit() != nullptr) {
            lineEdit()->setText(textFromValue(value()));
        }
    });
    if (lineEdit() != nullptr && expression_.isEmpty()) {
        lineEdit()->setText(textFromValue(value()));
    }
}

void ExpressionSpinBox::setExpression(const QString& text)
{
    const double result = valueFromText(text);
    setValue(result);
    if (!expression_.isEmpty() && lineEdit() != nullptr) {
        lineEdit()->setText(expression_);  // aunque el valor no haya cambiado
    }
}

} // namespace nuf::app
