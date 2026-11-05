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
#include <string>
#include <utility>
#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QLabel;
class QPushButton;
class QSpinBox;
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
    // Lo mismo, con las expresiones de los cuadros que usan parámetros: es la
    // que se debe usar para calcular y aplicar.
    std::unique_ptr<document::Feature> make() const;
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
    // Registra un cuadro numérico como el campo «key» de la operación, para
    // poder ligarlo a parámetros (se llama al crearlo en createFields).
    void field(const char* key, QDoubleSpinBox* box) { fields_.emplace_back(key, box); }
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

private:
    std::vector<std::pair<std::string, QDoubleSpinBox*>> fields_;
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
    std::vector<document::FeatureId> axisIds_;  // ejes de construcción del desplegable (tras los 4 fijos)
    QComboBox* axisKind_ = nullptr;
    QLabel* axisLabel_ = nullptr;
    QComboBox* extent_ = nullptr;
    QComboBox* direction_ = nullptr;
    QDoubleSpinBox* angle1_ = nullptr;
    QDoubleSpinBox* angle2_ = nullptr;
    QComboBox* operation_ = nullptr;
    QFormLayout* form_ = nullptr;
};

// Redondeo y chaflán: eligen aristas de un cuerpo y una medida. Con «editing»
// el tipo sale de la operación; si no, de «style».
class EdgeTool final : public FeatureTool {
    Q_OBJECT

public:
    enum class Style { Fillet, Chamfer };

    EdgeTool(document::Document& document, render::Viewport& viewport, std::optional<document::FeatureId> editing,
             Style style = Style::Fillet);

    std::unique_ptr<document::Feature> build() const override;

protected:
    QString title() const override;
    void createFields(QFormLayout* form) override;
    void selectionChanged(const render::ModelSelection& selection) override;
    QString inputsText() const override;
    bool removesMaterial() const override { return false; }
    void afterPreview(const document::ModelState& before) override;

private:
    Style style_;
    std::vector<geometry::ElementRef> edges_;
    QDoubleSpinBox* size_ = nullptr;
};

// Vaciado: elige las caras que quedan abiertas y el espesor de las paredes.
class ShellTool final : public FeatureTool {
    Q_OBJECT

public:
    ShellTool(document::Document& document, render::Viewport& viewport, std::optional<document::FeatureId> editing);

    std::unique_ptr<document::Feature> build() const override;

protected:
    QString title() const override { return QStringLiteral("Vaciado"); }
    void createFields(QFormLayout* form) override;
    void selectionChanged(const render::ModelSelection& selection) override;
    void afterPreview(const document::ModelState& before) override;
    QString inputsText() const override;
    bool removesMaterial() const override { return false; }

private:
    std::vector<geometry::ElementRef> faces_;
    QDoubleSpinBox* thickness_ = nullptr;
    QComboBox* direction_ = nullptr;
};

// Agujeros: elige el sketch cuyos círculos y puntos marcan dónde van, el tipo
// (simple, abocardado, avellanado) y sus medidas.
class HoleTool final : public FeatureTool {
    Q_OBJECT

public:
    HoleTool(document::Document& document, render::Viewport& viewport, std::optional<document::FeatureId> editing);

    std::unique_ptr<document::Feature> build() const override;

protected:
    QString title() const override { return QStringLiteral("Agujero"); }
    void createFields(QFormLayout* form) override;
    void selectionChanged(const render::ModelSelection& selection) override;
    QString inputsText() const override;
    bool removesMaterial() const override { return true; }

private:
    void updateVisibility();

    document::FeatureId sketch_ = 0;
    QComboBox* sketchBox_ = nullptr;
    QComboBox* kind_ = nullptr;
    QDoubleSpinBox* diameter_ = nullptr;
    QCheckBox* throughAll_ = nullptr;
    QDoubleSpinBox* depth_ = nullptr;
    QDoubleSpinBox* headDiameter_ = nullptr;
    QDoubleSpinBox* headDepth_ = nullptr;
    QDoubleSpinBox* headAngle_ = nullptr;
    QCheckBox* flip_ = nullptr;
    QFormLayout* form_ = nullptr;
};

// Espejo: elige el cuerpo, el plano de simetría (de origen o una cara plana en
// la vista) y si se une o queda como cuerpo nuevo.
class MirrorTool final : public FeatureTool {
    Q_OBJECT

public:
    MirrorTool(document::Document& document, render::Viewport& viewport, std::optional<document::FeatureId> editing);

    std::unique_ptr<document::Feature> build() const override;

protected:
    QString title() const override { return QStringLiteral("Espejo"); }
    void createFields(QFormLayout* form) override;
    void selectionChanged(const render::ModelSelection& selection) override;
    QString inputsText() const override;
    bool removesMaterial() const override { return false; }

private:
    std::string body_;
    geometry::ElementRef face_;
    std::vector<document::FeatureId> planeIds_;  // los planos de construcción del desplegable (tras los 4 fijos)
    QComboBox* bodyBox_ = nullptr;
    QComboBox* plane_ = nullptr;
    QComboBox* operation_ = nullptr;
};

// Plano de construcción: parte de un plano de origen, una cara u otro plano,
// con un desfase y un giro.
class PlaneTool final : public FeatureTool {
    Q_OBJECT

public:
    PlaneTool(document::Document& document, render::Viewport& viewport, std::optional<document::FeatureId> editing);

    std::unique_ptr<document::Feature> build() const override;

protected:
    QString title() const override { return QStringLiteral("Plano de construcción"); }
    void createFields(QFormLayout* form) override;
    void selectionChanged(const render::ModelSelection& selection) override;
    QString inputsText() const override;
    bool removesMaterial() const override { return false; }

private:
    geometry::ElementRef face_;
    std::vector<document::FeatureId> planeIds_;  // los planos de construcción del desplegable (tras los 4 fijos)
    document::FeatureId reference_ = 0;
    QComboBox* base_ = nullptr;
    QDoubleSpinBox* offset_ = nullptr;
    QDoubleSpinBox* angle_ = nullptr;
    QComboBox* angleAxis_ = nullptr;
};

// Eje de construcción: intersección de dos planos, o perpendicular a un plano.
class AxisTool final : public FeatureTool {
    Q_OBJECT

public:
    AxisTool(document::Document& document, render::Viewport& viewport, std::optional<document::FeatureId> editing);

    std::unique_ptr<document::Feature> build() const override;

protected:
    QString title() const override { return QStringLiteral("Eje de construcción"); }
    void createFields(QFormLayout* form) override;
    void selectionChanged(const render::ModelSelection& selection) override;
    QString inputsText() const override;
    bool removesMaterial() const override { return false; }

private:
    geometry::ElementRef face_;
    std::vector<document::FeatureId> planeIds_;  // planos de construcción de los desplegables (tras los fijos)
    QComboBox* first_ = nullptr;
    QComboBox* second_ = nullptr;
};

// Patrón: repite un cuerpo en fila o alrededor de un eje de origen.
class PatternTool final : public FeatureTool {
    Q_OBJECT

public:
    PatternTool(document::Document& document, render::Viewport& viewport, std::optional<document::FeatureId> editing);

    std::unique_ptr<document::Feature> build() const override;

protected:
    QString title() const override { return QStringLiteral("Patrón"); }
    void createFields(QFormLayout* form) override;
    void selectionChanged(const render::ModelSelection&) override {}
    QString inputsText() const override;
    bool removesMaterial() const override { return false; }

private:
    void updateVisibility();

    std::string body_;
    std::vector<document::FeatureId> axisIds_;  // ejes de construcción del desplegable (tras X, Y, Z)
    QFormLayout* form_ = nullptr;
    QComboBox* bodyBox_ = nullptr;
    QComboBox* layout_ = nullptr;
    QComboBox* axis_ = nullptr;
    QSpinBox* count_ = nullptr;
    QDoubleSpinBox* spacing_ = nullptr;
    QDoubleSpinBox* angle_ = nullptr;
    QComboBox* operation_ = nullptr;
};

} // namespace nuf::app
