#include <nuf/commands/AddRecipe.hpp>

#include <algorithm>

namespace nuf::commands {

AddRecipe::AddRecipe(document::Recipe recipe, std::string title)
    : recipe_(std::move(recipe))
    , title_(std::move(title))
{
}

void AddRecipe::execute(document::Document& doc)
{
    oldParameters_ = doc.parameters();
    oldInfo_ = doc.bodyInfo();
    std::vector<document::Parameter> parameters = oldParameters_;
    for (const document::Parameter& p : recipe_.parameters) {
        parameters.push_back(p);
    }
    doc.setParameters(parameters);

    std::vector<std::string> before;
    for (const document::Body& body : doc.result().bodies) {
        before.push_back(body.key);
    }
    ids_.clear();
    document::FeatureId recipeSketch = 0;
    // El id de la primera operación identifica el resultado entero (rehacer reutiliza el que ya tenía).
    document::FeatureId lead = 0;
    if (!recipe_.features.empty() && !recipe_.origin.generator.empty()) {
        lead = recipe_.features.front()->id() != 0 ? recipe_.features.front()->id() : doc.reserveId();
        recipe_.features.front()->setId(lead);
    }
    std::size_t k = 0;
    for (const auto& feature : recipe_.features) {
        auto instance = document::instantiate(*feature, recipeSketch, ids_);
        if (lead != 0) {
            instance->origin = recipe_.origin;
            instance->origin.lead = lead;
            if (k != 0) {
                instance->origin.values.clear();
            }
        }
        ++k;
        const document::FeatureId id = doc.insertFeature(std::move(instance), doc.marker());
        if (feature->kind() == document::FeatureKind::Sketch) {
            recipeSketch = id;
        }
        // Rehacer usa los mismos ids y nombres.
        feature->setId(id);
        feature->setName(doc.findFeature(id)->name());
        ids_.push_back(id);
    }

    // Con el cálculo en segundo plano el resultado todavía es el de antes: sin esperarlo, los cuerpos nuevos no se
    // encuentran y se quedan sin el nombre ni el componente de la receta.
    doc.waitForCompute();
    std::map<std::string, document::BodyInfo> info = doc.bodyInfo();
    const std::string component = document::numberedComponent(info, recipe_.component);
    std::size_t next = 0;
    for (const document::Body& body : doc.result().bodies) {
        if (std::ranges::find(before, body.key) != before.end()) {
            continue;
        }
        info[body.key] = {next < recipe_.bodyNames.size() ? recipe_.bodyNames[next] : std::string(), component, std::string(),
                          std::ranges::find(recipe_.fixedBodies, int(next)) != recipe_.fixedBodies.end()};
        ++next;
    }
    doc.setBodyInfo(info);
}

void AddRecipe::undo(document::Document& doc)
{
    for (auto it = ids_.rbegin(); it != ids_.rend(); ++it) {
        doc.removeFeature(*it);
    }
    doc.setParameters(oldParameters_);
    doc.setBodyInfo(oldInfo_);
}

} // namespace nuf::commands
