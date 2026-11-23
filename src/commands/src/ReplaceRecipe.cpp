#include <nuf/commands/ReplaceRecipe.hpp>

#include <algorithm>
#include <cctype>
#include <iterator>

namespace nuf::commands {

namespace {

// ¿Las operaciones de la receta son de los mismos tipos y en el mismo orden que las del grupo?
bool sameShape(const document::Document& doc, const std::vector<document::FeatureId>& group, const document::Recipe& recipe)
{
    if (group.size() != recipe.features.size()) {
        return false;
    }
    for (std::size_t i = 0; i < group.size(); ++i) {
        if (doc.findFeature(group[i])->kind() != recipe.features[i]->kind()) {
            return false;
        }
    }
    return true;
}

bool hasTag(const std::string& key, const document::Feature& f)
{
    const std::string tag = f.tag();
    return key.rfind(tag, 0) == 0 && (key.size() == tag.size() || !std::isdigit(static_cast<unsigned char>(key[tag.size()])));
}

} // namespace

ReplaceRecipe::ReplaceRecipe(document::FeatureId lead, document::Recipe recipe, std::string title)
    : lead_(lead)
    , recipe_(std::move(recipe))
    , title_(std::move(title))
{
}

std::string ReplaceRecipe::problem(const document::Document& doc, document::FeatureId lead, const document::Recipe& recipe)
{
    const std::vector<document::FeatureId> group = document::generatedGroup(doc, lead);
    if (group.empty()) {
        return "Ya no queda nada de lo que sacó el generador";
    }
    if (sameShape(doc, group, recipe)) {
        return {};
    }
    for (const document::FeatureId id : group) {
        for (const document::FeatureId user : doc.dependents(id)) {
            if (std::ranges::find(group, user) == group.end()) {
                return "Con esas respuestas el generador necesita otras operaciones, y «" + doc.findFeature(user)->name() +
                       "» depende de las actuales. Borra o mueve lo que usa el generador, o elige respuestas que no cambien su estructura.";
            }
        }
    }
    return {};
}

void ReplaceRecipe::execute(document::Document& doc)
{
    const std::vector<document::FeatureId> group = document::generatedGroup(doc, lead_);
    if (group.empty()) {
        return;
    }
    oldParameters_ = doc.parameters();
    oldInfo_ = doc.bodyInfo();
    oldMarker_ = doc.marker();
    oldFeatures_.clear();
    for (std::size_t i = 0; i < doc.featureCount(); ++i) {
        oldFeatures_.push_back(doc.feature(i).clone());
    }
    const document::GeneratorOrigin origin = doc.findFeature(group.front())->origin;

    // Parámetros: los de antes que la receta nueva ya no usa se van, los demás cambian de valor en su sitio y los
    // nuevos van al final.
    std::vector<std::string> oldNames;
    if (const document::Generator* generator = document::findGenerator(origin.generator)) {
        try {
            for (const document::Parameter& p : generator->make(document::decodeValues(origin.values), origin.suffix).parameters) {
                oldNames.push_back(p.name);
            }
        } catch (const std::exception&) {
        }
    }
    std::vector<document::Parameter> parameters;
    for (const document::Parameter& p : oldParameters_) {
        const auto same = std::ranges::find_if(recipe_.parameters, [&](const document::Parameter& n) { return n.name == p.name; });
        if (same != recipe_.parameters.end()) {
            parameters.push_back(*same);
        } else if (std::ranges::find(oldNames, p.name) == oldNames.end()) {
            parameters.push_back(p);
        }
    }
    for (const document::Parameter& n : recipe_.parameters) {
        if (std::ranges::none_of(parameters, [&](const document::Parameter& p) { return p.name == n.name; })) {
            parameters.push_back(n);
        }
    }

    const auto originFor = [&](std::size_t k, document::FeatureId lead) {
        document::GeneratorOrigin o = recipe_.origin;
        o.lead = lead;
        if (k != 0) {
            o.values.clear();
        }
        return o;
    };

    // Las operaciones nuevas, ya colocadas; todo se cambia de una vez (un solo recálculo del historial).
    std::vector<std::unique_ptr<document::Feature>> features;
    inPlace_ = sameShape(doc, group, recipe_);
    // El componente que tenían los cuerpos del grupo de antes: el rehecho lo conserva (con su número).
    std::string keptComponent;
    for (const document::Body& body : doc.result().bodies) {
        for (const document::FeatureId id : group) {
            const document::Feature* old = doc.findFeature(id);
            if (old != nullptr && hasTag(body.key, *old) && !doc.componentOf(body.key).empty()) {
                keptComponent = doc.componentOf(body.key);
            }
        }
    }
    document::FeatureId recipeSketch = 0;
    std::size_t k = 0;
    bool placed = false;
    const auto inGroup = [&](document::FeatureId id) { return std::ranges::find(group, id) != group.end(); };
    ids_.clear();
    if (inPlace_) {
        for (std::size_t i = 0; i < doc.featureCount(); ++i) {
            const document::Feature& current = doc.feature(i);
            if (!inGroup(current.id())) {
                features.push_back(current.clone());
                continue;
            }
            auto next = document::instantiate(*recipe_.features[k], recipeSketch, ids_);
            next->setId(current.id());
            next->setName(current.name());
            next->setSuppressed(current.suppressed());
            next->origin = originFor(k, group.front());
            if (recipe_.features[k]->kind() == document::FeatureKind::Sketch) {
                recipeSketch = current.id();
            }
            ids_.push_back(current.id());
            features.push_back(std::move(next));
            ++k;
        }
    } else {
        // Rehacer usa los mismos ids.
        const document::FeatureId lead = !recipe_.features.empty() && recipe_.features.front()->id() != 0
                                             ? recipe_.features.front()->id()
                                             : doc.reserveId();
        document::FeatureId nextId = doc.nextId();
        for (std::size_t i = 0; i < doc.featureCount(); ++i) {
            const document::Feature& current = doc.feature(i);
            if (inGroup(current.id())) {
                if (placed) {
                    continue;
                }
                placed = true;  // el grupo nuevo ocupa el sitio de la primera operación del anterior
                for (std::size_t j = 0; j < recipe_.features.size(); ++j) {
                    document::Feature& source = *recipe_.features[j];
                    const document::FeatureId id = j == 0 ? lead : source.id() != 0 ? source.id() : nextId++;
                    source.setId(id);
                    auto next = document::instantiate(source, recipeSketch, ids_);
                    next->origin = originFor(j, lead);
                    if (next->name().empty()) {
                        next->setName(source.name().empty() ? doc.defaultName(source.kind()) + "_" + std::to_string(id) : source.name());
                    }
                    source.setName(next->name());
                    if (source.kind() == document::FeatureKind::Sketch) {
                        recipeSketch = id;
                    }
                    ids_.push_back(id);
                    features.push_back(std::move(next));
                }
                continue;
            }
            features.push_back(current.clone());
        }
    }
    const std::size_t marker = oldMarker_ >= oldFeatures_.size() ? features.size() : std::min(oldMarker_, features.size());
    doc.restore(doc.name(), std::move(features), marker, std::max(doc.nextId(), ids_.empty() ? 0 : *std::ranges::max_element(ids_) + 1),
                parameters);

    // Los cuerpos nuevos de la receta (sin nombre propio todavía) reciben los nombres y el componente de la receta.
    // (Con el cálculo en segundo plano hay que esperarlo: si no, el resultado es el de antes y no se los encuentra.)
    doc.waitForCompute();
    std::map<std::string, document::BodyInfo> info = doc.bodyInfo();
    // El componente que ya tienen los cuerpos de la receta (con su número); si no tienen, uno nuevo numerado.
    std::string component;
    for (const document::Body& body : doc.result().bodies) {
        const bool ours = std::ranges::any_of(ids_, [&](document::FeatureId id) { return hasTag(body.key, *doc.findFeature(id)); });
        if (ours && info.contains(body.key) && !info.at(body.key).component.empty()) {
            component = info.at(body.key).component;
            break;
        }
    }
    if (component.empty()) {
        component = !keptComponent.empty() ? keptComponent : document::numberedComponent(info, recipe_.component);
    }
    std::size_t next = 0;
    for (const document::Body& body : doc.result().bodies) {
        const bool ours = std::ranges::any_of(ids_, [&](document::FeatureId id) { return hasTag(body.key, *doc.findFeature(id)); });
        if (!ours) {
            continue;
        }
        if (!info.contains(body.key)) {
            info[body.key] = {next < recipe_.bodyNames.size() ? recipe_.bodyNames[next] : std::string(), component, std::string(),
                              std::ranges::find(recipe_.fixedBodies, int(next)) != recipe_.fixedBodies.end()};
        }
        ++next;
    }
    doc.setBodyInfo(info);
}

void ReplaceRecipe::undo(document::Document& doc)
{
    std::vector<std::unique_ptr<document::Feature>> features;
    for (const auto& f : oldFeatures_) {
        features.push_back(f->clone());
    }
    doc.restore(doc.name(), std::move(features), oldMarker_, doc.nextId(), oldParameters_);
    doc.setBodyInfo(oldInfo_);
}

} // namespace nuf::commands
