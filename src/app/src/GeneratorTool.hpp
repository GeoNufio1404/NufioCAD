#pragma once

// Panel de una herramienta generadora (Lightbox…): las preguntas del generador, la vista previa en vivo del diseño
// que saldría y, al aceptar, la receta lista para aplicarla de una vez.

#include "FeatureTools.hpp"

#include <nuf/document/Generators.hpp>

#include <map>

class QComboBox;
class QDoubleSpinBox;

namespace nuf::app {

class GeneratorTool final : public FeatureTool {
    Q_OBJECT

public:
    GeneratorTool(document::Document& document, render::Viewport& viewport, const document::Generator& generator);
    // Reabre el formulario de lo que ya sacó el generador: «lead» identifica ese resultado, y el formulario parte
    // de sus respuestas y de su sufijo de parámetros.
    GeneratorTool(document::Document& document, render::Viewport& viewport, const document::Generator& generator,
                  document::FeatureId lead, document::GeneratorValues initial, std::string suffix);

    std::unique_ptr<document::Feature> build() const override { return nullptr; }
    void accept() override;
    void stopPreview() override;

    const document::Generator& generator() const { return generator_; }
    // El resultado que se reabre (0: uno nuevo).
    document::FeatureId lead() const { return lead_; }
    // La receta con los valores de ahora y nombres de parámetros que no choquen con los del documento.
    document::Recipe recipe() const;

protected:
    QString title() const override { return QString::fromStdString(generator_.title()); }
    int previewDelayMs() const override { return 250; }
    void createFields(QFormLayout* form) override;
    void selectionChanged(const render::ModelSelection&) override {}
    QString inputsText() const override;
    bool removesMaterial() const override { return false; }
    void updatePreview() override;

private:
    document::GeneratorValues values() const;
    void updateVisibility();

    struct Row {
        document::GeneratorField field;
        QWidget* widget = nullptr;
    };
    const document::Generator& generator_;
    document::FeatureId lead_ = 0;
    document::GeneratorValues initial_;
    bool hasInitial_ = false;
    std::string suffix_;
    QFormLayout* form_ = nullptr;
    std::vector<Row> rows_;
    QTimer* timer_ = nullptr;
    bool previewed_ = false;
    bool firstDone_ = false;
};

} // namespace nuf::app
