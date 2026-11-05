#pragma once

// Operación del historial y el estado del modelo que van construyendo.

#include <nuf/geometry/Shape.hpp>
#include <nuf/sketch/Plane.hpp>

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace nuf::document {

// Identificador estable de una operación (no cambia al reordenar).
using FeatureId = std::uint64_t;

struct Body {
    std::string key;   // estable: «f<id>» de la operación que lo creó
    std::string name;  // «Cuerpo1»
    geometry::Shape shape;
};

// Sketch ya colocado en su plano.
struct SketchOutput {
    std::shared_ptr<const sketch::Sketch> sketch;
    sketch::Plane plane;
};

// Resultado acumulado tras una operación.
struct ModelState {
    std::vector<Body> bodies;
    std::map<FeatureId, SketchOutput> sketches;
    int bodiesCreated = 0;  // para numerar «Cuerpo N»

    std::vector<geometry::Shape> shapes() const;
    // Cuerpo y cara que señala la referencia, en cualquier cuerpo.
    struct FaceHit {
        std::size_t body = 0;
        int face = 0;
    };
    std::optional<FaceHit> findFace(const geometry::ElementRef& ref) const;
    struct EdgeHit {
        std::size_t body = 0;
        int edge = 0;
    };
    std::optional<EdgeHit> findEdge(const geometry::ElementRef& ref) const;
};

enum class FeatureKind { Sketch, Extrude, Revolve, Import, Fillet, Chamfer, Shell, Hole, Mirror };

enum class Health {
    Ok,
    Warning,     // calculada, pero algo no es como se pidió
    Error,       // no se pudo calcular: el modelo sigue como antes de ella
    Suppressed,  // suprimida por el usuario
    RolledBack,  // después del marcador: no se calcula
};

struct FeatureStatus {
    Health health = Health::Ok;
    std::string message;
};

// Error al calcular una operación, con un mensaje para el usuario.
class FeatureError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class Feature {
public:
    virtual ~Feature() = default;

    FeatureId id() const { return id_; }
    void setId(FeatureId id) { id_ = id; }
    const std::string& name() const { return name_; }
    void setName(std::string name) { name_ = std::move(name); }
    bool suppressed() const { return suppressed_; }
    void setSuppressed(bool suppressed) { suppressed_ = suppressed; }
    // Prefijo de los nombres de las caras que crea: «f<id>».
    std::string tag() const { return "f" + std::to_string(id_); }

    virtual FeatureKind kind() const = 0;
    virtual std::unique_ptr<Feature> clone() const = 0;

    // Aplica la operación al estado. Lanza FeatureError si no puede; si se
    // calcula con algún reparo, devuelve el aviso.
    virtual std::optional<std::string> compute(ModelState& state) const = 0;

    // Operaciones de las que depende (sketches, caras que referencia…).
    virtual std::vector<FeatureId> dependencies() const = 0;

protected:
    Feature() = default;
    Feature(const Feature&) = default;
    Feature& operator=(const Feature&) = default;

private:
    FeatureId id_ = 0;
    std::string name_;
    bool suppressed_ = false;
};

// Operaciones a las que apunta una referencia (los «f<id>:» de su nombre).
std::vector<FeatureId> featuresIn(const geometry::ElementRef& ref);

} // namespace nuf::document
