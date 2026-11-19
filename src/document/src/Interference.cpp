#include <nuf/document/Interference.hpp>

#include <nuf/document/Features.hpp>
#include <nuf/geometry/Modeling.hpp>

#include <algorithm>
#include <map>

namespace nuf::document {

std::vector<InterferencePair> findInterferences(const ModelState& state, double minVolume, const std::set<std::string>& hidden)
{
    std::vector<InterferencePair> pairs;
    for (std::size_t i = 0; i < state.bodies.size(); ++i) {
        if (hidden.contains(state.bodies[i].key)) {
            continue;
        }
        for (std::size_t j = i + 1; j < state.bodies.size(); ++j) {
            if (hidden.contains(state.bodies[j].key)) {
                continue;
            }
            if (const auto common = geometry::overlap(state.bodies[i].shape, state.bodies[j].shape, minVolume)) {
                pairs.push_back({state.bodies[i].key, state.bodies[j].key, state.bodies[i].name, state.bodies[j].name,
                                 common->volume, 0.0, common->shape});
            }
        }
    }
    std::ranges::sort(pairs, [](const InterferencePair& a, const InterferencePair& b) { return a.volume > b.volume; });
    return pairs;
}

std::vector<InterferencePair> findInterferencesAlong(const Document& document, FeatureId link, double span, int steps,
                                                     int* checked, double minVolume, const std::function<bool()>& cancelled)
{
    const auto* feature = dynamic_cast<const MotionLinkFeature*>(document.findFeature(link));
    const auto index = document.indexOf(link);
    if (!feature || !index) {
        return {};
    }
    steps = std::max(steps, 1);
    // El peor momento de cada pareja.
    std::map<std::pair<std::string, std::string>, InterferencePair> worst;
    int looked = 0;
    for (int k = 0; k <= steps; ++k) {
        if (cancelled && cancelled()) {
            break;
        }
        MotionLinkFeature moved = *feature;
        moved.position = span * double(k) / double(steps);
        const Preview preview = document::preview(document, moved, *index);
        if (preview.status.health == Health::Error) {
            continue;
        }
        ++looked;
        for (InterferencePair& pair : findInterferences(preview.state, minVolume)) {
            pair.at = moved.position;
            auto [it, inserted] = worst.try_emplace({pair.first, pair.second}, pair);
            if (!inserted && pair.volume > it->second.volume) {
                it->second = std::move(pair);
            }
        }
    }
    if (checked) {
        *checked = looked;
    }
    std::vector<InterferencePair> out;
    for (auto& [key, pair] : worst) {
        out.push_back(std::move(pair));
    }
    std::ranges::sort(out, [](const InterferencePair& a, const InterferencePair& b) { return a.volume > b.volume; });
    return out;
}

} // namespace nuf::document
