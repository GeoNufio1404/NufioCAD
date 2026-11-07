#include "GeneratorTool.hpp"

#include "ExpressionSpinBox.hpp"

#include <nuf/document/Document.hpp>
#include <nuf/render/Viewport.hpp>

#include <QComboBox>
#include <QFontComboBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QTimer>

namespace nuf::app {

GeneratorTool::GeneratorTool(document::Document& document, render::Viewport& viewport,
                             const document::Generator& generator)
    : FeatureTool(document, viewport, std::nullopt)
    , generator_(generator)
{
}

void GeneratorTool::createFields(QFormLayout* form)
{
    form_ = form;
    for (const document::GeneratorField& f : generator_.fields()) {
        QWidget* widget = nullptr;
        if (f.kind == document::GeneratorField::Kind::Text) {
            auto* edit = new QLineEdit(QString::fromStdString(f.text));
            connect(edit, &QLineEdit::textChanged, this, [this] { updatePreview(); });
            widget = edit;
        } else if (f.kind == document::GeneratorField::Kind::Font) {
            auto* box = new QFontComboBox();
            box->setFontFilters(QFontComboBox::ScalableFonts);
            if (!f.text.empty()) {
                box->setCurrentFont(QFont(QString::fromStdString(f.text)));
            }
            connect(box, &QFontComboBox::currentFontChanged, this, [this] { updatePreview(); });
            widget = box;
        } else if (f.kind == document::GeneratorField::Kind::Count || f.kind == document::GeneratorField::Kind::Angle) {
            auto* box = new ExpressionSpinBox();
            const bool count = f.kind == document::GeneratorField::Kind::Count;
            box->setRange(f.min, f.max);
            box->setDecimals(count ? 0 : 1);
            box->setValue(f.value);
            if (!count) {
                setUnit(box, QStringLiteral("°"));
            }
            box->setKeyboardTracking(false);
            connect(box, &QDoubleSpinBox::valueChanged, this, [this] { updatePreview(); });
            widget = box;
        } else if (f.kind == document::GeneratorField::Kind::Choice) {
            auto* box = new QComboBox();
            for (const std::string& choice : f.choices) {
                box->addItem(QString::fromStdString(choice));
            }
            box->setCurrentIndex(int(f.value));
            connect(box, &QComboBox::currentIndexChanged, this, [this] {
                updateVisibility();
                updatePreview();
            });
            widget = box;
        } else {
            auto* box = new ExpressionSpinBox();
            box->setRange(f.min, f.max);
            box->setDecimals(2);
            box->setValue(f.value);
            box->setLength(true);
            box->setKeyboardTracking(false);
            connect(box, &QDoubleSpinBox::valueChanged, this, [this] { updatePreview(); });
            widget = box;
        }
        if (!f.hint.empty()) {
            widget->setToolTip(QString::fromStdString(f.hint));
        }
        form->addRow(QString::fromStdString(f.label), widget);
        rows_.push_back({f, widget});
    }
    updateVisibility();
}

void GeneratorTool::updateVisibility()
{
    for (const Row& row : rows_) {
        bool visible = true;
        if (!row.field.showKey.empty()) {
            for (const Row& other : rows_) {
                if (other.field.key == row.field.showKey) {
                    visible = static_cast<QComboBox*>(other.widget)->currentIndex() == row.field.showIndex;
                }
            }
        }
        form_->setRowVisible(row.widget, visible);
    }
}

QString GeneratorTool::inputsText() const
{
    QString text = QString::fromStdString(generator_.description());
    if (!rows_.empty()) {
        const std::string summary = generator_.summary(values());
        if (!summary.empty()) {
            text += QStringLiteral("\n") + QString::fromStdString(summary);
        }
    }
    return text;
}

document::GeneratorValues GeneratorTool::values() const
{
    document::GeneratorValues result;
    for (const Row& row : rows_) {
        if (row.field.kind == document::GeneratorField::Kind::Text) {
            result.texts[row.field.key] = static_cast<QLineEdit*>(row.widget)->text().toStdString();
        } else if (row.field.kind == document::GeneratorField::Kind::Font) {
            result.texts[row.field.key] = static_cast<QFontComboBox*>(row.widget)->currentFont().family().toStdString();
        } else if (row.field.kind == document::GeneratorField::Kind::Choice) {
            result[row.field.key] = double(static_cast<QComboBox*>(row.widget)->currentIndex());
        } else {
            result[row.field.key] = static_cast<QDoubleSpinBox*>(row.widget)->value();
        }
    }
    return result;
}

document::Recipe GeneratorTool::recipe() const
{
    const document::GeneratorValues v = values();
    return generator_.make(v, document::freeSuffix(document_, generator_, v));
}

void GeneratorTool::stopPreview()
{
    if (timer_ != nullptr) {
        timer_->stop();
    }
    updating_ = true;
}

void GeneratorTool::accept()
{
    emit accepted();
}

// La vista previa: el diseño que saldría se calcula en un documento aparte (solo lo generado) y se muestra junto a
// lo que ya hay.
void GeneratorTool::updatePreview()
{
    if (updating_ || !panel_) {
        return;
    }
    inputsLabel_->setText(inputsText());
    if (previewed_) {
        if (timer_ == nullptr) {
            timer_ = new QTimer(this);
            timer_->setSingleShot(true);
            connect(timer_, &QTimer::timeout, this, [this] {
                previewed_ = false;  // calcular ya
                updatePreview();
            });
        }
        statusLabel_->setText(QStringLiteral("<span style='color:#8B93A0'>Calculando…</span>"));
        timer_->start(previewDelayMs());
        return;
    }
    const bool first = !firstDone_;
    firstDone_ = true;
    previewed_ = true;
    if (timer_ != nullptr) {
        timer_->stop();
    }
    document::Document scratch;
    std::string problem;
    try {
        document::applyRecipe(scratch, recipe());
        for (std::size_t i = 0; i < scratch.featureCount() && problem.empty(); ++i) {
            const document::FeatureStatus status = scratch.status(scratch.feature(i).id());
            if (status.health == document::Health::Error) {
                problem = scratch.feature(i).name() + ": " + status.message;
            }
        }
    } catch (const std::exception& e) {
        problem = e.what();
        if (problem.find_first_not_of(' ') == std::string::npos) {
            problem = "No se pudo calcular con esos valores";
        }
    } catch (...) {
        problem = "No se pudo calcular con esos valores";
    }
    std::vector<document::Body> shown = before().bodies;
    for (const document::Body& body : scratch.result().bodies) {
        shown.push_back({"gen:" + body.key, body.name, body.shape});
    }
    viewport_.showBodies(shown);
    if (first) {
        viewport_.fitAll();
    }
    if (problem.empty()) {
        statusLabel_->clear();
        okButton_->setEnabled(true);
    } else {
        statusLabel_->setText(QStringLiteral("<span style='color:#E5484D'>%1</span>")
                                  .arg(QString::fromStdString(problem).toHtmlEscaped()));
        okButton_->setEnabled(false);
    }
}

} // namespace nuf::app
