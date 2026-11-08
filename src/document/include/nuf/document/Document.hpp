#pragma once

#include <nuf/document/Expression.hpp>
#include <nuf/document/Feature.hpp>
#include <nuf/document/Signal.hpp>

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace nuf::document {

// Parámetro con nombre («grosor = 3 mm»): su expresión puede usar otros.
struct Parameter {
    std::string name;
    std::string expression;
    bool operator==(const Parameter&) const = default;
};

// Lo que el usuario le puso a un cuerpo: un nombre propio y el componente al que
// pertenece (vacío = ninguno). Se guarda por la clave del cuerpo («f3»).
struct BodyInfo {
    std::string name;
    std::string component;
    bool operator==(const BodyInfo&) const = default;
};

// Valor evaluado de un parámetro, o el motivo por el que no se pudo.
struct ParameterValue {
    std::optional<double> value;
    std::string error;
};

// Documento de diseño: nombre e historial de operaciones (timeline) con su
// marcador. Lo que queda después del marcador no se calcula. Cada cambio
// recalcula desde la operación afectada, reutilizando el estado guardado de
// la anterior. Los cambios deben llegar por comandos (deshacer/rehacer).
class Document {
public:
    Document();

    const std::string& name() const noexcept { return name_; }
    void setName(std::string name);

    // Aumenta con cada cambio; útil para saber si hay cambios sin guardar.
    std::uint64_t revision() const noexcept { return revision_; }

    // --- Historial ---
    std::size_t featureCount() const noexcept { return features_.size(); }
    const Feature& feature(std::size_t index) const { return *features_.at(index); }
    const Feature* findFeature(FeatureId id) const;
    std::optional<std::size_t> indexOf(FeatureId id) const;

    // Inserta en «index» (asigna id y nombre si no tiene). Si entra antes
    // del marcador, o justo en él, el marcador avanza con ella.
    FeatureId insertFeature(std::unique_ptr<Feature> feature, std::size_t index);
    // Al final del historial activo (en el marcador).
    FeatureId addFeature(std::unique_ptr<Feature> feature);
    std::unique_ptr<Feature> removeFeature(FeatureId id);
    // Sustituye la operación (misma id); devuelve la anterior.
    std::unique_ptr<Feature> replaceFeature(FeatureId id, std::unique_ptr<Feature> feature);
    void setSuppressed(FeatureId id, bool suppressed);
    // Mueve la operación a la posición «index» (contada sin ella).
    void moveFeature(FeatureId id, std::size_t index);
    // true si al moverla sigue después de lo que necesita y antes de lo que
    // la necesita.
    bool canMoveFeature(FeatureId id, std::size_t index) const;
    // Operaciones que dependen (directa o indirectamente) de esta.
    std::vector<FeatureId> dependents(FeatureId id) const;

    // Cuántas operaciones están activas (antes del marcador).
    std::size_t marker() const noexcept { return marker_; }
    void setMarker(std::size_t marker);

    // Nombre para una operación nueva: «Sketch3», «Extrusión2»…
    std::string defaultName(FeatureKind kind) const;
    // Reserva un id (para una vista previa que luego se agrega).
    FeatureId reserveId() { return nextId_++; }

    // Sustituye todo el contenido (al abrir un archivo) con un solo
    // recálculo y sin cambiar el objeto, para que lo conectado a él siga
    // funcionando. Lanza std::invalid_argument si hay ids repetidos o nulos.
    void restore(std::string name, std::vector<std::unique_ptr<Feature>> features, std::size_t marker,
                 FeatureId nextId, std::vector<Parameter> parameters = {});
    // Siguiente id libre (se guarda en el archivo).
    FeatureId nextId() const noexcept { return nextId_; }

    // --- Parámetros ---
    const std::vector<Parameter>& parameters() const noexcept { return parameters_; }
    // Cambia la tabla y recalcula lo que usa expresiones.
    void setParameters(std::vector<Parameter> parameters);
    // Expresiones de cada operación (para poder deshacer un renombrado).
    using ExpressionSnapshot = std::map<FeatureId, std::map<std::string, std::string>>;
    ExpressionSnapshot expressionSnapshot() const;
    // Como setParameters, pero antes cambia el nombre de los parámetros según
    // «renames» (antiguo → nuevo) en las expresiones de las operaciones, para que
    // lo ligado no se rompa al renombrar. Las expresiones de «parameters» ya deben
    // venir con los nombres nuevos (las reescribe quien llama, como el diálogo).
    void setParameters(std::vector<Parameter> parameters, const std::map<std::string, std::string>& renames);
    // Devuelve tabla y expresiones de las operaciones a un estado anterior.
    void restoreParameters(std::vector<Parameter> parameters, const ExpressionSnapshot& snapshot);
    // El valor (o el error) de un parámetro de la tabla.
    ParameterValue parameterValue(const std::string& name) const;
    // Para evaluar una expresión con los parámetros del documento.
    ParameterLookup lookup() const;

    // --- Nombres y componentes de los cuerpos ---
    const std::map<std::string, BodyInfo>& bodyInfo() const noexcept { return bodyInfo_; }
    // Los nombres propios sustituyen a «Cuerpo1»… en todos los estados (sin recalcular).
    void setBodyInfo(std::map<std::string, BodyInfo> info);
    // Componente de un cuerpo ("" si no tiene).
    std::string componentOf(const std::string& bodyKey) const;

    // --- Resultado ---
    // El modelo tras la última operación activa.
    const ModelState& result() const;
    // El modelo justo antes de la operación en «index».
    const ModelState& stateBefore(std::size_t index) const;
    const FeatureStatus& status(FeatureId id) const;

    // Tras cualquier cambio (nombre, historial, recálculo).
    Signal<> changed;

private:
    void markChanged();
    void recomputeFrom(std::size_t index);
    void evaluateParameters();
    void applyBodyInfo();

    std::string name_ = "Sin título";
    std::uint64_t revision_ = 0;
    std::vector<std::unique_ptr<Feature>> features_;
    std::vector<ModelState> states_;  // states_[i]: después de la operación i
    std::vector<FeatureStatus> statuses_;
    std::size_t marker_ = 0;
    FeatureId nextId_ = 1;
    std::vector<Parameter> parameters_;
    std::map<std::string, ParameterValue> values_;
    std::map<std::string, BodyInfo> bodyInfo_;
    ModelState empty_;
};

// Calcula «feature» como si estuviera en «index» (sin tocar el documento):
// para la vista previa de una herramienta.
struct Preview {
    ModelState state;
    FeatureStatus status;
};
Preview preview(const Document& document, const Feature& feature, std::size_t index);

} // namespace nuf::document
