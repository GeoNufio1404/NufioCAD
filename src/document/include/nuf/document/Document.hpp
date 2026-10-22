#pragma once

#include <nuf/document/Feature.hpp>
#include <nuf/document/Signal.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace nuf::document {

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

    std::string name_ = "Sin título";
    std::uint64_t revision_ = 0;
    std::vector<std::unique_ptr<Feature>> features_;
    std::vector<ModelState> states_;  // states_[i]: después de la operación i
    std::vector<FeatureStatus> statuses_;
    std::size_t marker_ = 0;
    FeatureId nextId_ = 1;
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
