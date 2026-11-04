#pragma once

// Herramientas de operaciones sólidas (Extruir, Revolución): eligen las
// entradas en la vista 3D, muestran un panel con los parámetros y una vista
// previa en vivo, y al aceptar entregan la operación para agregarla (o
// sustituir la que se editaba) con un comando.

#include <nuf/document/Document.hpp>
#include <nuf/document/Features.hpp>

#include <QObject>
#include <QPointer>
#include <QString>

#include <memory>
#include <optional>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QLabel;
class QPushButton;
class QWidget;

namespace nuf::render {
class Viewport;
struct ModelSelection;
}

class QTimer;

namespace nuf::app {

class FeatureTool : public QObject {
    Q_OBJECT

public:
    // Con «editing», edita esa operación; si no, crea una nueva en el marcador.
    FeatureTool(document::Document& document, render::Viewport& viewport,
                std::optional<document::FeatureId> editing);
    ~FeatureTool() override;

    QWidget* panel() const { return panel_; }
    std::optional<document::FeatureId> editing() const { return editing_; }
    // Id de la operación (la que se edita o la reservada para la nueva).
    document::FeatureId featureId() const { return id_; }
    // Posición en el historial donde se calcula.
    std::size_t position() const { return index_; }

    // Llamar tras conectar las señales.
    void start();
    // La operación con los parámetros actuales (nullptr si faltan entradas).
    virtual std::unique_ptr<document::Feature> build() const = 0;
    void accept();

signals:
    void accepted();
    void cancelled();
    void message(const QString& text);

protected:
    // «Extruir», «Revolución».
    virtual QString title() const = 0;
    // Construye los campos del panel (después de crear el panel base).
    virtual void createFields(QFormLayout* form) = 0;
    // La selección de la vista 3D cambió: tomar las entradas.
    virtual void selectionChanged(const render::ModelSelection& selection) = 0;
    // Tras calcular la vista previa (para colocar la flecha, etc.).
    virtual void afterPreview(const document::ModelState& /*before*/) {}
    // Texto de las entradas («2 perfiles»…), para el panel.
    virtual QString inputsText() const = 0;
    virtual bool removesMaterial() const = 0;

    void updatePreview();
    // Perfiles de la selección como referencias del documento.
    std::vector<document::ProfileRef> profilesOf(const render::ModelSelection& selection) const;
    const document::ModelState& before() const { return document_.stateBefore(index_); }

    document::Document& document_;
    render::Viewport& viewport_;
    std::optional<document::FeatureId> editing_;
    document::FeatureId id_ = 0;
    std::size_t index_ = 0;
    std::unique_ptr<document::Feature> original_;  // la que se edita
    QPointer<QWidget> panel_;
    QLabel* inputsLabel_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QPushButton* okButton_ = nullptr;
    bool updating_ = false;  // al rellenar los campos, sin vista previa
};

class ExtrudeTool final : public FeatureTool {
    Q_OBJECT

public:
    ExtrudeTool(document::Document& document, render::Viewport& viewport,
                std::optional<document::FeatureId> editing);

    std::unique_ptr<document::Feature> build() const override;

protected:
    QString title() const override { return QStringLiteral("Extruir"); }
    void createFields(QFormLayout* form) override;
    void selectionChanged(const render::ModelSelection& selection) override;
    void afterPreview(const document::ModelState& before) override;
    QString inputsText() const override;
    bool removesMaterial() const override;

private:
    void updateVisibility();
    // Unir o cortar según hacia dónde va la extrusión (si no se eligió a mano).
    void chooseOperation();

    std::vector<document::ProfileRef> profiles_;
    geometry::ElementRef face_;
    geometry::ElementRef toFace1_;
    geometry::ElementRef toFace2_;
    int pickingTarget_ = 0;  // 1 o 2: el próximo clic en una cara es el destino
    std::vector<geometry::ElementRef> selectedFaces_;  // para saber cuál es nueva
    bool operationTouched_ = false;
    int lastSign_ = 1;  // signo de la distancia al elegir la operación
    QTimer* dragTimer_ = nullptr;  // agrupa los movimientos de la flecha de distancia
    double pendingDistance_ = 0.0;

    QComboBox* direction_ = nullptr;
    QComboBox* extent1_ = nullptr;
    QDoubleSpinBox* distance1_ = nullptr;
    QDoubleSpinBox* taper1_ = nullptr;
    QPushButton* target1_ = nullptr;
    QComboBox* extent2_ = nullptr;
    QDoubleSpinBox* distance2_ = nullptr;
    QDoubleSpinBox* taper2_ = nullptr;
    QPushButton* target2_ = nullptr;
    QDoubleSpinBox* offset_ = nullptr;
    QComboBox* operation_ = nullptr;
    QFormLayout* form_ = nullptr;
};

class RevolveTool final : public FeatureTool {
    Q_OBJECT

public:
    RevolveTool(document::Document& document, render::Viewport& viewport,
                std::optional<document::FeatureId> editing);

    std::unique_ptr<document::Feature> build() const override;

protected:
    QString title() const override { return QStringLiteral("Revolución"); }
    void createFields(QFormLayout* form) override;
    void selectionChanged(const render::ModelSelection& selection) override;
    QString inputsText() const override;
    bool removesMaterial() const override;

private:
    void updateVisibility();

    std::vector<document::ProfileRef> profiles_;
    document::AxisRef axis_;
    QComboBox* axisKind_ = nullptr;
    QLabel* axisLabel_ = nullptr;
    QComboBox* extent_ = nullptr;
    QComboBox* direction_ = nullptr;
    QDoubleSpinBox* angle1_ = nullptr;
    QDoubleSpinBox* angle2_ = nullptr;
    QComboBox* operation_ = nullptr;
    QFormLayout* form_ = nullptr;
};

} // namespace nuf::app
