#include "ValueInput.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>

namespace nuf::app {

ValueInput::ValueInput(QWidget* parent)
    : QFrame(parent)
{
    setFrameShape(QFrame::StyledPanel);
    setAutoFillBackground(true);
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(6, 3, 6, 3);
    layout->setSpacing(6);
    hide();
}

void ValueInput::setFields(const std::vector<QString>& labels, const std::vector<double>& current)
{
    if (labels != labels_) {
        // Campos nuevos (otra herramienta u otro paso): se rehacen.
        for (QLabel* caption : captions_) {
            delete caption;
        }
        for (QLineEdit* edit : edits_) {
            delete edit;
        }
        captions_.clear();
        edits_.clear();
        labels_ = labels;
        for (const QString& label : labels) {
            auto* caption = new QLabel(label, this);
            auto* edit = new QLineEdit(this);
            edit->setFixedWidth(64);
            connect(edit, &QLineEdit::returnPressed, this, &ValueInput::accepted);
            layout()->addWidget(caption);
            layout()->addWidget(edit);
            captions_.push_back(caption);
            edits_.push_back(edit);
        }
        adjustSize();
    }
    for (std::size_t i = 0; i < edits_.size() && i < current.size(); ++i) {
        edits_[i]->setPlaceholderText(QLocale().toString(current[i], 'f', 2));
    }
}

void ValueInput::startTyping(const QString& text)
{
    if (edits_.empty()) {
        return;
    }
    edits_.front()->setFocus();
    edits_.front()->setText(text);
}

bool ValueInput::isTyping() const
{
    for (const QLineEdit* edit : edits_) {
        if (!edit->text().isEmpty() || edit->hasFocus()) {
            return true;
        }
    }
    return false;
}

void ValueInput::clear()
{
    for (QLineEdit* edit : edits_) {
        edit->clear();
    }
}

std::vector<std::optional<double>> ValueInput::values() const
{
    std::vector<std::optional<double>> result;
    for (const QLineEdit* edit : edits_) {
        // Acepta coma o punto decimal.
        QString text = edit->text().trimmed();
        text.replace(QLatin1Char(','), QLatin1Char('.'));
        bool ok = false;
        const double value = text.toDouble(&ok);
        result.push_back(ok ? std::optional<double>(value) : std::nullopt);
    }
    return result;
}

} // namespace nuf::app
