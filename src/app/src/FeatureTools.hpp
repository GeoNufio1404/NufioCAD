#pragma once

// Herramientas de operaciones sólidas (Extruir, Revolución): eligen las
// entradas en la vista 3D, muestran un panel con los parámetros y una vista
// previa en vivo, y al aceptar entregan la operación para agregarla (o
// sustituir la que se editaba) con un comando.

#include "BackgroundRun.hpp"

#include <nuf/document/Document.hpp>
#include <nuf/document/Features.hpp>

#include <QObject>
#include <QPointer>

#include "FieldMarks.hpp"
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
class QListWidget;
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
    // El campo numérico principal de la herramienta (la distancia de Extruir, el ángulo de Revolución…): el primero
    // que no es una posición ni un giro. Sirve para escribir su valor junto al cursor (U7). «box» nulo: no hay.
    struct PrimaryField {
        QString label;
        QDoubleSpinBox* box = nullptr;
    };
    PrimaryField primaryField() const;
    // Id de la operación (la que se edita o la reservada para la nueva).
    document::FeatureId featureId() const { return id_; }
    // Posición en el historial donde se calcula.
    std::size_t position() const { return index_; }

    // Llamar tras conectar las señales.
    void start();
    // true mientras la vista previa se está calculando en segundo plano.
    bool previewing() const { return previewRun_.running(); }
    // La operación con los parámetros actuales (nullptr si faltan entradas).
    virtual std::unique_ptr<document::Feature> build() const = 0;
    // Lo mismo, con las expresiones de los cuadros que usan parámetros: es la
    // que se debe usar para calcular y aplicar.
    std::unique_ptr<document::Feature> make() const;
    virtual void accept();
    // Al cerrar la herramienta: se cancela la vista previa que estuviera esperando su turno (si no, se
    // calcularía y se dibujaría después de aceptar o cancelar).
    virtual void stopPreview();

signals:
    void accepted();
    void cancelled();
    void message(const QString& text);
    // La vista previa empezó (true) o terminó de calcularse (false): la ventana muestra «Calculando…» sobre la vista.
    void computingChanged(bool computing);

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
    // Milisegundos de espera tras el último cambio antes de calcular la vista previa: las operaciones
    // pesadas (rosca, engranaje) lo usan para no recalcular a cada tecla; 0 = al instante.
    virtual int previewDelayMs() const { return 0; }

    virtual void updatePreview();

    // Dónde va un cuerpo generado (rosca, engranaje): un plano de origen, un plano de construcción o una cara
    // plana elegida en la vista, y una posición X/Y en ese plano (al elegir una cara, el centro de la cara).
    struct Placement {
        QComboBox* base = nullptr;  // 3 de origen, los de construcción y, si hay, «Cara elegida»
        QDoubleSpinBox* x = nullptr;
        QDoubleSpinBox* y = nullptr;
        std::vector<document::FeatureId> planeIds;
        geometry::ElementRef face;
    };
    void createPlacement(QFormLayout* form, Placement& placement, const document::SketchAttachment* original, double x,
                         double y);
    // Toma la cara plana de la selección, si la hay; true si cambió algo.
    bool placementSelection(Placement& placement, const render::ModelSelection& selection);
    document::SketchAttachment placementAttachment(const Placement& placement) const;
    bool placementOnFace(const Placement& placement) const;
    // Registra un cuadro numérico como el campo «key» de la operación, para
    // poder ligarlo a parámetros (se llama al crearlo en createFields).
    void field(const char* key, QDoubleSpinBox* box)
    {
        fields_.emplace_back(key, box);
        track(key, box);
    }
    // Los extremos de una flecha de medida en el modelo.
    using Arrow = std::pair<sketch::Vec3, sketch::Vec3>;
    // Registra un cuadro numérico para que, mientras se edita, se dibuje en la vista la flecha de doble punta que
    // devuelve dimensionArrow(key) (sin ligarlo a parámetros: para eso es field()).
    void track(const char* key, QDoubleSpinBox* box) { arrowBoxes_.emplace_back(key, box); }
    // Dónde dibujar la flecha de la medida «key» con los valores de ahora (nullopt: no hay flecha).
    virtual std::optional<Arrow> dimensionArrow(const std::string& /*key*/) const { return std::nullopt; }
    // La flecha de la posición X o Y de una colocación (en el plano o la cara elegida), si «key» es una de ellas.
    std::optional<Arrow> placementArrow(const Placement& placement, const std::string& key) const;
    // Un ángulo: arco alrededor del eje «axis» que pasa por «center», desde «start» y girando «angle» (rad).
    struct Arc {
        sketch::Vec3 center;
        sketch::Vec3 axis;
        sketch::Vec3 start;
        double angle = 0.0;
    };
    virtual std::optional<Arc> dimensionArc(const std::string& /*key*/) const { return std::nullopt; }
    // Radio razonable para dibujar un arco: una parte del tamaño de lo que hay en la escena.
    double arcRadius() const;
    // Un punto a «radius» de «center» perpendicular al eje «axis» (donde empieza el arco).
    static sketch::Vec3 perpendicularPoint(const sketch::Vec3& center, const sketch::Vec3& axis, double radius);
    // Vuelve a colocar (o quita) la flecha de la medida que se está editando.
    void refreshArrow();
    // Perfiles de la selección como referencias del documento.
    std::vector<document::ProfileRef> profilesOf(const render::ModelSelection& selection) const;
    const document::ModelState& before() const { return document_.stateBefore(index_); }
    // Centro de la caja envolvente de un cuerpo de antes de la operación.
    std::optional<sketch::Vec3> bodyCenter(const std::string& key) const;
    // Flecha de «length» a lo largo de «dir» desde «from» (una medida lineal), o de «length» ± centrada.
    static Arrow arrowAlong(const sketch::Vec3& from, const sketch::Vec3& dir, double length);

    // Dibuja el resultado de la vista previa (en el hilo de la interfaz, cuando el cálculo ya acabó).
    void showPreview(const document::Feature& feature, const document::Preview& result, const geometry::Shape& tool);

    document::Document& document_;
    render::Viewport& viewport_;
    BackgroundRun previewRun_;  // la vista previa se calcula en un hilo aparte: una rosca no cuelga la interfaz
    std::optional<document::FeatureId> editing_;
    document::FeatureId id_ = 0;
    std::size_t index_ = 0;
    std::unique_ptr<document::Feature> original_;  // la que se edita
    QPointer<QWidget> panel_;
    QLabel* inputsLabel_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QPushButton* okButton_ = nullptr;
    bool updating_ = false;  // al rellenar los campos, sin vista previa

    // Aviso que no viene del cálculo sino de algo que la herramienta puso sola (un ángulo recomendado, por ejemplo): sale
    // en naranja bajo los cuadros y el cuadro «field» se marca también; se quita al cambiar ese cuadro a mano.
    void setNotice(const QString& text, QDoubleSpinBox* field);
    void clearNotice();
    // Los cuadros registrados con field(), con su clave, para marcarlos.
    std::vector<std::pair<std::string, QWidget*>> markableFields() const;
    // Pone el estado de abajo (el texto ya con su color) y marca los cuadros que el mensaje nombra: rojo si es un error,
    // naranja si es un aviso; después añade el aviso propio, si hay.
    void showStatus(const QString& message, Mark level, const std::vector<std::pair<std::string, QWidget*>>& widgets);
    QString notice_;
    QPointer<QDoubleSpinBox> noticeField_;

    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    std::vector<std::pair<std::string, QDoubleSpinBox*>> fields_;
    std::vector<std::pair<std::string, QDoubleSpinBox*>> arrowBoxes_;  // los que tienen flecha
    std::string focusedKey_;                                           // la medida que se edita ahora
    QTimer* previewTimer_ = nullptr;
    bool previewDue_ = false;       // llamada del temporizador: calcular ya
    bool firstPreviewDone_ = false;  // la primera vista previa sale al instante
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
    std::optional<Arrow> dimensionArrow(const std::string& key) const override;
    std::optional<Arc> dimensionArc(const std::string& key) const override;
    bool removesMaterial() const override;

private:
    // Centro y normal de la base de la extrusión (el primer perfil o la cara elegida).
    std::optional<std::pair<sketch::Vec3, sketch::Vec3>> baseOf() const;
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
    std::optional<Arc> dimensionArc(const std::string& key) const override;
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
    std::optional<Arrow> dimensionArrow(const std::string& key) const override;
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
    std::optional<Arrow> dimensionArrow(const std::string& key) const override;
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
    std::optional<Arrow> dimensionArrow(const std::string& key) const override;
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
    std::optional<Arrow> dimensionArrow(const std::string& key) const override;
    std::optional<Arc> dimensionArc(const std::string& key) const override;
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

// Engranaje: módulo, dientes, ángulo de presión, ancho, agujero y hélice, sobre un
// plano de origen o de construcción.
class GearTool final : public FeatureTool {
    Q_OBJECT

public:
    GearTool(document::Document& document, render::Viewport& viewport, std::optional<document::FeatureId> editing);

    std::unique_ptr<document::Feature> build() const override;

protected:
    QString title() const override { return QStringLiteral("Engranaje"); }
    int previewDelayMs() const override { return 250; }
    void createFields(QFormLayout* form) override;
    void selectionChanged(const render::ModelSelection& selection) override;
    QString inputsText() const override;
    std::optional<Arrow> dimensionArrow(const std::string& key) const override;
    std::optional<Arc> dimensionArc(const std::string& key) const override;
    bool removesMaterial() const override { return false; }

private:
    // En una cara, «Cortar» va hacia dentro de la pieza y lo demás hacia fuera (el signo del ancho).
    void adjustDirection();
    // Pone los valores de fábrica del tipo elegido (dientes, módulo, ancho…), sin tocar la colocación ni la operación.
    void applyTypeDefaults();

    Placement placement_;
    QDoubleSpinBox* module_ = nullptr;
    QDoubleSpinBox* teeth_ = nullptr;
    QDoubleSpinBox* rotation_ = nullptr;
    QDoubleSpinBox* pressure_ = nullptr;
    QDoubleSpinBox* thickness_ = nullptr;
    QDoubleSpinBox* bore_ = nullptr;
    QDoubleSpinBox* helix_ = nullptr;
    QDoubleSpinBox* rim_ = nullptr;
    QCheckBox* herringbone_ = nullptr;
    QDoubleSpinBox* backlash_ = nullptr;
    QDoubleSpinBox* baseChamfer_ = nullptr;
    QDoubleSpinBox* shift_ = nullptr;
    QDoubleSpinBox* wormDiameter_ = nullptr;
    QDoubleSpinBox* mateTeeth_ = nullptr;
    QDoubleSpinBox* groove_ = nullptr;
    QCheckBox* leftHanded_ = nullptr;
    QComboBox* type_ = nullptr;
    QComboBox* operation_ = nullptr;
};

// Caja y cilindro: primitivas con posición en un plano o cara, ligables a parámetros.
class BoxTool final : public FeatureTool {
    Q_OBJECT

public:
    BoxTool(document::Document& document, render::Viewport& viewport, std::optional<document::FeatureId> editing);

    std::unique_ptr<document::Feature> build() const override;

protected:
    QString title() const override { return QStringLiteral("Caja"); }
    void createFields(QFormLayout* form) override;
    void selectionChanged(const render::ModelSelection& selection) override;
    QString inputsText() const override;
    std::optional<Arrow> dimensionArrow(const std::string& key) const override;
    bool removesMaterial() const override;

private:
    Placement placement_;
    QDoubleSpinBox* z_ = nullptr;
    QDoubleSpinBox* width_ = nullptr;
    QDoubleSpinBox* depth_ = nullptr;
    QDoubleSpinBox* height_ = nullptr;
    QDoubleSpinBox* radius_ = nullptr;
    QCheckBox* centered_ = nullptr;
    QCheckBox* symmetric_ = nullptr;
    QComboBox* operation_ = nullptr;
};

class CylinderTool final : public FeatureTool {
    Q_OBJECT

public:
    CylinderTool(document::Document& document, render::Viewport& viewport, std::optional<document::FeatureId> editing);

    std::unique_ptr<document::Feature> build() const override;

protected:
    QString title() const override { return QStringLiteral("Cilindro"); }
    void createFields(QFormLayout* form) override;
    void selectionChanged(const render::ModelSelection& selection) override;
    QString inputsText() const override;
    std::optional<Arrow> dimensionArrow(const std::string& key) const override;
    bool removesMaterial() const override;

private:
    Placement placement_;
    QDoubleSpinBox* z_ = nullptr;
    QDoubleSpinBox* diameter_ = nullptr;
    QDoubleSpinBox* height_ = nullptr;
    QCheckBox* symmetric_ = nullptr;
    QComboBox* operation_ = nullptr;
};

// Barrido: perfiles (regiones de un sketch) a lo largo de curvas de otro sketch.
class SweepTool final : public FeatureTool {
    Q_OBJECT

public:
    SweepTool(document::Document& document, render::Viewport& viewport, std::optional<document::FeatureId> editing);

    std::unique_ptr<document::Feature> build() const override;

protected:
    QString title() const override { return QStringLiteral("Barrido"); }
    void createFields(QFormLayout* form) override;
    void selectionChanged(const render::ModelSelection& selection) override;
    QString inputsText() const override;
    bool removesMaterial() const override { return false; }

private:
    std::vector<document::ProfileRef> profiles_;
    document::FeatureId pathSketch_ = 0;
    std::vector<sketch::EntityId> path_;
    QComboBox* orientation_ = nullptr;
    QComboBox* operation_ = nullptr;
};

// Loft: perfiles de uno o varios sketches, en el orden en que se eligen.
class LoftTool final : public FeatureTool {
    Q_OBJECT

public:
    LoftTool(document::Document& document, render::Viewport& viewport, std::optional<document::FeatureId> editing);

    std::unique_ptr<document::Feature> build() const override;

protected:
    QString title() const override { return QStringLiteral("Loft"); }
    void createFields(QFormLayout* form) override;
    void selectionChanged(const render::ModelSelection& selection) override;
    QString inputsText() const override;
    bool removesMaterial() const override { return false; }

private:
    std::vector<document::ProfileRef> profiles_;  // en el orden de elección
    QComboBox* kind_ = nullptr;
    QComboBox* operation_ = nullptr;
};

// Mover / copiar un cuerpo: desplazamiento en X, Y y Z y giro alrededor de un eje.
class MoveTool final : public FeatureTool {
    Q_OBJECT

public:
    MoveTool(document::Document& document, render::Viewport& viewport, std::optional<document::FeatureId> editing);
    ~MoveTool() override;

    std::unique_ptr<document::Feature> build() const override;
    void stopPreview() override;

protected:
    QString title() const override { return QStringLiteral("Mover cuerpo"); }
    void createFields(QFormLayout* form) override;
    void selectionChanged(const render::ModelSelection& selection) override;
    void afterPreview(const document::ModelState& before) override;
    QString inputsText() const override;
    std::optional<Arrow> dimensionArrow(const std::string& key) const override;
    std::optional<Arc> dimensionArc(const std::string& key) const override;
    bool removesMaterial() const override { return false; }

private:
    // El punto por el que giran los ejes: el centro de los cuerpos elegidos (juntos) o el origen.
    std::optional<sketch::Vec3> pivot() const;
    // Los cuerpos elegidos: «body_» y los que se mueven con él.
    std::vector<std::string> chosen() const;
    // El centro de la caja de los cuerpos elegidos juntos.
    std::optional<sketch::Vec3> bodiesCenter() const;
    // Elige estos cuerpos (el primero es el principal) y marca las casillas de la lista.
    void choose(const std::vector<std::string>& keys);
    // Las flechas o los anillos del visor, en el lugar donde queda el cuerpo.
    void refreshGizmo();
    void gizmoPressed(int axis);
    void gizmoDragged(int axis, double amount);
    void gizmoReleased();
    // La sombra con los valores de los cuadros (o de los que se están arrastrando).
    void moveGhost(const std::array<double, 3>& position, const std::array<double, 3>& turns);
    std::array<double, 3> positionValues() const;
    std::array<double, 3> turnValues() const;  // grados

    std::string body_;
    std::vector<std::string> alsoMove_;         // los que se mueven y giran junto con «body_»
    std::vector<document::FeatureId> axisIds_;  // ejes de construcción tras X, Y, Z
    QListWidget* bodyList_ = nullptr;           // casillas: qué cuerpos se mueven
    QDoubleSpinBox* x_ = nullptr;
    QDoubleSpinBox* y_ = nullptr;
    QDoubleSpinBox* z_ = nullptr;
    QDoubleSpinBox* rx_ = nullptr;
    QDoubleSpinBox* ry_ = nullptr;
    QDoubleSpinBox* rz_ = nullptr;
    QComboBox* axis_ = nullptr;
    QComboBox* around_ = nullptr;
    QDoubleSpinBox* angle_ = nullptr;
    QCheckBox* copy_ = nullptr;
    QPushButton* moveMode_ = nullptr;
    QPushButton* turnMode_ = nullptr;
    bool legacyAxis_ = false;  // gira sobre un eje de construcción (lo que se podía hacer antes de los anillos)
    // Arrastre en curso: los valores de los cuadros al agarrar.
    bool dragging_ = false;
    // «Softblock»: la marca a la que está pegado el giro (se queda en ella hasta que el cursor se aleja claramente).
    std::optional<double> heldMark_;
    std::array<double, 3> dragPosition_{};
    std::array<double, 3> dragTurns_{};
};

// Combinar cuerpos: destino, herramientas (casillas) y operación.
class CombineTool final : public FeatureTool {
    Q_OBJECT

public:
    CombineTool(document::Document& document, render::Viewport& viewport, std::optional<document::FeatureId> editing);

    std::unique_ptr<document::Feature> build() const override;

protected:
    QString title() const override { return QStringLiteral("Combinar cuerpos"); }
    void createFields(QFormLayout* form) override;
    void selectionChanged(const render::ModelSelection&) override {}
    QString inputsText() const override;
    bool removesMaterial() const override;

private:
    void fillTools();

    QComboBox* target_ = nullptr;
    QListWidget* tools_ = nullptr;
    QComboBox* operation_ = nullptr;
    QCheckBox* keep_ = nullptr;
    std::vector<std::string> checked_;  // herramientas marcadas (al editar)
};

// Unión: dos caras planas (la fija y la móvil) de dos cuerpos se enfrentan.
class JointTool final : public FeatureTool {
    Q_OBJECT

public:
    JointTool(document::Document& document, render::Viewport& viewport, std::optional<document::FeatureId> editing);

    std::unique_ptr<document::Feature> build() const override;

protected:
    QString title() const override { return QStringLiteral("Unión"); }
    void createFields(QFormLayout* form) override;
    void selectionChanged(const render::ModelSelection& selection) override;
    QString inputsText() const override;
    std::optional<Arrow> dimensionArrow(const std::string& key) const override;
    std::optional<Arc> dimensionArc(const std::string& key) const override;
    bool removesMaterial() const override { return false; }

private:
    void updateVisibility();
    QString faceText(const geometry::ElementRef& ref) const;
    // Centro y normal de la cara fija (donde se miden la distancia, el ángulo y el desplazamiento).
    std::optional<std::pair<sketch::Vec3, sketch::Plane>> fixedFace() const;

    geometry::ElementRef fixed_;
    geometry::ElementRef moving_;
    QComboBox* type_ = nullptr;
    QDoubleSpinBox* offset_ = nullptr;
    QDoubleSpinBox* angle_ = nullptr;
    QDoubleSpinBox* slide_ = nullptr;
    QCheckBox* flip_ = nullptr;
    QFormLayout* form_ = nullptr;
};

// Rosca: varilla roscada métrica (con medidas estándar M3…M24 o propias).
class ThreadTool final : public FeatureTool {
    Q_OBJECT

public:
    ThreadTool(document::Document& document, render::Viewport& viewport, std::optional<document::FeatureId> editing);

    std::unique_ptr<document::Feature> build() const override;

protected:
    QString title() const override { return QStringLiteral("Rosca"); }
    int previewDelayMs() const override { return 250; }
    void createFields(QFormLayout* form) override;
    void selectionChanged(const render::ModelSelection& selection) override;
    QString inputsText() const override;
    std::optional<Arrow> dimensionArrow(const std::string& key) const override;
    bool removesMaterial() const override;

private:
    void adjustDirection();

    QFormLayout* form_ = nullptr;
    Placement placement_;
    QDoubleSpinBox* clearance_ = nullptr;
    QComboBox* standard_ = nullptr;
    QDoubleSpinBox* diameter_ = nullptr;
    QDoubleSpinBox* pitch_ = nullptr;
    QDoubleSpinBox* length_ = nullptr;
    QCheckBox* left_ = nullptr;
    QComboBox* operation_ = nullptr;
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
    std::optional<Arrow> dimensionArrow(const std::string& key) const override;
    std::optional<Arc> dimensionArc(const std::string& key) const override;
    bool removesMaterial() const override { return false; }

private:
    void updateVisibility();

    std::string body_;
    std::vector<document::FeatureId> axisIds_;  // ejes de construcción del desplegable (tras X, Y, Z)
    QFormLayout* form_ = nullptr;
    QComboBox* bodyBox_ = nullptr;
    QComboBox* layout_ = nullptr;
    QComboBox* axis_ = nullptr;
    QDoubleSpinBox* count_ = nullptr;
    QDoubleSpinBox* spacing_ = nullptr;
    QDoubleSpinBox* angle_ = nullptr;
    QComboBox* operation_ = nullptr;
};

} // namespace nuf::app
