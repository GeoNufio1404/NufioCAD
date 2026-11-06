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
    for (const auto& feature : recipe_.features) {
        const document::FeatureId id = doc.insertFeature(document::instantiate(*feature, recipeSketch), doc.marker());
        if (feature->kind() == document::FeatureKind::Sketch) {
            recipeSketch = id;
        }
        // Rehacer usa los mismos ids y nombres.
        feature->setId(id);
        feature->setName(doc.findFeature(id)->name());
        ids_.push_back(id);
    }

    std::map<std::string, document::BodyInfo> info = doc.bodyInfo();
    std::size_t next = 0;
    for (const document::Body& body : doc.result().bodies) {
        if (std::ranges::find(before, body.key) != before.end()) {
            continue;
        }
        info[body.key] = {next < recipe_.bodyNames.size() ? recipe_.bodyNames[next] : std::string(), recipe_.component};
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
