#include <nuf/commands/DuplicateBodies.hpp>

#include <nuf/document/Features.hpp>
#include <nuf/document/Generators.hpp>

#include <algorithm>
#include <cctype>
#include <set>
#include <stdexcept>

namespace nuf::commands {

namespace {

// «Caja 3» → {«Caja», 3}; «Tapa» → {«Tapa», 1}.
std::pair<std::string, int> splitNumber(const std::string& name)
{
    std::size_t end = name.size();
    std::size_t digits = end;
    while (digits > 0 && std::isdigit(static_cast<unsigned char>(name[digits - 1]))) {
        --digits;
    }
    if (digits == end || digits == 0 || name[digits - 1] != ' ') {
        return {name, 1};
    }
    std::string base = name.substr(0, digits - 1);
    return {base, std::stoi(name.substr(digits))};
}

// El primer nombre «base n» (con n > el del original) que no usa ningún cuerpo.
std::string nextName(const std::string& original, const std::set<std::string>& used)
{
    const auto [base, start] = splitNumber(original);
    for (int n = start + 1; n < 100000; ++n) {
        const std::string name = base + " " + std::to_string(n);
        if (!used.contains(name)) {
            return name;
        }
    }
    return original + " (copia)";
}

} // namespace

DuplicateBodies::DuplicateBodies(std::vector<std::string> keys, sketch::Vec3 offset)
    : keys_(std::move(keys))
    , offset_(offset)
{
    if (keys_.empty()) {
        throw std::invalid_argument("Duplicar necesita al menos un cuerpo");
    }
}

void DuplicateBodies::execute(document::Document& doc)
{
    oldInfo_ = doc.bodyInfo();
    std::set<std::string> usedNames;
    for (const document::Body& body : doc.result().bodies) {
        usedNames.insert(body.name);
    }
    std::map<std::string, std::string> originalName;
    for (const document::Body& body : doc.result().bodies) {
        originalName[body.key] = body.name;
    }
    if (!add_) {
        auto move = std::make_unique<document::MoveFeature>();
        move->body = keys_.front();
        move->alsoMove.assign(keys_.begin() + 1, keys_.end());
        move->translation = offset_;
        move->copy = true;
        add_ = std::make_unique<AddFeature>(std::move(move));
    }
    add_->execute(doc);

    // Las claves de los cuerpos nuevos no dependen del cálculo (que puede ir todavía en segundo plano): la primera es
    // la de la operación y las demás llevan «:c1», «:c2»… (ver MoveFeature::compute), en el orden de los originales.
    const std::string tag = "f" + std::to_string(add_->id());
    std::vector<std::string> fresh;
    for (std::size_t i = 0; i < keys_.size(); ++i) {
        fresh.push_back(i == 0 ? tag : tag + ":c" + std::to_string(i));
    }
    newKeys_ = fresh;
    std::map<std::string, document::BodyInfo> info = doc.bodyInfo();
    std::map<std::string, std::string> newComponent;  // componente original → el de las copias
    for (std::size_t i = 0; i < keys_.size(); ++i) {
        document::BodyInfo entry;
        const std::string& original = originalName[keys_[i]];
        const std::string name = nextName(original.empty() ? std::string("Cuerpo") : original, usedNames);
        usedNames.insert(name);
        entry.name = name;
        const std::string component = doc.componentOf(keys_[i]);
        if (!component.empty()) {
            auto [it, inserted] = newComponent.try_emplace(component);
            if (inserted) {
                // «Par de engranajes 1» → base «Par de engranajes» → el siguiente número libre.
                it->second = document::numberedComponent(info, splitNumber(component).first);
                // numberedComponent mira solo lo que hay: marcamos el nombre como usado para el siguiente grupo.
                info[fresh[i]].component = it->second;
            }
            entry.component = it->second;
        }
        info[fresh[i]] = entry;
    }
    doc.setBodyInfo(info);
}

void DuplicateBodies::undo(document::Document& doc)
{
    add_->undo(doc);
    if (oldInfo_) {
        doc.setBodyInfo(*oldInfo_);
    }
}

} // namespace nuf::commands
