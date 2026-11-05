#pragma once

// Operaciones concretas: sketch, extruir, revolución, importar, redondeo y chaflán.

#include <nuf/document/Feature.hpp>
#include <nuf/geometry/Modeling.hpp>
#include <nuf/sketch/Profiles.hpp>

#include <string_view>

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

// El sketch ya calculado (SketchOutput) puede diferir del guardado: lo
// proyectado del modelo se actualiza con la pieza.
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
    // El sketch con lo proyectado al día (el mismo si no tiene vínculos).
    std::shared_ptr<const sketch::Sketch> updatedSketch(const ModelState& state, const sketch::Plane& plane,
                                                        std::optional<std::string>& warning) const;

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

// Redondeo o chaflán de aristas de un cuerpo. Las aristas se guardan por
// nombre (como todo lo que apunta a la geometría), así que siguen a la pieza si
// se edita lo anterior. Si alguna ya no existe se avisa y se hace con las demás.
class EdgeFeature : public Feature {
public:
    std::vector<geometry::ElementRef> edges;
    double size = 1.0;  // radio o distancia, mm

    std::optional<std::string> compute(ModelState& state) const override;
    std::vector<FeatureId> dependencies() const override;

protected:
    virtual geometry::Shape apply(std::string_view tag, const geometry::Shape& body, const std::vector<int>& indices) const = 0;
};

class FilletFeature final : public EdgeFeature {
public:
    FeatureKind kind() const override { return FeatureKind::Fillet; }
    std::unique_ptr<Feature> clone() const override { return std::make_unique<FilletFeature>(*this); }

protected:
    geometry::Shape apply(std::string_view tag, const geometry::Shape& body, const std::vector<int>& indices) const override
    {
        return geometry::fillet(tag, body, indices, size);
    }
};

class ChamferFeature final : public EdgeFeature {
public:
    FeatureKind kind() const override { return FeatureKind::Chamfer; }
    std::unique_ptr<Feature> clone() const override { return std::make_unique<ChamferFeature>(*this); }

protected:
    geometry::Shape apply(std::string_view tag, const geometry::Shape& body, const std::vector<int>& indices) const override
    {
        return geometry::chamfer(tag, body, indices, size);
    }
};

// Sólidos que vienen de otro programa (STEP). Guarda el BREP ya reparado, no el
// archivo original: se puede mover o borrar el STEP sin afectar al diseño.
// Cada sólido es un cuerpo; si hay varios, sus claves son «f<id>:c<n>».
class ImportFeature final : public Feature {
public:
    std::string source;                       // nombre del archivo, para mostrar
    std::shared_ptr<const std::string> brep;  // compartido: las copias no lo duplican
    std::vector<std::string> notes;           // avisos de la importación

    FeatureKind kind() const override { return FeatureKind::Import; }
    std::unique_ptr<Feature> clone() const override { return std::make_unique<ImportFeature>(*this); }
    std::optional<std::string> compute(ModelState& state) const override;
    std::vector<FeatureId> dependencies() const override { return {}; }
};

} // namespace nuf::document
