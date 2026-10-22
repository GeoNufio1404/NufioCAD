#pragma once

// Modelo de un sketch 2D: entidades, restricciones y cotas.
// C++ puro (sin Qt ni OCCT). Las coordenadas son las del plano del sketch,
// en milímetros; los ángulos, en radianes.

#include <cstdint>
#include <optional>
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

using Geometry = std::variant<Point, Line, Circle, Arc>;

struct Entity {
    EntityId id = 0;
    Geometry geometry;
    // Geometría auxiliar: participa en las restricciones pero no en los perfiles.
    bool construction = false;
    // Referencia fija (origen y ejes): el solver no la mueve y no se borra.
    bool reference = false;
};

// ---------------------------------------------------------------------------
// Referencias a partes de una entidad
// ---------------------------------------------------------------------------

// Whole es la entidad completa (la curva, o el propio punto si es un Point).
enum class Part : std::uint8_t { Whole, Start, End, Center };

struct Ref {
    EntityId entity = 0;
    Part part = Part::Whole;

    static Ref of(EntityId id) { return {id, Part::Whole}; }
    static Ref start(EntityId id) { return {id, Part::Start}; }
    static Ref end(EntityId id) { return {id, Part::End}; }
    static Ref center(EntityId id) { return {id, Part::Center}; }

    bool empty() const noexcept { return entity == 0; }
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
    Tangent,        // curva, curva (no ambas líneas)
    Equal,          // línea, línea (longitud) | círculo/arco, círculo/arco (radio)
    Collinear,      // línea, línea
    Concentric,     // círculo/arco, círculo/arco
    Midpoint,       // punto, línea
    Symmetric,      // punto, punto, línea | punto, punto, punto
    Fix,            // punto | entidad completa
    // Dimensionales (usan value)
    Distance,       // punto, punto | punto, línea | línea (longitud)
    DistanceX,      // punto, punto | línea  (b.x − a.x)
    DistanceY,      // punto, punto | línea  (b.y − a.y)
    Angle,          // línea, línea (de la primera a la segunda, antihorario)
    Radius,         // círculo/arco
    Diameter,       // círculo/arco
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

    static Constraint coincident(Ref a, Ref b);
    static Constraint pointOnObject(Ref point, EntityId curve);
    static Constraint horizontal(EntityId line);
    static Constraint horizontal(Ref a, Ref b);
    static Constraint vertical(EntityId line);
    static Constraint vertical(Ref a, Ref b);
    static Constraint parallel(EntityId a, EntityId b);
    static Constraint perpendicular(EntityId a, EntityId b);
    static Constraint tangent(EntityId a, EntityId b);
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

    EntityId addPoint(Vec2 position, bool construction = false);
    EntityId addLine(Vec2 start, Vec2 end, bool construction = false);
    EntityId addCircle(Vec2 center, double radius, bool construction = false);
    EntityId addArc(Vec2 center, double radius, double startAngle, double endAngle,
                    bool construction = false);

    // Borra la entidad y las restricciones que la usan. Las referencias
    // (origen y ejes) no se pueden borrar: lanza std::invalid_argument.
    void removeEntity(EntityId id);

    // Valida los argumentos según el tipo (lanza std::invalid_argument)
    // y asigna un id nuevo.
    ConstraintId addConstraint(Constraint constraint);
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
