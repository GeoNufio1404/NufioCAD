#pragma once

#include <nuf/commands/Command.hpp>
#include <nuf/document/Document.hpp>
#include <nuf/document/Generators.hpp>

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace nuf::commands {

// Rehace lo que sacó un generador con otras respuestas (reabrir su formulario): cambia sus parámetros y sus
// operaciones en el mismo sitio del historial. Si la receta nueva tiene las mismas operaciones, se sustituyen una
// a una (conservan su id, así lo que dependa de ellas sigue funcionando); si no, se borran y se vuelven a crear,
// y eso solo vale si ninguna operación de fuera depende de ellas.
class ReplaceRecipe final : public Command {
public:
    // «lead»: el de la receta que se rehace (GeneratorOrigin::lead de sus operaciones).
    ReplaceRecipe(document::FeatureId lead, document::Recipe recipe, std::string title);

    // Por qué no se puede aplicar (nada que rehacer, o algo de fuera depende de lo que cambia); vacío si se puede.
    static std::string problem(const document::Document& doc, document::FeatureId lead, const document::Recipe& recipe);

    void execute(document::Document& doc) override;
    void undo(document::Document& doc) override;
    std::string label() const override { return "Cambiar " + title_; }

private:
    document::FeatureId lead_;
    document::Recipe recipe_;
    std::string title_;
    bool inPlace_ = false;
    std::vector<document::FeatureId> ids_;  // las de ahora (rehacer las reutiliza)
    std::vector<std::unique_ptr<document::Feature>> oldFeatures_;  // todo el historial de antes
    std::size_t oldMarker_ = 0;
    std::vector<document::Parameter> oldParameters_;
    std::map<std::string, document::BodyInfo> oldInfo_;
    std::shared_ptr<bool> naming_;  // el nombrado que espera al cálculo; deshacer lo anula
};

} // namespace nuf::commands
