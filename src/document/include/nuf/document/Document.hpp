#pragma once

#include <nuf/document/Signal.hpp>

#include <cstdint>
#include <string>

namespace nuf::document {

// Identificador estable de una operación del historial (Fase 3).
using FeatureId = std::uint64_t;

// Documento de diseño. Fase 0: solo nombre, revisión y notificación de
// cambios. En la Fase 3 se agregan Timeline, Feature y el recálculo.
class Document {
public:
    const std::string& name() const noexcept { return name_; }
    void setName(std::string name);

    // Aumenta con cada cambio; útil para saber si hay cambios sin guardar.
    std::uint64_t revision() const noexcept { return revision_; }

    Signal<> changed;

private:
    void markChanged();

    std::string name_ = "Sin título";
    std::uint64_t revision_ = 0;
};

} // namespace nuf::document
