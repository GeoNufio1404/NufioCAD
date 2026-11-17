#pragma once

// Panel de una herramienta generadora (Lightbox…): las preguntas del generador, la vista previa en vivo del diseño
// que saldría y, al aceptar, la receta lista para aplicarla de una vez.

#include "FeatureTools.hpp"

#include <nuf/document/Generators.hpp>

#include <map>
#include <optional>

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
    // Lo que se calcula en el hilo de trabajo: el diseño que saldría, en un documento aparte (solo lo generado).
    struct Calculation {
        std::shared_ptr<const document::Recipe> recipe;
        document::Document scratch;
        std::string problem;
    };
    void showResult(const Calculation& calculation, bool first);
    // La receta de la última vista previa que salió bien, con las respuestas que la hicieron: al aceptar se reutiliza
    // (armarla otra vez en el hilo de la interfaz repetía, por ejemplo, el contorno de las letras del Letrero).
    std::string lastKey_;
    std::shared_ptr<const document::Recipe> lastRecipe_;
    document::GeneratorValues values() const;
    void resetValues();
    void updateVisibility();
    std::optional<Arrow> dimensionArrow(const std::string& key) const override;

    struct Row {
        document::GeneratorField field;
        QWidget* widget = nullptr;
        QWidget* container = nullptr;  // el campo con su ayuda debajo (lo que se muestra y oculta)
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
    std::vector<geometry::Box> bounds_;  // de los cuerpos de la última vista previa, para colocar las flechas
};

} // namespace nuf::app
