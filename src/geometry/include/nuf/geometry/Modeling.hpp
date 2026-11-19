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
    ToFace,      // hasta una cara de «context»: plana, cilíndrica, esférica o toroidal
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

// Une varias formas de dos en dos, como un torneo (1+2, 3+4… y luego los resultados entre sí), en vez de ir añadiendo
// una a una a un resultado cada vez más grande: cada unión trabaja con formas pequeñas y el coste de un patrón de 50
// copias deja de crecer más que lo lineal. Mismo resultado que unirlas una a una (la unión es asociativa; si se
// solapan entre sí, cada par lo resuelve el kernel). Forma nula si no hay ninguna.
Shape fuseBalanced(std::vector<Shape> parts);

// true si las formas se tocan o se solapan (para elegir con qué unir).
bool touches(const Shape& a, const Shape& b);

// El volumen que dos cuerpos tienen en común (Fase 21, interferencias): nada si no se pisan o si lo común es menor que
// «minVolume» mm³ (tocarse por una cara no cuenta). Lo primero que mira es que sus cajas se crucen, así que parejas
// lejanas cuestan casi nada.
struct Overlap {
    Shape shape;
    double volume = 0.0;
};
std::optional<Overlap> overlap(const Shape& a, const Shape& b, double minVolume = 1e-3);

// Redondea (radio en mm) o achaflana (distancia en mm) las aristas del cuerpo
// dadas por su índice (Shape::findEdge). Las cadenas de aristas tangentes se
// siguen solas. Las caras nuevas se llaman «tag:redondeo:<arista>» o
// «tag:chaflan:<arista>» (las de las esquinas, «tag:esquina:?<n>»); las demás
// conservan su nombre. Lanzan GeometryError si el kernel no puede (radio
// demasiado grande, aristas que no se pueden redondear…).
Shape fillet(std::string_view tag, const Shape& body, const std::vector<int>& edges, double radius);
// Chaflán de varias medidas: «distance» es la distancia sobre la cara de referencia (la primera de las dos que comparten
// la arista; con «flip», la otra) y, según el tipo, la otra distancia sobre la cara vecina o el ángulo con que el chaflán se
// aparta de esa cara de referencia (rad, entre 0 y 90°).
// Redondeo de radio variable (O2): en cada arista elegida el radio va de «start» en su primer vértice a «end» en el último.
Shape filletVariable(std::string_view tag, const Shape& body, const std::vector<int>& edges, double start, double end);

enum class ChamferStyle { Equal, TwoDistances, DistanceAngle };
struct ChamferSpec {
    ChamferStyle style = ChamferStyle::Equal;
    double distance = 1.0;
    double distance2 = 1.0;
    double angle = 0.7853981633974483;
    bool flip = false;
};
Shape chamfer(std::string_view tag, const Shape& body, const std::vector<int>& edges, const ChamferSpec& spec);

Shape chamfer(std::string_view tag, const Shape& body, const std::vector<int>& edges, double distance);

// Copia del cuerpo reflejada sobre el plano. Las caras se llaman
// «tag:espejo:<nombre de la cara original>».
// Divide un cuerpo con un plano en dos: el de la parte hacia donde apunta la normal del plano y el de la otra. Si el plano no
// lo atraviesa lanza GeometryError. Las caras del corte se llaman «tag:corte» y «tag:corte:2».
std::pair<Shape, Shape> splitBody(std::string_view tag, const Shape& body, const sketch::Plane& plane);

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

// Tipo de dentado: rueda exterior, corona con los dientes hacia dentro (engranaje interno) o cremallera
// (barra dentada: es una rueda de radio infinito).
enum class GearType { External, Internal, Rack, Worm, Bevel };

// Corrección de perfil recomendada para que un engranaje exterior de «teeth» dientes no salga puntiagudo ni socavado:
// 0 a partir de 17 dientes (con 20°) y crece al bajar de ahí (x = (z mínimo − z)/z mínimo, con z mínimo = 2/sen²α).
double recommendedProfileShift(int teeth, double pressureAngle);

struct GearSpec {
    GearType type = GearType::External;
    double rim = 5.0;                    // mm: grosor del aro por fuera de los huecos (interno) o de la base bajo la raíz (cremallera)
    double module = 2.0;                 // mm
    int teeth = 20;
    double pressureAngle = 0.3490658503988659;  // rad (20°)
    double thickness = 10.0;             // mm a lo largo de la normal del plano (el signo da el sentido)
    double bore = 0.0;                   // diámetro del agujero central (0 = macizo)
    double helixAngle = 0.0;             // rad; 0 = recto, el signo es el sentido de la hélice
    double length = 0.0;                 // cremallera: largo de la barra en mm; 0 = «teeth»·π·módulo. Si es menor, la barra
                                         // se corta a esa medida (el último diente queda cortado); no puede ser mayor
    double backlash = 0.0;               // mm: cuánto se adelgaza cada diente medido sobre la circunferencia primitiva (0 = sin juego);
                                         // en el interno se ensancha el hueco. Deja holgura sin mover los ejes
    double herringboneGroove = 0.0;     // mm: surco en el centro de la espina de pescado, donde se juntan las dos mitades (0 = sin surco);
                                         // se corta una corona hasta la raíz de los dientes, a lo ancho de esta medida
    bool herringbone = false;            // espina de pescado: dos mitades con la hélice contraria (necesita helixAngle ≠ 0; solo exterior).
                                         // Las caras de la segunda mitad llevan «:m2» al final de su nombre
    double wormDiameter = 20.0;          // tipo Worm: diámetro primitivo del tornillo (mm); «teeth» son sus entradas (1 a 4),
                                         // «thickness» su largo, «module» y «pressureAngle» los axiales; el resto de campos no cuenta
    bool leftHanded = false;             // tipo Worm: hélice a izquierdas
    int mateTeeth = 20;                  // tipo Bevel: dientes de la rueda que engrana con esta (con ejes a 90°) y que fija el ángulo del cono;
                                         // «thickness» es el ancho de cara medido sobre el cono (el largo axial es ancho·cos del ángulo del cono),
                                         // «module» el del extremo grande; el vértice del cono queda a un radio primitivo de la rueda pareja
    double heightScale = 1.0;            // uso interno: escala la cabeza y la raíz del diente (los cónicos la fijan solos)
    double profileShift = 0.0;           // corrección de perfil x (en módulos, solo exterior): x>0 alarga la cabeza y engruesa el diente
                                         // (evita el socavado y los dientes puntiagudos con pocos dientes); x<0 hace lo contrario.
                                         // Dos ruedas con x opuestas (x1 = −x2) engranan a la misma distancia entre ejes
    double baseChamfer = 0.0;            // mm: chaflán en las aristas de la cara inicial («pata de elefante» de la primera capa); 0 = sin chaflán;
                                         // solo dientes rectos; el kernel falla de forma irregular con algunas medidas según los dientes (ver chamferStart)
};

// Interno: «teeth» dientes de la corona, cuyo hueco con forma de diente de rueda lleva cabeza −1 módulo y raíz
// +1,25 módulo (el piñón engrana por dentro, a la distancia (z corona − z piñón)·módulo/2); un aro de grosor «rim»
// lo rodea y el primer hueco apunta por el eje X del plano. Solo dientes rectos (sin hélice ni agujero).
// Cremallera: «teeth» dientes en una barra recta de largo teeth·π·módulo, con la línea primitiva sobre el eje X
// del plano desde X = 0 hasta el largo, un hueco centrado en X = 0 y en X = π·módulo·k, los dientes hacia +Y y una
// base de grosor «rim» bajo la raíz; caras «tag:lado:d<i>:flanco_d|flanco_i|cabeza|raiz», «…:base», «…:extremo_i|d».
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

// Tornillo sin fin: una varilla con «starts» entradas (1 a 4) cuyo perfil axial es el de una cremallera de ese módulo
// (cabeza 1 módulo, raíz 1,25 módulos, flancos al ángulo de presión). El avance por vuelta es starts·π·módulo.
struct WormSpec {
    double module = 2.0;           // mm (módulo axial: el de la rueda que engrana con él)
    int starts = 1;                // entradas, de 1 a 4
    double pitchDiameter = 20.0;   // mm, diámetro primitivo del tornillo (suele ser 8 a 12 módulos)
    double length = 30.0;          // mm a lo largo de la normal (el signo da el sentido)
    double pressureAngle = 0.3490658503988659;  // rad (20°)
    bool leftHanded = false;       // hélice a izquierdas
};

// Ángulo de avance del tornillo (rad): atan(entradas·módulo / diámetro primitivo). La rueda que engrana con él lleva
// ese mismo ángulo de hélice.
double wormLeadAngle(const WormSpec& spec);

// El tornillo con su eje a lo largo de la normal del plano, desde el origen. Caras: «tag:tornillo», «tag:inicio» y
// «tag:fin». Lanza GeometryError si los datos no valen.
Shape worm(std::string_view tag, const sketch::Plane& plane, const WormSpec& spec);

struct SweepSpec {
    bool followPath = true;  // el perfil gira con la trayectoria; si no, queda paralelo a sí mismo
    double endScale = 1.0;   // escala del perfil al final de la trayectoria (1 = sin cambio; 0,5 = termina a la mitad de tamaño)
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
// otro sitio). Con «turns» (rad) además lo gira alrededor de los ejes X, Y y Z del mundo, en ese orden, por el punto
// «turnsAround» (después del giro de «angle» y antes del desplazamiento).
Shape moveBody(std::string_view tag, const Shape& body, const sketch::Vec3& translation, const Axis& axis, double angle,
               bool copy, const sketch::Vec3& turns = {}, const sketch::Vec3& turnsAround = {});

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

// Vaciado cerrado (O3): sin abrir ninguna cara, el cuerpo queda con un hueco por dentro (una cavidad cerrada) y paredes de
// «thickness» mm. Las caras del hueco se llaman «tag:interior:<cara de origen>». Solo hacia dentro.
Shape shellClosed(std::string_view tag, const Shape& body, double thickness);

} // namespace nuf::geometry
