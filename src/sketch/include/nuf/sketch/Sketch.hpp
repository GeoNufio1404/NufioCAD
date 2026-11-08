#pragma once

// Modelo de un sketch 2D: entidades, restricciones y cotas.
// C++ puro (sin Qt ni OCCT). Las coordenadas son las del plano del sketch,
// en milímetros; los ángulos, en radianes.

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace nuf::sketch {

struct Vec2 {
    double x = 0.0;
    double y = 0.0;
};

// Identificadores estables dentro del sketch (0 = ninguno). No se reutilizan
// al borrar, para que las referencias guardadas no apunten a otra entidad.
using EntityId = std::uint32_t;
using ConstraintId = std::uint32_t;

// ---------------------------------------------------------------------------
// Entidades
// ---------------------------------------------------------------------------

struct Point {
    Vec2 position;
};

struct Line {
    Vec2 start;
    Vec2 end;
};

struct Circle {
    Vec2 center;
    double radius = 0.0;
};

// Arco en sentido antihorario de startAngle a endAngle.
struct Arc {
    Vec2 center;
    double radius = 0.0;
    double startAngle = 0.0;
    double endAngle = 0.0;

    Vec2 startPoint() const;
    Vec2 endPoint() const;
};

// Elipse: semiejes (mayor ≥ menor) y giro del eje mayor respecto al eje X.
struct Ellipse {
    Vec2 center;
    double majorRadius = 0.0;
    double minorRadius = 0.0;
    double rotation = 0.0;

    // Punto en el ángulo paramétrico (excéntrico) t.
    Vec2 pointAt(double t) const;
};

// Arco de elipse, antihorario entre los ángulos paramétricos start y end.
struct EllipseArc {
    Ellipse ellipse;
    double startParam = 0.0;
    double endParam = 0.0;

    Vec2 startPoint() const { return ellipse.pointAt(startParam); }
    Vec2 endPoint() const { return ellipse.pointAt(endParam); }
};

// B-spline sujeta en los extremos (pasa por el primer y el último polo),
// con nudos uniformes. Con pesos distintos de 1 es racional (así se
// representa la curva cónica: grado 2, tres polos y peso central).
struct BSpline {
    std::vector<Vec2> poles;
    std::vector<double> weights;  // uno por polo
    int degree = 3;

    // Grado efectivo: no puede superar polos − 1.
    int effectiveDegree() const;
    // Nudos distintos (0, 1, …, m) y su multiplicidad.
    std::vector<double> knots() const;
    std::vector<int> multiplicities() const;
    // Punto en u ∈ [0, 1].
    Vec2 pointAt(double u) const;
};

using Geometry = std::variant<Point, Line, Circle, Arc, Ellipse, EllipseArc, BSpline>;

// Polilínea que aproxima una curva (para dibujarla, seleccionarla y buscar
// perfiles); cerrada (último = primero) en círculos y elipses. Vacía para
// un punto.
std::vector<Vec2> polyline(const Geometry& geometry);
// true si la curva es cerrada por sí sola (círculo, elipse).
bool isClosedCurve(const Geometry& geometry);

// Vínculo de lo proyectado con la arista del modelo de la que sale: al
// recalcular, el documento la vuelve a proyectar y actualiza la entidad.
struct ModelLink {
    std::string edge;    // nombre estable de la arista
    int occurrence = 0;  // cuál, si varias comparten nombre
    int piece = 0;       // qué trozo de su proyección (una spline se trocea)
};

struct Entity {
    EntityId id = 0;
    Geometry geometry;
    // Geometría auxiliar: participa en las restricciones pero no en los perfiles.
    bool construction = false;
    // Referencia fija (origen y ejes): el solver no la mueve y no se borra.
    bool reference = false;
    // Bloqueada (lo proyectado del modelo, texto, DXF): el solver no la
    // mueve, pero cuenta para los perfiles y se puede borrar o desbloquear.
    bool locked = false;
    // Con vínculo, además se actualiza con el modelo.
    std::optional<ModelLink> link;
};

// ---------------------------------------------------------------------------
// Referencias a partes de una entidad
// ---------------------------------------------------------------------------

// Whole es la entidad completa (la curva, o el propio punto si es un Point).
// Pole es un polo de una spline (con su índice).
enum class Part : std::uint8_t { Whole, Start, End, Center, Pole };

struct Ref {
    EntityId entity = 0;
    Part part = Part::Whole;
    int index = 0;  // solo para Pole

    Ref() = default;
    Ref(EntityId e, Part p = Part::Whole, int i = 0)
        : entity(e)
        , part(p)
        , index(i)
    {}

    static Ref of(EntityId id) { return {id, Part::Whole}; }
    static Ref start(EntityId id) { return {id, Part::Start}; }
    static Ref end(EntityId id) { return {id, Part::End}; }
    static Ref center(EntityId id) { return {id, Part::Center}; }
    static Ref pole(EntityId id, int i) { return {id, Part::Pole, i}; }

    bool empty() const noexcept { return entity == 0; }
    bool operator==(const Ref&) const = default;
};

// ---------------------------------------------------------------------------
// Restricciones y cotas
// ---------------------------------------------------------------------------

// Argumentos de cada tipo («punto» = Ref a un Point, o Start/End/Center de
// una curva; «línea», «curva» = Ref::of de esa entidad):
enum class ConstraintType : std::uint8_t {
    // Geométricas
    Coincident,     // punto, punto
    PointOnObject,  // punto, curva
    Horizontal,     // línea | punto, punto
    Vertical,       // línea | punto, punto
    Parallel,       // línea, línea
    Perpendicular,  // línea, línea
    Tangent,        // curva, curva (no ambas líneas) | extremo, extremo: une los dos
                    // extremos y deja las curvas tangentes en ese punto
    Equal,          // línea, línea (longitud) | círculo/arco, círculo/arco (radio)
    Collinear,      // línea, línea
    Concentric,     // círculo/arco, círculo/arco
    Midpoint,       // punto, línea
    Symmetric,      // punto, punto, línea | punto, punto, punto
    Fix,            // punto | entidad completa
    // Dimensionales (usan value)
    Curvature,      // extremo, extremo: como Tangent en un extremo y además
                    // con la misma curvatura (continuidad G2)
    Distance,       // punto, punto | punto, línea | línea (longitud)
    DistanceX,      // punto, punto | línea  (b.x − a.x)
    DistanceY,      // punto, punto | línea  (b.y − a.y)
    Angle,          // línea, línea (de la primera a la segunda, antihorario)
    Radius,         // círculo/arco
    Diameter,       // círculo/arco
    MajorRadius,    // elipse / arco de elipse
    MinorRadius,    // elipse / arco de elipse
};

bool isDimensional(ConstraintType type) noexcept;

struct Constraint {
    ConstraintId id = 0;
    ConstraintType type = ConstraintType::Coincident;
    Ref first;
    Ref second;
    Ref third;
    double value = 0.0;
    // false = cota de referencia: no restringe, solo muestra la medida
    // (el solver la actualiza con la geometría).
    bool driving = true;
    // Expresión con parámetros que dio el valor de la cota (vacía = número a
    // secas). Solo vive mientras se edita el sketch: el documento la guarda en
    // las expresiones de la operación (clave «c<id>») y recalcula el valor.
    std::string expression;

    static Constraint coincident(Ref a, Ref b);
    static Constraint pointOnObject(Ref point, EntityId curve);
    static Constraint horizontal(EntityId line);
    static Constraint horizontal(Ref a, Ref b);
    static Constraint vertical(EntityId line);
    static Constraint vertical(Ref a, Ref b);
    static Constraint parallel(EntityId a, EntityId b);
    static Constraint perpendicular(EntityId a, EntityId b);
    static Constraint tangent(EntityId a, EntityId b);
    // Tangencia en el punto de unión de dos curvas (Ref::start/end de cada una).
    static Constraint tangentAt(Ref a, Ref b);
    // Continuidad de curvatura (G2) en el punto de unión de dos curvas.
    static Constraint curvatureAt(Ref a, Ref b);
    static Constraint equal(EntityId a, EntityId b);
    static Constraint collinear(EntityId a, EntityId b);
    static Constraint concentric(EntityId a, EntityId b);
    static Constraint midpoint(Ref point, EntityId line);
    static Constraint symmetric(Ref a, Ref b, Ref axis);
    static Constraint fix(Ref what);

    static Constraint distance(Ref a, Ref b, double value);
    static Constraint length(EntityId line, double value);
    static Constraint distanceX(Ref a, Ref b, double value);
    static Constraint distanceY(Ref a, Ref b, double value);
    static Constraint angle(EntityId a, EntityId b, double radians);
    static Constraint radius(EntityId curve, double value);
    static Constraint diameter(EntityId curve, double value);
    static Constraint majorRadius(EntityId ellipse, double value);
    static Constraint minorRadius(EntityId ellipse, double value);
};

// ---------------------------------------------------------------------------
// Sketch
// ---------------------------------------------------------------------------

class Sketch {
public:
    // Referencias que todo sketch trae: el origen y los ejes X e Y.
    static constexpr EntityId kOrigin = 1;
    static constexpr EntityId kAxisX = 2;
    static constexpr EntityId kAxisY = 3;

    Sketch();

    // Reconstruye un sketch guardado conservando los ids y sus contadores (no
    // se reutilizan: lo que apunta a ellos —perfiles, ejes, vínculos— sigue
    // valiendo). Lanza std::invalid_argument si los datos no son coherentes.
    static Sketch restore(std::vector<Entity> entities, std::vector<Constraint> constraints,
                          EntityId nextEntityId, ConstraintId nextConstraintId);
    EntityId nextEntityId() const noexcept { return nextEntityId_; }
    ConstraintId nextConstraintId() const noexcept { return nextConstraintId_; }

    EntityId addPoint(Vec2 position, bool construction = false);
    EntityId addLine(Vec2 start, Vec2 end, bool construction = false);
    EntityId addCircle(Vec2 center, double radius, bool construction = false);
    EntityId addArc(Vec2 center, double radius, double startAngle, double endAngle,
                    bool construction = false);
    EntityId addEllipse(const Ellipse& ellipse, bool construction = false);
    EntityId addEllipseArc(const EllipseArc& arc, bool construction = false);
    // Lanza std::invalid_argument con menos de 2 polos o pesos no positivos.
    EntityId addBSpline(const BSpline& spline, bool construction = false);
    // Cualquier tipo de geometría (con las mismas comprobaciones).
    EntityId addGeometry(const Geometry& geometry, bool construction = false);

    // Borra la entidad y las restricciones que la usan. Las referencias
    // (origen y ejes) no se pueden borrar: lanza std::invalid_argument.
    void removeEntity(EntityId id);

    // Valida los argumentos según el tipo (lanza std::invalid_argument)
    // y asigna un id nuevo.
    ConstraintId addConstraint(Constraint constraint);
    // true si addConstraint aceptaría esos argumentos.
    bool canAdd(const Constraint& constraint) const;
    void removeConstraint(ConstraintId id);

    const std::vector<Entity>& entities() const noexcept { return entities_; }
    const std::vector<Constraint>& constraints() const noexcept { return constraints_; }

    const Entity* entity(EntityId id) const;
    Entity* entity(EntityId id);
    const Constraint* constraint(ConstraintId id) const;
    Constraint* constraint(ConstraintId id);

    // Posición de un punto (Ref a un Point o Start/End/Center de una curva).
    std::optional<Vec2> point(Ref ref) const;

    // Valor actual de una cota medido sobre la geometría (para cotas de
    // referencia y para mostrar el valor inicial al acotar).
    double measure(const Constraint& constraint) const;

private:
    EntityId addEntity(Geometry geometry, bool construction, bool reference);
    void validate(const Constraint& constraint) const;

    std::vector<Entity> entities_;
    std::vector<Constraint> constraints_;
    EntityId nextEntityId_ = 1;
    ConstraintId nextConstraintId_ = 1;
};

} // namespace nuf::sketch
