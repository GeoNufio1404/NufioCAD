#pragma once

#include <nuf/commands/Command.hpp>
#include <nuf/commands/FeatureCommands.hpp>
#include <nuf/document/Document.hpp>
#include <nuf/sketch/Plane.hpp>

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace nuf::commands {

// Duplicar (Fase 21, A1): copia uno o varios cuerpos de una vez —o un componente entero— desplazados «offset». Las
// copias llevan el nombre del original con el siguiente número («Caja 1» → «Caja 2»; «Tapa» → «Tapa 2»); si el
// original está en un componente, las copias van a uno nuevo con su número («Par de engranajes 1» → «… 2»). Una
// copia nunca queda fija. Es una sola operación del historial (un Mover con copia), así que sigue a los originales.
class DuplicateBodies final : public Command {
public:
    DuplicateBodies(std::vector<std::string> keys, sketch::Vec3 offset);

    void execute(document::Document& doc) override;
    void undo(document::Document& doc) override;
    std::string label() const override { return "Duplicar"; }

    // Las claves de los cuerpos nuevos (tras execute), en el orden de los originales.
    const std::vector<std::string>& newKeys() const { return newKeys_; }

private:
    std::vector<std::string> keys_;
    sketch::Vec3 offset_;
    std::unique_ptr<AddFeature> add_;
    std::optional<std::map<std::string, document::BodyInfo>> oldInfo_;
    std::vector<std::string> newKeys_;
};

} // namespace nuf::commands
