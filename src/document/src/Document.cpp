#include <nuf/document/Document.hpp>

#include <nuf/geometry/Shape.hpp>

#include <algorithm>
#include <cctype>
#include <set>
#include <stdexcept>
#include <utility>

namespace nuf::document {

namespace {

std::string kindLabel(FeatureKind kind)
{
    switch (kind) {
    case FeatureKind::Sketch:
        return "Sketch";
    case FeatureKind::Extrude:
        return "Extrusión";
    case FeatureKind::Revolve:
        return "Revolución";
    }
    return "Operación";
}

// Calcula la operación sobre «before».
std::pair<ModelState, FeatureStatus> computeOn(const ModelState& before, const Feature& feature)
{
    ModelState state = before;
    try {
        const auto warning = feature.compute(state);
        return {std::move(state), warning ? FeatureStatus{Health::Warning, *warning} : FeatureStatus{}};
    } catch (const FeatureError& e) {
        return {before, {Health::Error, e.what()}};
    } catch (const geometry::GeometryError& e) {
        return {before, {Health::Error, e.what()}};
    } catch (const std::exception& e) {
        return {before, {Health::Error, std::string("Error inesperado: ") + e.what()}};
    }
}

} // namespace

// --- ModelState y referencias --------------------------------------------

std::vector<geometry::Shape> ModelState::shapes() const
{
    std::vector<geometry::Shape> result;
    for (const Body& body : bodies) {
        result.push_back(body.shape);
    }
    return result;
}

std::optional<ModelState::FaceHit> ModelState::findFace(const geometry::ElementRef& ref) const
{
    if (ref.empty()) {
        return std::nullopt;
    }
    for (std::size_t i = 0; i < bodies.size(); ++i) {
        if (const auto face = bodies[i].shape.findFace(ref)) {
            return FaceHit{i, *face};
        }
    }
    return std::nullopt;
}

std::optional<ModelState::EdgeHit> ModelState::findEdge(const geometry::ElementRef& ref) const
{
    if (ref.empty()) {
        return std::nullopt;
    }
    for (std::size_t i = 0; i < bodies.size(); ++i) {
        if (const auto edge = bodies[i].shape.findEdge(ref)) {
            return EdgeHit{i, *edge};
        }
    }
    return std::nullopt;
}

std::vector<FeatureId> featuresIn(const geometry::ElementRef& ref)
{
    // «f<dígitos>:» al principio o tras «|» o «:».
    std::vector<FeatureId> ids;
    const std::string& s = ref.name;
    for (std::size_t p = 0; p < s.size(); ++p) {
        if (s[p] != 'f' || (p > 0 && s[p - 1] != '|' && s[p - 1] != ':')) {
            continue;
        }
        std::size_t q = p + 1;
        while (q < s.size() && std::isdigit(static_cast<unsigned char>(s[q]))) {
            ++q;
        }
        if (q > p + 1 && (q == s.size() || s[q] == ':')) {
            const FeatureId id = std::stoull(s.substr(p + 1, q - p - 1));
            if (std::ranges::find(ids, id) == ids.end()) {
                ids.push_back(id);
            }
        }
    }
    return ids;
}

// --- Document --------------------------------------------------------------

Document::Document() = default;

void Document::setName(std::string name)
{
    if (name == name_) {
        return;
    }
    name_ = std::move(name);
    markChanged();
}

void Document::markChanged()
{
    ++revision_;
    changed.notify();
}

const Feature* Document::findFeature(FeatureId id) const
{
    const auto index = indexOf(id);
    return index ? features_[*index].get() : nullptr;
}

std::optional<std::size_t> Document::indexOf(FeatureId id) const
{
    for (std::size_t i = 0; i < features_.size(); ++i) {
        if (features_[i]->id() == id) {
            return i;
        }
    }
    return std::nullopt;
}

FeatureId Document::insertFeature(std::unique_ptr<Feature> feature, std::size_t index)
{
    if (!feature) {
        throw std::invalid_argument("Operación nula");
    }
    index = std::min(index, features_.size());
    if (feature->id() == 0) {
        feature->setId(nextId_++);
    } else {
        if (findFeature(feature->id())) {
            throw std::invalid_argument("Ya existe una operación con ese id");
        }
        nextId_ = std::max(nextId_, feature->id() + 1);
    }
    if (feature->name().empty()) {
        feature->setName(defaultName(feature->kind()));
    }
    const FeatureId id = feature->id();
    features_.insert(features_.begin() + std::ptrdiff_t(index), std::move(feature));
    states_.insert(states_.begin() + std::ptrdiff_t(index), ModelState{});
    statuses_.insert(statuses_.begin() + std::ptrdiff_t(index), FeatureStatus{});
    if (index <= marker_) {
        ++marker_;
    }
    recomputeFrom(index);
    return id;
}

FeatureId Document::addFeature(std::unique_ptr<Feature> feature)
{
    return insertFeature(std::move(feature), marker_);
}

std::unique_ptr<Feature> Document::removeFeature(FeatureId id)
{
    const auto index = indexOf(id);
    if (!index) {
        return nullptr;
    }
    std::unique_ptr<Feature> removed = std::move(features_[*index]);
    features_.erase(features_.begin() + std::ptrdiff_t(*index));
    states_.erase(states_.begin() + std::ptrdiff_t(*index));
    statuses_.erase(statuses_.begin() + std::ptrdiff_t(*index));
    if (*index < marker_) {
        --marker_;
    }
    recomputeFrom(*index);
    return removed;
}

std::unique_ptr<Feature> Document::replaceFeature(FeatureId id, std::unique_ptr<Feature> feature)
{
    const auto index = indexOf(id);
    if (!index || !feature) {
        return nullptr;
    }
    feature->setId(id);
    if (feature->name().empty()) {
        feature->setName(features_[*index]->name());
    }
    std::unique_ptr<Feature> old = std::exchange(features_[*index], std::move(feature));
    recomputeFrom(*index);
    return old;
}

void Document::setSuppressed(FeatureId id, bool suppressed)
{
    const auto index = indexOf(id);
    if (!index || features_[*index]->suppressed() == suppressed) {
        return;
    }
    features_[*index]->setSuppressed(suppressed);
    recomputeFrom(*index);
}

bool Document::canMoveFeature(FeatureId id, std::size_t index) const
{
    const auto from = indexOf(id);
    if (!from) {
        return false;
    }
    std::vector<const Feature*> order;
    for (const auto& f : features_) {
        if (f->id() != id) {
            order.push_back(f.get());
        }
    }
    order.insert(order.begin() + std::ptrdiff_t(std::min(index, order.size())), features_[*from].get());
    std::set<FeatureId> seen;
    for (const Feature* f : order) {
        for (FeatureId dep : f->dependencies()) {
            if (findFeature(dep) && !seen.contains(dep)) {
                return false;
            }
        }
        seen.insert(f->id());
    }
    return true;
}

void Document::moveFeature(FeatureId id, std::size_t index)
{
    const auto from = indexOf(id);
    if (!from) {
        return;
    }
    std::unique_ptr<Feature> moving = std::move(features_[*from]);
    features_.erase(features_.begin() + std::ptrdiff_t(*from));
    index = std::min(index, features_.size());
    features_.insert(features_.begin() + std::ptrdiff_t(index), std::move(moving));
    recomputeFrom(std::min(*from, index));
}

std::vector<FeatureId> Document::dependents(FeatureId id) const
{
    std::set<FeatureId> affected{id};
    std::vector<FeatureId> result;
    for (const auto& f : features_) {
        for (FeatureId dep : f->dependencies()) {
            if (affected.contains(dep) && f->id() != id) {
                affected.insert(f->id());
                result.push_back(f->id());
                break;
            }
        }
    }
    return result;
}

void Document::setMarker(std::size_t marker)
{
    marker = std::min(marker, features_.size());
    if (marker == marker_) {
        return;
    }
    const std::size_t from = std::min(marker, marker_);
    marker_ = marker;
    recomputeFrom(from);
}

std::string Document::defaultName(FeatureKind kind) const
{
    const std::string label = kindLabel(kind);
    int highest = 0;
    for (const auto& f : features_) {
        const std::string& name = f->name();
        if (f->kind() != kind || !name.starts_with(label)) {
            continue;
        }
        const std::string digits = name.substr(label.size());
        if (!digits.empty() && std::ranges::all_of(digits, [](char c) { return std::isdigit(static_cast<unsigned char>(c)); })) {
            highest = std::max(highest, std::stoi(digits));
        }
    }
    return label + std::to_string(highest + 1);
}

void Document::restore(std::string name, std::vector<std::unique_ptr<Feature>> features, std::size_t marker,
                       FeatureId nextId)
{
    FeatureId highest = 0;
    for (std::size_t i = 0; i < features.size(); ++i) {
        if (!features[i] || features[i]->id() == 0) {
            throw std::invalid_argument("Operación sin id");
        }
        for (std::size_t j = 0; j < i; ++j) {
            if (features[j]->id() == features[i]->id()) {
                throw std::invalid_argument("Hay dos operaciones con el mismo id");
            }
        }
        highest = std::max(highest, features[i]->id());
    }
    name_ = std::move(name);
    features_ = std::move(features);
    states_.assign(features_.size(), ModelState{});
    statuses_.assign(features_.size(), FeatureStatus{});
    marker_ = std::min(marker, features_.size());
    nextId_ = std::max(nextId, highest + 1);
    recomputeFrom(0);
}

const ModelState& Document::result() const
{
    return marker_ == 0 ? empty_ : states_[marker_ - 1];
}

const ModelState& Document::stateBefore(std::size_t index) const
{
    index = std::min(index, states_.size());
    return index == 0 ? empty_ : states_[index - 1];
}

const FeatureStatus& Document::status(FeatureId id) const
{
    static const FeatureStatus kUnknown{Health::Error, "No existe la operación"};
    const auto index = indexOf(id);
    return index ? statuses_[*index] : kUnknown;
}

void Document::recomputeFrom(std::size_t index)
{
    for (std::size_t i = index; i < features_.size(); ++i) {
        const ModelState& before = i == 0 ? empty_ : states_[i - 1];
        const Feature& feature = *features_[i];
        if (i >= marker_) {
            states_[i] = before;
            statuses_[i] = {Health::RolledBack, {}};
        } else if (feature.suppressed()) {
            states_[i] = before;
            statuses_[i] = {Health::Suppressed, {}};
        } else {
            auto [state, status] = computeOn(before, feature);
            states_[i] = std::move(state);
            statuses_[i] = std::move(status);
        }
    }
    markChanged();
}

Preview preview(const Document& document, const Feature& feature, std::size_t index)
{
    auto [state, status] = computeOn(document.stateBefore(index), feature);
    return {std::move(state), std::move(status)};
}

} // namespace nuf::document
