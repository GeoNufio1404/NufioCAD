#pragma once

// Ayudas que comparten los archivos de las operaciones (Features*.cpp).

#include <nuf/document/Features.hpp>

#include <nuf/document/Expression.hpp>

#include <nuf/geometry/Exchange.hpp>
#include <nuf/sketch/Solver.hpp>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <numbers>
#include <map>
#include <set>

namespace nuf::document {

inline void addUnique(std::vector<FeatureId>& ids, FeatureId id)
{
    if (id != 0 && std::ranges::find(ids, id) == ids.end()) {
        ids.push_back(id);
    }
}

inline void addAll(std::vector<FeatureId>& ids, const geometry::ElementRef& ref)
{
    for (FeatureId id : featuresIn(ref)) {
        addUnique(ids, id);
    }
}

inline std::optional<std::string> joinWarnings(const std::vector<std::string>& warnings)
{
    if (warnings.empty()) {
        return std::nullopt;
    }
    std::string joined;
    for (const std::string& w : warnings) {
        joined += (joined.empty() ? "" : " ") + w;
    }
    return joined;
}

// Los perfiles elegidos, agrupados por sketch y ya en su plano.
inline std::vector<geometry::SketchRegion> regionsOf(const ModelState& state, const std::vector<ProfileRef>& refs,
                                              std::vector<std::string>& warnings)
{
    std::map<FeatureId, geometry::SketchRegion> bySketch;
    std::map<FeatureId, std::vector<sketch::Profile>> found;
    int lost = 0;
    for (const ProfileRef& ref : refs) {
        const auto out = state.sketches.find(ref.sketch);
        if (out == state.sketches.end()) {
            throw FeatureError("Falta el sketch de los perfiles");
        }
        auto [it, fresh] = found.try_emplace(ref.sketch);
        if (fresh) {
            it->second = sketch::findProfiles(*out->second.sketch);
        }
        const auto profile = resolveProfile(it->second, ref);
        if (!profile) {
            ++lost;
            continue;
        }
        auto& region = bySketch[ref.sketch];
        region.sketch = out->second.sketch.get();
        region.plane = out->second.plane;
        region.profiles.push_back(*profile);
    }
    if (bySketch.empty()) {
        throw FeatureError("Los perfiles elegidos ya no existen en el sketch");
    }
    if (lost > 0) {
        warnings.push_back(lost == 1 ? "Un perfil elegido ya no existe." : "Algunos perfiles elegidos ya no existen.");
    }
    std::vector<geometry::SketchRegion> regions;
    for (auto& [id, region] : bySketch) {
        regions.push_back(std::move(region));
    }
    return regions;
}

// Une los sólidos de varios sketches en uno.
inline geometry::Shape fuseTools(const std::vector<geometry::Shape>& tools)
{
    geometry::Shape result = tools.front();
    for (std::size_t i = 1; i < tools.size(); ++i) {
        result = geometry::boolean(geometry::BooleanOp::Fuse, result, tools[i]);
    }
    return result;
}

inline void addProfileDependencies(std::vector<FeatureId>& ids, const std::vector<ProfileRef>& profiles)
{
    for (const ProfileRef& p : profiles) {
        addUnique(ids, p.sketch);
    }
}

// Dónde está el plano de un sitio (origen, plano de construcción o cara). Si ya
// no se encuentra, el último que tuvo, y se avisa.
inline sketch::Plane resolvePlane(const ModelState& state, const SketchAttachment& at, const std::string& subject,
                           const std::string& fallback, std::optional<std::string>& warning)
{
    const auto note = [&](const std::string& text) { warning = warning ? *warning + " " + text : text; };
    if (at.origin) {
        switch (*at.origin) {
        case OriginPlane::XY:
            return sketch::Plane::xy();
        case OriginPlane::XZ:
            return sketch::Plane::xz();
        case OriginPlane::YZ:
            return sketch::Plane::yz();
        }
    }
    if (at.reference != 0) {
        if (const auto it = state.planes.find(at.reference); it != state.planes.end()) {
            return it->second;
        }
        note("El plano de construcción " + subject + " ya no existe; " + fallback + ".");
        return at.lastPlane;
    }
    if (const auto hit = state.findFace(at.face)) {
        if (const auto facePlane = state.bodies[hit->body].shape.facePlane(hit->face)) {
            return *facePlane;
        }
        note("La cara " + subject + " ya no es plana; " + fallback + ".");
    } else {
        note("La cara " + subject + " ya no existe; " + fallback + ".");
    }
    return at.lastPlane;
}

inline void addAttachmentDependencies(std::vector<FeatureId>& ids, const SketchAttachment& at)
{
    if (at.origin) {
        return;
    }
    if (at.reference != 0) {
        ids.push_back(at.reference);
    } else {
        addAll(ids, at.face);
    }
}

inline sketch::Vec3 rotateAbout(const sketch::Vec3& v, const sketch::Vec3& axis, double angle)
{
    const double c = std::cos(angle);
    const double s = std::sin(angle);
    const sketch::Vec3 k{axis.y * v.z - axis.z * v.y, axis.z * v.x - axis.x * v.z, axis.x * v.y - axis.y * v.x};
    const double d = (axis.x * v.x + axis.y * v.y + axis.z * v.z) * (1.0 - c);
    return {v.x * c + k.x * s + axis.x * d, v.y * c + k.y * s + axis.y * d, v.z * c + k.z * s + axis.z * d};
}

} // namespace nuf::document
