#pragma once

// Operaciones de modelado: extruir, revolucionar y booleanas, con nombres
// para las caras que crean (ver Shape.hpp). Lanzan GeometryError si fallan.
//
// «tag» identifica la operación del documento que llama (ej. "f3") y es el
// prefijo de los nombres de las caras nuevas.

#include <nuf/geometry/Shape.hpp>
#include <nuf/sketch/Profiles.hpp>

#include <string_view>
#include <vector>

namespace nuf::geometry {

// Perfiles cerrados de un sketch en su plano.
struct SketchRegion {
    const sketch::Sketch* sketch = nullptr;
    sketch::Plane plane;
    std::vector<sketch::Profile> profiles;
};

// Hasta dónde llega un lado de la extrusión.
enum class Extent {
    Distance,    // «distance» mm
    ThroughAll,  // atraviesa todos los cuerpos de «context»
    ToFace,      // hasta el plano de una cara plana de «context»
};

struct ExtrudeSide {
    Extent extent = Extent::Distance;
    double distance = 10.0;  // mm; en una dirección puede ser negativa (invierte)
    double taper = 0.0;      // radianes; positivo = se ensancha
    ElementRef face;         // con Extent::ToFace
};

enum class ExtrudeDirection {
    One,        // solo side1, a lo largo de la normal del plano
    Symmetric,  // side1 hacia los dos lados
    Two,        // side1 a lo largo de la normal y side2 en contra
};

struct ExtrudeSpec {
    ExtrudeDirection direction = ExtrudeDirection::One;
    ExtrudeSide side1;
    ExtrudeSide side2;
    double startOffset = 0.0;  // mm a lo largo de la normal
};

// Sólido extruido de los perfiles. Las caras laterales se llaman
// «tag:lado:e<curva>», las tapas «tag:inicio:e<k>» y «tag:fin:e<k>», con k la
// menor curva del contorno exterior del perfil.
Shape extrude(std::string_view tag, const SketchRegion& region, const ExtrudeSpec& spec,
              const std::vector<Shape>& context = {});

// Extruye una cara plana de un cuerpo (empujar/tirar); los laterales se
// llaman «tag:lado:<nombre de la arista>».
Shape extrudeFace(std::string_view tag, const Shape& body, int faceIndex, const ExtrudeSpec& spec,
                  const std::vector<Shape>& context = {});

struct Axis {
    sketch::Vec3 point;
    sketch::Vec3 direction{0.0, 0.0, 1.0};
};

enum class RevolveExtent {
    Angle,  // side1 grados en una dirección (o simétrico / dos lados)
    Full,   // vuelta completa
};

struct RevolveSpec {
    RevolveExtent extent = RevolveExtent::Angle;
    ExtrudeDirection direction = ExtrudeDirection::One;
    double angle1 = 3.14159265358979323846;  // radianes; puede ser negativo
    double angle2 = 0.0;
};

// Sólido de revolución de los perfiles alrededor del eje (que no debe
// cruzarlos). Mismos nombres que extrude().
Shape revolve(std::string_view tag, const SketchRegion& region, const Axis& axis, const RevolveSpec& spec);

enum class BooleanOp { Fuse, Cut, Common };

// target ∘ tool, con las caras coplanares unidas. Puede devolver una forma
// nula (un corte que se lleva todo el cuerpo).
Shape boolean(BooleanOp op, const Shape& target, const Shape& tool);

// true si las formas se tocan o se solapan (para elegir con qué unir).
bool touches(const Shape& a, const Shape& b);

// Redondea (radio en mm) o achaflana (distancia en mm) las aristas del cuerpo
// dadas por su índice (Shape::findEdge). Las cadenas de aristas tangentes se
// siguen solas. Las caras nuevas se llaman «tag:redondeo:<arista>» o
// «tag:chaflan:<arista>» (las de las esquinas, «tag:esquina:?<n>»); las demás
// conservan su nombre. Lanzan GeometryError si el kernel no puede (radio
// demasiado grande, aristas que no se pueden redondear…).
Shape fillet(std::string_view tag, const Shape& body, const std::vector<int>& edges, double radius);
Shape chamfer(std::string_view tag, const Shape& body, const std::vector<int>& edges, double distance);

// Copia del cuerpo reflejada sobre el plano. Las caras se llaman
// «tag:espejo:<nombre de la cara original>».
Shape mirror(std::string_view tag, const Shape& body, const sketch::Plane& plane);

struct PatternSpec {
    bool circular = false;
    Axis axis;                         // lineal: dirección de la fila; circular: eje de giro
    double spacing = 0.0;              // mm entre copias (lineal)
    double angle = 6.283185307179586;  // rad en total (circular); 2π = vuelta completa
    int count = 2;                     // con el original
};

// Las copias del cuerpo (sin el original), una forma por copia. La copia k
// tiene las caras «tag:patron<k>:<nombre de la cara original>». Con la vuelta
// completa las copias se reparten en count partes iguales; si no, el ángulo
// total va del original a la última copia.
std::vector<Shape> pattern(std::string_view tag, const Shape& body, const PatternSpec& spec);

struct GearSpec {
    double module = 2.0;                 // mm
    int teeth = 20;
    double pressureAngle = 0.3490658503988659;  // rad (20°)
    double thickness = 10.0;             // mm a lo largo de la normal del plano (el signo da el sentido)
    double bore = 0.0;                   // diámetro del agujero central (0 = macizo)
    double helixAngle = 0.0;             // rad; 0 = recto, el signo es el sentido de la hélice
};

// Engranaje cilíndrico de involuta centrado en el origen del plano (con su
// eje a lo largo de la normal). Diámetro primitivo = módulo × dientes, cabeza
// +1 módulo, raíz −1,25 módulo. Las caras se llaman
// «tag:lado:d<i>:flanco_d|flanco_i|cabeza|raiz|rad_d|rad_i» (i = número de
// diente), «tag:lado:agujero», «tag:inicio» y «tag:fin». Lanza GeometryError si
// los datos no dan un engranaje (dientes puntiagudos o solapados…).
Shape gear(std::string_view tag, const sketch::Plane& plane, const GearSpec& spec);

// Caja con una esquina (o su centro, «centered») en el origen del plano: ancho a lo largo de su X, fondo a lo
// largo de su Y y alto a lo largo de la normal (el signo da el sentido). Caras: «tag:cara:+x», «-x», «+y», «-y»,
// «+z» y «-z» (según los ejes del plano; +z es la que mira hacia donde apunta la normal).
// Con «cornerRadius» > 0 las cuatro aristas paralelas a la normal van redondeadas (las caras de las esquinas se
// llaman «tag:cara:esquina+x+y», etc.).
Shape box(std::string_view tag, const sketch::Plane& plane, double width, double depth, double height, bool centered,
          double cornerRadius = 0.0);

// Cilindro con el centro de su base en el origen del plano y el eje a lo largo de la normal (el signo del alto
// da el sentido). Caras: «tag:cara:lado», «+z» y «-z».
Shape cylinder(std::string_view tag, const sketch::Plane& plane, double diameter, double height);

struct ThreadSpec {
    double diameter = 8.0;     // mm, diámetro exterior (mayor)
    double pitch = 1.25;       // mm de avance por vuelta
    double length = 20.0;      // mm a lo largo de la normal (el signo da el sentido)
    bool leftHanded = false;   // rosca a izquierdas
};

// Varilla roscada (rosca métrica ISO aproximada: cresta P/8, fondo P/4,
// profundidad 0,6134 P) con su eje a lo largo de la normal del plano, desde el
// origen. Sirve de tornillo (nuevo cuerpo, unir) o, restada de un cuerpo con un
// agujero del diámetro menor, de rosca interior. Caras: «tag:rosca»,
// «tag:inicio» y «tag:fin». Lanza GeometryError si los datos no valen.
Shape thread(std::string_view tag, const sketch::Plane& plane, const ThreadSpec& spec);

// Diámetro menor de la rosca (el del agujero que se roscará).
double threadMinorDiameter(const ThreadSpec& spec);

struct SweepSpec {
    bool followPath = true;  // el perfil gira con la trayectoria; si no, queda paralelo a sí mismo
};

// Barrido: el perfil (sus contornos) a lo largo de una trayectoria hecha con
// curvas de otro sketch, que deben formar un solo camino continuo. El perfil
// debe estar donde empieza la trayectoria y perpendicular a ella. Las caras se
// llaman «tag:lado:<origen>» (como la extrusión), «tag:inicio:<perfil>» y
// «tag:fin:<perfil>».
Shape sweep(std::string_view tag, const SketchRegion& profile, const sketch::Sketch& pathSketch,
            const sketch::Plane& pathPlane, const std::vector<sketch::EntityId>& pathEntities, const SweepSpec& spec);

// Loft: un sólido que pasa por varios perfiles (cada región con un solo perfil),
// en el orden dado. Con «ruled» los tramos son rectos; si no, la superficie es
// suave por todos los perfiles. Las caras laterales se llaman
// «tag:lado:<origen en el primer perfil>», «tag:inicio» y «tag:fin».
Shape loft(std::string_view tag, const std::vector<SketchRegion>& sections, bool ruled);

// Mueve un cuerpo: primero lo gira «angle» (rad) alrededor de «axis» y luego lo
// desplaza «translation» (mm). Con «copy» las caras de la copia se llaman
// «tag:copia:<nombre original>»; si no, conservan su nombre (es el mismo cuerpo, en
// otro sitio).
Shape moveBody(std::string_view tag, const Shape& body, const sketch::Vec3& translation, const Axis& axis, double angle,
               bool copy);

// Mueve el cuerpo de modo que el sistema de ejes «from» (origen, ejes X e Y, normal)
// quede sobre «to». Las caras conservan su nombre.
Shape placeBody(std::string_view tag, const Shape& body, const sketch::Plane& from, const sketch::Plane& to);

enum class HoleKind { Simple, Counterbore, Countersink };

struct HoleSpec {
    HoleKind kind = HoleKind::Simple;
    double diameter = 5.0;               // mm
    double depth = 10.0;                 // mm; no se usa con throughAll
    bool throughAll = false;
    double headDiameter = 10.0;          // abocardado o avellanado: diámetro en la superficie
    double headDepth = 3.0;              // abocardado: profundidad del escalón
    double headAngle = 1.5707963267948966;  // avellanado: ángulo total del cono (90°)
};

// Sólido que se resta para hacer los agujeros: uno por centro, en el plano
// dado, hacia dentro del material (en contra de la normal; «flip» la invierte).
// Empieza 1 mm fuera de la superficie para que el corte sea limpio. Con
// throughAll atraviesa todo lo de «context». Las caras se llaman «tag:pared».
Shape holeTool(std::string_view tag, const sketch::Plane& plane, const std::vector<sketch::Vec2>& centers,
               const HoleSpec& spec, bool flip, const std::vector<Shape>& context = {});

// Vacía el cuerpo: quita las caras dadas (por índice) y deja paredes de
// «thickness» mm, hacia dentro o hacia fuera. Las caras de las paredes nuevas se
// llaman «tag:interior:<cara de origen>»; las demás, incluida la que queda como
// borde de la abertura, conservan su nombre.
Shape shell(std::string_view tag, const Shape& body, const std::vector<int>& removedFaces, double thickness, bool outward);

} // namespace nuf::geometry
