#pragma once

#include <nuf/commands/Command.hpp>
#include <nuf/document/Document.hpp>
#include <nuf/document/Generators.hpp>

#include <map>
#include <string>
#include <vector>

namespace nuf::commands {

// Aplica la receta de un generador de una sola vez (parámetros, operaciones y nombres de cuerpos): un solo paso
// para deshacer.
class AddRecipe final : public Command {
public:
    AddRecipe(document::Recipe recipe, std::string title);

    void execute(document::Document& doc) override;
    void undo(document::Document& doc) override;
    std::string label() const override { return "Generar " + title_; }

private:
    document::Recipe recipe_;
    std::string title_;
    std::vector<document::FeatureId> ids_;  // los de las operaciones agregadas (rehacer los reutiliza)
    std::vector<document::Parameter> oldParameters_;
    std::map<std::string, document::BodyInfo> oldInfo_;
};

} // namespace nuf::commands
