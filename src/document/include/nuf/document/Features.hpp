#pragma once

// Operaciones concretas: sketch, extruir y revolución.

#include <nuf/document/Feature.hpp>
#include <nuf/geometry/Modeling.hpp>
#include <nuf/sketch/Profiles.hpp>

namespace nuf::document {

enum class OriginPlane { XY, XZ, YZ };

// Dónde va un sketch: un plano de origen o una cara plana de un cuerpo (lo
// sigue si la pieza cambia).
struct SketchAttachment {
    std::optional<OriginPlane> origin = OriginPlane::XY;
    geometry::ElementRef face;  // si no hay origin
    // Plano que tenía al crearse: se usa si la cara deja de existir.
    sketch::Plane lastPlane = sketch::Plane::xy();
};

class SketchFeature final : public Feature {
public:
    SketchFeature() = default;
    SketchFeature(sketch::Sketch sketch, SketchAttachment attachment);

    const sketch::Sketch& sketch() const { return *sketch_; }
    void setSketch(sketch::Sketch sketch);
    const SketchAttachment& attachment() const { return attachment_; }
    void setAttachment(SketchAttachment attachment) { attachment_ = std::move(attachment); }

    FeatureKind kind() const override { return FeatureKind::Sketch; }
    std::unique_ptr<Feature> clone() const override { return std::make_unique<SketchFeature>(*this); }
    std::optional<std::string> compute(ModelState& state) const override;
    std::vector<FeatureId> dependencies() const override;

private:
    // Compartido e inmutable: las copias (deshacer, caché) no lo duplican.
    std::shared_ptr<const sketch::Sketch> sketch_ = std::make_shared<sketch::Sketch>();
    SketchAttachment attachment_;
};

// Perfil de un sketch, identificado por las curvas de su contorno exterior
// (así se reconoce aunque el sketch se edite).
struct ProfileRef {
    FeatureId sketch = 0;
    std::vector<sketch::EntityId> outer;  // ordenadas

    friend bool operator==(const ProfileRef&, const ProfileRef&) = default;
};

ProfileRef profileRef(FeatureId sketch, const sketch::Profile& profile);
// El perfil que corresponde: el de mismo contorno o, si se editó, el que
// comparte más curvas (al menos la mitad).
std::optional<sketch::Profile> resolveProfile(const std::vector<sketch::Profile>& profiles, const ProfileRef& ref);

// Qué hace el sólido nuevo con los cuerpos que toca.
enum class Operation { Join, Cut, Intersect, NewBody };

// Aplica el sólido de una operación a los cuerpos; devuelve un aviso si no
// tocaba ninguno (al cortar o intersecar).
std::optional<std::string> applyOperation(ModelState& state, const Feature& feature, Operation operation,
                                          const geometry::Shape& tool);

class ExtrudeFeature final : public Feature {
public:
    // Perfiles de sketches o, si no hay, una cara plana (empujar/tirar).
    std::vector<ProfileRef> profiles;
    geometry::ElementRef face;
    geometry::ExtrudeSpec spec;
    Operation operation = Operation::NewBody;

    FeatureKind kind() const override { return FeatureKind::Extrude; }
    std::unique_ptr<Feature> clone() const override { return std::make_unique<ExtrudeFeature>(*this); }
    std::optional<std::string> compute(ModelState& state) const override;
    std::vector<FeatureId> dependencies() const override;

    // Solo el sólido de la extrusión (para la vista previa y compute).
    geometry::Shape tool(const ModelState& state, std::vector<std::string>* warnings = nullptr) const;
};

// Eje de una revolución.
struct AxisRef {
    enum class Kind { OriginX, OriginY, OriginZ, SketchLine, Edge };
    Kind kind = Kind::OriginZ;
    FeatureId sketch = 0;          // con SketchLine
    sketch::EntityId entity = 0;   // con SketchLine
    geometry::ElementRef edge;     // con Edge (arista recta)
};

class RevolveFeature final : public Feature {
public:
    std::vector<ProfileRef> profiles;
    AxisRef axis;
    geometry::RevolveSpec spec;
    Operation operation = Operation::NewBody;

    FeatureKind kind() const override { return FeatureKind::Revolve; }
    std::unique_ptr<Feature> clone() const override { return std::make_unique<RevolveFeature>(*this); }
    std::optional<std::string> compute(ModelState& state) const override;
    std::vector<FeatureId> dependencies() const override;

    geometry::Shape tool(const ModelState& state, std::vector<std::string>* warnings = nullptr) const;
};

} // namespace nuf::document
