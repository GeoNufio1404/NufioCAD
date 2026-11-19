#pragma once

// Operaciones concretas: sketch, extruir, revolución, importar, redondeo, chaflán y vaciado.

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
    FeatureId reference = 0;    // si no hay origin: un plano de construcción (0 = no)
    geometry::ElementRef face;  // si no hay origin ni reference
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
    // Las cotas con valor (no de referencia) se pueden ligar a parámetros: clave «c<id>».
    std::vector<NumericField> numericFields() override;

    // Para editar: el sketch ya calculado (con los valores de las expresiones) y la
    // expresión de cada cota en `Constraint::expression`.
    sketch::Sketch forEditing(const sketch::Sketch& computed) const;
    // Al terminar de editar: guarda el sketch y pasa las expresiones de las cotas a
    // las de la operación (quitando las de cotas que ya no existen).
    void setSketchFromEditing(sketch::Sketch sketch);

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
    std::vector<NumericField> numericFields() override;
    std::unique_ptr<Feature> clone() const override { return std::make_unique<ExtrudeFeature>(*this); }
    std::optional<std::string> compute(ModelState& state) const override;
    std::vector<FeatureId> dependencies() const override;

    // Solo el sólido de la extrusión (para la vista previa y compute).
    geometry::Shape tool(const ModelState& state, std::vector<std::string>* warnings = nullptr) const;
};

// Eje de una revolución.
struct AxisRef {
    enum class Kind { OriginX, OriginY, OriginZ, SketchLine, Edge, Construction };
    Kind kind = Kind::OriginZ;
    FeatureId sketch = 0;          // con SketchLine
    sketch::EntityId entity = 0;   // con SketchLine
    geometry::ElementRef edge;     // con Edge (arista recta)
    FeatureId reference = 0;       // con Construction: el eje de construcción
};

class RevolveFeature final : public Feature {
public:
    std::vector<ProfileRef> profiles;
    AxisRef axis;
    geometry::RevolveSpec spec;
    Operation operation = Operation::NewBody;

    FeatureKind kind() const override { return FeatureKind::Revolve; }
    std::vector<NumericField> numericFields() override;
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
    std::vector<NumericField> numericFields() override;
    std::vector<geometry::ElementRef> edges;
    double size = 1.0;  // radio o distancia, mm
    // Solo el chaflán: igual por los dos lados, con dos distancias o con distancia y ángulo (O2).
    geometry::ChamferStyle style = geometry::ChamferStyle::Equal;
    double size2 = 1.0;                // la segunda distancia, mm (chaflán) o el radio del otro extremo (redondeo variable)
    bool variable = false;             // redondeo de radio variable: de «size» a «size2» a lo largo de la arista
    double middle = 0.0;               // con «variable»: radio a mitad de la arista (0: sin punto intermedio)
    double angle = 0.7853981633974483; // rad, con la distancia y ángulo
    bool flip = false;                 // la cara de referencia es la otra de las dos

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
        return variable ? geometry::filletVariable(tag, body, indices, size, size2, middle) : geometry::fillet(tag, body, indices, size);
    }
};

class ChamferFeature final : public EdgeFeature {
public:
    FeatureKind kind() const override { return FeatureKind::Chamfer; }
    std::unique_ptr<Feature> clone() const override { return std::make_unique<ChamferFeature>(*this); }

protected:
    geometry::Shape apply(std::string_view tag, const geometry::Shape& body, const std::vector<int>& indices) const override
    {
        geometry::ChamferSpec spec;
        spec.style = style;
        spec.distance = size;
        spec.distance2 = size2;
        spec.angle = angle;
        spec.flip = flip;
        return geometry::chamfer(tag, body, indices, spec);
    }
};

// Vaciado de un cuerpo: las caras elegidas quedan abiertas y el resto se
// convierte en paredes del espesor dado.
class ShellFeature final : public Feature {
public:
    std::vector<geometry::ElementRef> faces;  // las que se quitan
    double thickness = 1.0;                   // mm
    bool outward = false;                     // las paredes crecen hacia fuera
    bool closed = false;                      // vaciado cerrado: hueco por dentro sin abrir caras (las elegidas solo señalan el cuerpo)

    FeatureKind kind() const override { return FeatureKind::Shell; }
    std::vector<NumericField> numericFields() override;
    std::unique_ptr<Feature> clone() const override { return std::make_unique<ShellFeature>(*this); }
    std::optional<std::string> compute(ModelState& state) const override;
    std::vector<FeatureId> dependencies() const override;
};

// Espejo: una copia de un cuerpo reflejada sobre un plano (de origen o una
// cara plana). Con «Unir» se junta con lo que toca (el propio cuerpo si toca
// el plano), con «Cuerpo nuevo» queda aparte.
class MirrorFeature final : public Feature {
public:
    std::string body;            // clave del cuerpo («f3»)
    std::vector<std::string> alsoBodies;  // otros cuerpos que se reflejan a la vez (O1); cada uno da su copia
    FeatureId sourceFeature = 0;  // si no es 0: se refleja esa operación (un agujero, un corte…) en vez de cuerpos; «operation» no cuenta
    SketchAttachment plane;      // dónde está el plano de simetría
    Operation operation = Operation::Join;

    FeatureKind kind() const override { return FeatureKind::Mirror; }
    std::unique_ptr<Feature> clone() const override { return std::make_unique<MirrorFeature>(*this); }
    std::optional<std::string> compute(ModelState& state) const override;
    std::vector<FeatureId> dependencies() const override;
};

// Dividir un cuerpo (O4): un plano (de origen, de construcción o una cara plana), movido «offset» mm a lo largo de su normal,
// lo parte en dos cuerpos: el original conserva el lado de la normal y el otro sale como cuerpo nuevo.
class SplitFeature final : public Feature {
public:
    std::string body;        // clave del cuerpo
    SketchAttachment plane;  // dónde está el plano de corte
    double offset = 0.0;     // mm a lo largo de la normal del plano

    FeatureKind kind() const override { return FeatureKind::Split; }
    std::unique_ptr<Feature> clone() const override { return std::make_unique<SplitFeature>(*this); }
    std::optional<std::string> compute(ModelState& state) const override;
    std::vector<FeatureId> dependencies() const override;
    std::vector<NumericField> numericFields() override;
};

// Plano de construcción: parte de un plano de origen, una cara plana u otro
// plano de construcción, se gira un ángulo alrededor de uno de sus ejes y se
// desplaza a lo largo de su normal. No cambia ningún cuerpo: solo da un plano
// donde dibujar sketches, reflejar, etc.
class ConstructionPlaneFeature final : public Feature {
public:
    SketchAttachment base;
    double offset = 10.0;  // mm a lo largo de la normal (tras el giro)
    double angle = 0.0;    // rad
    int angleAxis = 0;     // 0 = alrededor del eje X del plano base, 1 = del eje Y

    FeatureKind kind() const override { return FeatureKind::ConstructionPlane; }
    std::vector<NumericField> numericFields() override;
    std::unique_ptr<Feature> clone() const override { return std::make_unique<ConstructionPlaneFeature>(*this); }
    std::optional<std::string> compute(ModelState& state) const override;
    std::vector<FeatureId> dependencies() const override;
};

// Eje de construcción: la intersección de dos planos o, con un solo plano, la
// recta perpendicular a él por su origen. No cambia ningún cuerpo.
class ConstructionAxisFeature final : public Feature {
public:
    SketchAttachment first;
    bool useSecond = true;
    SketchAttachment second;

    FeatureKind kind() const override { return FeatureKind::ConstructionAxis; }
    std::unique_ptr<Feature> clone() const override { return std::make_unique<ConstructionAxisFeature>(*this); }
    std::optional<std::string> compute(ModelState& state) const override;
    std::vector<FeatureId> dependencies() const override;
};

// Caja: ancho (a lo largo de X del plano), fondo (Y) y alto (la normal; el signo da el sentido), con la
// esquina o el centro en un punto del plano o de una cara. «z» la separa del plano a lo largo de la normal;
// «symmetric» reparte el alto a los dos lados del plano. Todo ligable a parámetros: es la base de las
// piezas que generan las herramientas.
class BoxFeature final : public Feature {
public:
    SketchAttachment base;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double width = 10.0;
    double depth = 10.0;
    double height = 10.0;
    double cornerRadius = 0.0;  // esquinas redondeadas (las aristas paralelas a la normal)
    bool centered = false;   // el punto es el centro del ancho y del fondo
    bool symmetric = false;  // el alto va mitad a cada lado del plano
    Operation operation = Operation::NewBody;

    FeatureKind kind() const override { return FeatureKind::Box; }
    std::unique_ptr<Feature> clone() const override { return std::make_unique<BoxFeature>(*this); }
    std::optional<std::string> compute(ModelState& state) const override;
    std::vector<FeatureId> dependencies() const override;
    std::vector<NumericField> numericFields() override;
};

// Cilindro: diámetro y alto a lo largo de la normal, con el centro de la base en un punto del plano o de una cara.
class CylinderFeature final : public Feature {
public:
    SketchAttachment base;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double diameter = 10.0;
    double height = 10.0;
    bool symmetric = false;
    Operation operation = Operation::NewBody;

    FeatureKind kind() const override { return FeatureKind::Cylinder; }
    std::unique_ptr<Feature> clone() const override { return std::make_unique<CylinderFeature>(*this); }
    std::optional<std::string> compute(ModelState& state) const override;
    std::vector<FeatureId> dependencies() const override;
    std::vector<NumericField> numericFields() override;
};

// Engranaje cilíndrico de involuta (recto o helicoidal) centrado en el origen de
// un plano, con su eje a lo largo de la normal.
class GearFeature final : public Feature {
public:
    SketchAttachment base;   // plano o cara donde se coloca
    double x = 0.0;          // posición del centro en ese plano (mm, desde su origen)
    double y = 0.0;
    double rotation = 0.0;   // giro del engranaje sobre su eje (rad): dónde queda el primer diente
    geometry::GearSpec spec;
    Operation operation = Operation::NewBody;

    FeatureKind kind() const override { return FeatureKind::Gear; }
    std::unique_ptr<Feature> clone() const override { return std::make_unique<GearFeature>(*this); }
    std::optional<std::string> compute(ModelState& state) const override;
    std::vector<FeatureId> dependencies() const override;
    std::vector<NumericField> numericFields() override;
};

// Rosca (varilla roscada o tornillo) en un punto de un plano o de una cara. Con «Cortar» da una rosca
// interior (tuerca): el agujero tiene el perfil del tornillo más la holgura.
class ThreadFeature final : public Feature {
public:
    SketchAttachment base;   // plano o cara donde se coloca
    double x = 0.0;          // posición del eje en ese plano (mm, desde su origen)
    double y = 0.0;
    // Holgura radial (mm) con «Cortar»: el agujero sale más ancho que el tornillo para que éste entre
    // (impreso, un agujero exacto no deja pasar el tornillo).
    double clearance = 0.0;
    geometry::ThreadSpec spec;
    Operation operation = Operation::NewBody;
    // Rosca sobre una cara cilíndrica que ya existe (un agujero o un cilindro macizo): se corta del cuerpo en todo el largo de
    // la cara, con su diámetro; del «spec» solo se usan el paso y el sentido. La cara se busca por nombre.
    bool onCylinder = false;
    geometry::ElementRef cylinderFace;

    FeatureKind kind() const override { return FeatureKind::Thread; }
    std::unique_ptr<Feature> clone() const override { return std::make_unique<ThreadFeature>(*this); }
    std::optional<std::string> compute(ModelState& state) const override;
    std::vector<FeatureId> dependencies() const override;
    std::vector<NumericField> numericFields() override;
};

// Barrido: perfiles de un sketch a lo largo de una trayectoria de curvas de otro.
class SweepFeature final : public Feature {
public:
    std::vector<ProfileRef> profiles;
    FeatureId pathSketch = 0;
    std::vector<sketch::EntityId> path;
    geometry::SweepSpec spec;
    Operation operation = Operation::NewBody;

    FeatureKind kind() const override { return FeatureKind::Sweep; }
    std::unique_ptr<Feature> clone() const override { return std::make_unique<SweepFeature>(*this); }
    std::optional<std::string> compute(ModelState& state) const override;
    std::vector<FeatureId> dependencies() const override;
    std::vector<NumericField> numericFields() override;
};

// Loft: un sólido por varios perfiles, en el orden de la lista.
class LoftFeature final : public Feature {
public:
    std::vector<ProfileRef> profiles;  // en orden
    bool ruled = false;
    Operation operation = Operation::NewBody;

    FeatureKind kind() const override { return FeatureKind::Loft; }
    std::unique_ptr<Feature> clone() const override { return std::make_unique<LoftFeature>(*this); }
    std::optional<std::string> compute(ModelState& state) const override;
    std::vector<FeatureId> dependencies() const override;
};

// Mover o copiar un cuerpo: gira alrededor de un eje (el del origen, uno de
// construcción o uno que pasa por el centro del propio cuerpo) y se desplaza.
class MoveFeature final : public Feature {
public:
    std::string body;                     // clave del cuerpo
    // Otros cuerpos que se mueven y giran junto con él, como un solo bloque (con «copy» solo vale uno).
    std::vector<std::string> alsoMove;
    sketch::Vec3 translation;             // mm
    int axis = 2;                         // 0 = X, 1 = Y, 2 = Z
    FeatureId axisRef = 0;                // si no es 0, un eje de construcción
    bool aroundOrigin = false;            // eje de origen: por el origen (si no, por el centro del cuerpo)
    double angle = 0.0;                   // rad (un solo eje, con el que se podía girar antes)
    // Giro alrededor de los ejes X, Y y Z del mundo (rad), en ese orden, por el centro del cuerpo (o el origen si
    // «aroundOrigin»); es lo que dibujan los anillos de la vista. Se aplica después de «angle».
    sketch::Vec3 rotation;
    bool copy = false;                    // deja el original y crea un cuerpo nuevo

    FeatureKind kind() const override { return FeatureKind::Move; }
    std::unique_ptr<Feature> clone() const override { return std::make_unique<MoveFeature>(*this); }
    std::optional<std::string> compute(ModelState& state) const override;
    std::vector<FeatureId> dependencies() const override;
    std::vector<NumericField> numericFields() override;
};

// Combinar cuerpos: el cuerpo destino se une, se corta o se interseca con los
// cuerpos herramienta, que desaparecen salvo que se conserven.
class CombineFeature final : public Feature {
public:
    std::string target;
    std::vector<std::string> tools;
    Operation operation = Operation::Join;  // Join, Cut o Intersect
    bool keepTools = false;

    FeatureKind kind() const override { return FeatureKind::Combine; }
    std::unique_ptr<Feature> clone() const override { return std::make_unique<CombineFeature>(*this); }
    std::optional<std::string> compute(ModelState& state) const override;
    std::vector<FeatureId> dependencies() const override;
};

enum class JointType { Rigid, Revolute, Slider, Cylindrical, PinSlot, Planar, Ball };

// Una cara o una arista de un cuerpo, para alinear o unir: el centro de una cara plana, el eje de un cilindro, el
// centro de una arista circular… (geometry::Frame).
struct ElementPick {
    geometry::ElementRef ref;
    bool edge = false;
    bool operator==(const ElementPick&) const = default;
};

// Unión entre dos caras planas de dos cuerpos: el cuerpo de la segunda cara se
// coloca de modo que sus caras queden enfrentadas (centro con centro). La
// rígida solo deja una distancia entre ellas; la giratoria, además, un ángulo
// alrededor de la normal; la deslizante, un desplazamiento en el plano de la
// primera cara. La primera cara es la fija.
class JointFeature final : public Feature {
public:
    geometry::ElementRef fixedFace;   // la cara o la arista (según «fixedEdge») de la pieza que se queda
    geometry::ElementRef movingFace;  // lo mismo, de la pieza que se mueve
    bool fixedEdge = false;
    bool movingEdge = false;
    std::vector<std::string> alsoMove;  // otros cuerpos que van con la pieza móvil
    JointType type = JointType::Rigid;
    double offset = 0.0;  // mm entre las caras (positivo las separa); en la cilíndrica, el deslizamiento por el eje
    double angle = 0.0;   // rad alrededor de la normal
    double slide = 0.0;   // mm a lo largo del eje X del elemento fijo
    double slideY = 0.0;  // mm a lo largo de su eje Y (unión plana)
    double tiltX = 0.0;   // rad: cabeceo de la unión esférica
    double tiltY = 0.0;
    bool flip = false;    // del mismo lado (no enfrentadas)
    // Límites del movimiento (Fase 21): el giro, y el deslizamiento (el de X; en la cilíndrica, el del eje).
    bool limitAngle = false;
    double angleMin = 0.0;  // rad
    double angleMax = 0.0;
    bool limitSlide = false;
    double slideMin = 0.0;  // mm
    double slideMax = 0.0;

    FeatureKind kind() const override { return FeatureKind::Joint; }
    std::unique_ptr<Feature> clone() const override { return std::make_unique<JointFeature>(*this); }
    std::optional<std::string> compute(ModelState& state) const override;
    std::vector<FeatureId> dependencies() const override;
    std::vector<NumericField> numericFields() override;
};

// Alinear (Fase 21): coloca de una vez una pieza sobre otra, centro con centro, sin dejar una unión que se pueda mover.
// Si la pieza que se mueve está fija, se mueve la otra (nunca se mueve una pieza fija).
class AlignFeature final : public Feature {
public:
    ElementPick moving;  // en la pieza que salta
    ElementPick target;  // adonde llega
    std::vector<std::string> alsoMove;  // otros cuerpos que van con la pieza que salta
    bool flip = false;   // del otro lado (las caras planas se enfrentan; los ejes van en el mismo sentido)
    double angle = 0.0;  // rad: giro alrededor de la dirección del destino (de 90 en 90 con los botones)
    double offset = 0.0; // mm: separación a lo largo de esa dirección

    FeatureKind kind() const override { return FeatureKind::Align; }
    std::unique_ptr<Feature> clone() const override { return std::make_unique<AlignFeature>(*this); }
    std::optional<std::string> compute(ModelState& state) const override;
    std::vector<FeatureId> dependencies() const override;
    std::vector<NumericField> numericFields() override;
};

// Enlace de movimiento (Fase 21): hace girar (o deslizar) a la vez varias uniones con una relación fija. Es la manera
// de decir «cuando gira el sol, el planeta gira al revés y más rápido»: la «posición» del enlace es lo que gira el
// primer miembro y cada miembro se mueve «razón» veces eso (un giro en radianes; un deslizamiento, en mm por radián).
class MotionLinkFeature final : public Feature {
public:
    struct Member {
        FeatureId joint = 0;
        double ratio = 1.0;
        bool operator==(const Member&) const = default;
    };
    std::vector<Member> members;
    double position = 0.0;  // rad: lo que gira el miembro con razón 1

    FeatureKind kind() const override { return FeatureKind::MotionLink; }
    std::unique_ptr<Feature> clone() const override { return std::make_unique<MotionLinkFeature>(*this); }
    std::optional<std::string> compute(ModelState& state) const override;
    std::vector<FeatureId> dependencies() const override;
    std::vector<NumericField> numericFields() override;
};

enum class PatternLayout { Linear, Circular };

// Patrón: copias de un cuerpo en fila (a lo largo de un eje de origen) o
// alrededor de un eje de origen. «count» incluye el original. Con «Unir» las
// copias se funden con el cuerpo; con «Cuerpo nuevo» forman un cuerpo aparte.
class PatternFeature final : public Feature {
public:
    std::string body;                          // clave del cuerpo («f3»)
    std::vector<std::string> alsoBodies;       // otros cuerpos que se repiten a la vez (O1), cada uno con sus copias
    FeatureId sourceFeature = 0;               // si no es 0: se repite esa operación (un agujero, un corte…) en vez de cuerpos
    PatternLayout layout = PatternLayout::Linear;
    int axis = 0;                              // 0 = X, 1 = Y, 2 = Z (por el origen)
    FeatureId axisRef = 0;                     // si no es 0, un eje de construcción (en vez de axis)
    int count = 3;                             // con el original
    double spacing = 30.0;                     // mm entre copias (lineal)
    double angle = 6.283185307179586;          // rad en total (circular; 2π = vuelta completa)
    Operation operation = Operation::Join;

    FeatureKind kind() const override { return FeatureKind::Pattern; }
    std::vector<NumericField> numericFields() override;
    std::unique_ptr<Feature> clone() const override { return std::make_unique<PatternFeature>(*this); }
    std::optional<std::string> compute(ModelState& state) const override;
    std::vector<FeatureId> dependencies() const override;
};

// Agujeros: un sketch cuyos círculos (su centro) y puntos marcan dónde van; se
// restan del cuerpo hacia dentro del material, desde el plano del sketch.
class HoleFeature final : public Feature {
public:
    FeatureId sketch = 0;   // el sketch que marca los agujeros; 0 si el agujero se puso con un clic en una cara
    // Con «onFace»: un solo agujero puesto con un clic en una cara (o plano): «base» la identifica y (x, y) es el punto en
    // su plano (mm, desde su origen). La cara se busca por nombre, así que el agujero sigue a la pieza si se edita antes.
    bool onFace = false;
    SketchAttachment base;
    double x = 0.0;
    double y = 0.0;
    geometry::HoleSpec spec;
    bool flip = false;  // al otro lado del plano

    FeatureKind kind() const override { return FeatureKind::Hole; }
    std::vector<NumericField> numericFields() override;
    std::unique_ptr<Feature> clone() const override { return std::make_unique<HoleFeature>(*this); }
    std::optional<std::string> compute(ModelState& state) const override;
    std::vector<FeatureId> dependencies() const override;
    // Solo el sólido que se resta (para la vista previa).
    geometry::Shape tool(const ModelState& state) const;
};

// Sólidos que vienen de otro programa (STEP). Guarda el BREP ya reparado, no el
// archivo original: se puede mover o borrar el STEP sin afectar al diseño.
// Cada sólido es un cuerpo; si hay varios, sus claves son «f<id>:c<n>».
class ImportFeature final : public Feature {
public:
    std::string source;                       // nombre del archivo, para mostrar
    std::string sourcePath;                   // la ruta completa donde estaba al importarlo (vacía en los diseños anteriores): para volver a leerlo
    std::shared_ptr<const std::string> brep;  // compartido: las copias no lo duplican
    std::vector<std::string> notes;           // avisos de la importación

    FeatureKind kind() const override { return FeatureKind::Import; }
    std::unique_ptr<Feature> clone() const override { return std::make_unique<ImportFeature>(*this); }
    std::optional<std::string> compute(ModelState& state) const override;
    std::vector<FeatureId> dependencies() const override { return {}; }
};

// El plano en el que está colocada una operación (origen, plano de construcción o cara), tal como lo calcularía
// ella; si ya no se encuentra, el último que tuvo. Para que la interfaz dibuje ayudas sobre él.
sketch::Plane attachmentPlane(const ModelState& state, const SketchAttachment& attachment);

// La recta de un eje (de origen, de un sketch, de construcción o de una arista) tal como la usaría una revolución;
// lanza FeatureError si ya no existe. Para que la interfaz dibuje ayudas alrededor de ella.
geometry::Axis axisOf(const ModelState& state, const AxisRef& axis);

} // namespace nuf::document
