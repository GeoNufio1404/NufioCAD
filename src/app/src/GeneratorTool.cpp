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
#include <QSettings>
#include <QVBoxLayout>
#include <QTimer>

#include <algorithm>
#include <cctype>

namespace nuf::app {

GeneratorTool::GeneratorTool(document::Document& document, render::Viewport& viewport,
                             const document::Generator& generator)
    : FeatureTool(document, viewport, std::nullopt)
    , generator_(generator)
{
}

GeneratorTool::GeneratorTool(document::Document& document, render::Viewport& viewport, const document::Generator& generator,
                             document::FeatureId lead, document::GeneratorValues initial, std::string suffix)
    : FeatureTool(document, viewport, std::nullopt)
    , generator_(generator)
    , lead_(lead)
    , initial_(std::move(initial))
    , hasInitial_(true)
    , suffix_(std::move(suffix))
{
}

void GeneratorTool::createFields(QFormLayout* form)
{
    form_ = form;
    for (document::GeneratorField f : generator_.fields()) {
        // Un formulario nuevo parte de las últimas respuestas que se aceptaron con este generador.
        if (!hasInitial_) {
            QSettings settings;
            const QString base = QStringLiteral("generator/%1/").arg(QString::fromStdString(generator_.id()));
            const QString key = base + QString::fromStdString(f.key);
            if (f.kind == document::GeneratorField::Kind::Text || f.kind == document::GeneratorField::Kind::Font) {
                if (settings.contains(key)) {
                    f.text = settings.value(key).toString().toStdString();
                }
            } else if (settings.contains(key)) {
                f.value = std::clamp(settings.value(key).toDouble(), f.kind == document::GeneratorField::Kind::Choice ? 0.0 : f.min,
                                     f.kind == document::GeneratorField::Kind::Choice ? double(f.choices.size()) - 1.0 : f.max);
            }
        }
        // Al reabrir, el formulario parte de las respuestas de antes.
        if (hasInitial_) {
            if (const auto it = initial_.find(f.key); it != initial_.end()) {
                f.value = it->second;
            }
            if (const auto it = initial_.texts.find(f.key); it != initial_.texts.end()) {
                f.text = it->second;
            }
        }
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
        // La ayuda se ve siempre, en pequeño y en gris bajo el campo (y también como tooltip).
        auto* container = new QWidget();
        auto* column = new QVBoxLayout(container);
        column->setContentsMargins(0, 0, 0, 0);
        column->setSpacing(1);
        column->addWidget(widget);
        // La ayuda sale como tooltip al dejar el ratón sobre el campo.
        if (!f.hint.empty()) {
            widget->setToolTip(QString::fromStdString(f.hint));
        }
        form->addRow(QString::fromStdString(f.label), container);
        rows_.push_back({f, widget, container});
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
        form_->setRowVisible(row.container, visible);
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
    const std::string suffix = lead_ != 0 ? suffix_ : document::freeSuffix(document_, generator_, v);
    document::Recipe made = generator_.make(v, suffix);
    made.origin = {generator_.id(), suffix, document::encodeValues(v), 0};
    return made;
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
    // Se recuerdan las respuestas para el próximo formulario de este generador.
    if (lead_ == 0 && !hasInitial_) {
        const document::GeneratorValues v = values();
        QSettings settings;
        const QString base = QStringLiteral("generator/%1/").arg(QString::fromStdString(generator_.id()));
        for (const auto& [key, value] : v) {
            settings.setValue(base + QString::fromStdString(key), value);
        }
        for (const auto& [key, text] : v.texts) {
            settings.setValue(base + QString::fromStdString(key), QString::fromStdString(text));
        }
    }
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
    std::vector<document::Body> shown;
    // Al reabrir, lo que ya había sacado el generador se oculta: lo sustituye la vista previa.
    std::vector<std::string> replaced;
    for (const document::FeatureId id : document::generatedGroup(document_, lead_)) {
        replaced.push_back(document_.findFeature(id)->tag());
    }
    for (const document::Body& body : before().bodies) {
        const bool old = std::ranges::any_of(replaced, [&](const std::string& tag) {
            return body.key.rfind(tag, 0) == 0 && (body.key.size() == tag.size() || !std::isdigit(static_cast<unsigned char>(body.key[tag.size()])));
        });
        if (!old) {
            shown.push_back(body);
        }
    }
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
