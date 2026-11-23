#pragma once

// Edición del sketch (Fase 2c): recortar, extender, partir, empalme,
// chaflán, desfase, simetría, patrones y transformaciones. Funciones puras
// sobre el modelo; devuelven false / vacío si no se pudo, sin tocar nada.

#include <nuf/sketch/Sketch.hpp>

#include <map>
#include <optional>
#include <vector>

namespace nuf::sketch {

// Recorta el tramo de la curva (línea, arco o círculo) entre los cortes que
// rodean el clic: cruces con otras curvas y puntos apoyados en ella. Si no
// hay cortes, borra la curva entera. Los extremos nuevos quedan sobre la
// curva que los cortó.
bool trim(Sketch& sketch, EntityId curve, Vec2 click);

// Alarga una línea o un arco por el extremo más cercano al clic hasta la
// primera curva que encuentra.
bool extend(Sketch& sketch, EntityId curve, Vec2 click);

// Parte la curva en los cortes que rodean el clic (sin borrar nada).
bool breakCurve(Sketch& sketch, EntityId curve, Vec2 click);

// Empalme (arco tangente de radio dado) o chaflán (segmento a la distancia
// dada) en la esquina entre dos líneas cuyo extremo común está en «corner».
// Devuelven la entidad nueva.
std::optional<EntityId> fillet(Sketch& sketch, Vec2 corner, double radius, double tolerance = 1e-6);
std::optional<EntityId> chamfer(Sketch& sketch, Vec2 corner, double distance, double tolerance = 1e-6);

// Curvas encadenadas (unidas por sus extremos) a partir de una: las que
// desfasar juntas.
std::vector<EntityId> chainOf(const Sketch& sketch, EntityId curve);

// Desfase de una cadena de líneas, arcos y círculos (o de una elipse, un arco de elipse o una spline, que se aproximan con una spline): a la izquierda del
// sentido de la cadena si distance > 0 (a la derecha si < 0). Las líneas
// quedan paralelas a su original y acotadas a esa distancia; arcos y
// círculos, concéntricos. Devuelve las curvas nuevas.
std::vector<EntityId> offset(Sketch& sketch, const std::vector<EntityId>& chain, double distance);

// Restricciones y cotas que tocan alguna de «entities» (con sus referencias tal como están): se guardan antes de
// sustituir esas entidades (por ejemplo, las letras de un texto que se edita) para volver a ponerlas después.
std::vector<Constraint> constraintsOn(const Sketch& sketch, const std::vector<EntityId>& entities);

// Vuelve a poner las restricciones guardadas con las entidades nuevas (antigua → nueva). Se descarta la que toca una
// entidad sin pareja, la que no se puede poner y la que el solver no admite (en conflicto, redundante o sin
// resolver: por ejemplo, una cota entre dos letras que ya no mide lo mismo). Devuelve cuántas se conservaron.
std::size_t restoreConstraints(Sketch& sketch, const std::vector<Constraint>& saved,
                               const std::map<EntityId, EntityId>& remap);

// Copia simétrica respecto a una línea, con restricciones de simetría.
std::vector<EntityId> mirror(Sketch& sketch, const std::vector<EntityId>& entities, EntityId axis);

// Transformación de semejanza: escala y giro alrededor de pivot, después
// traslación.
struct Similarity {
    Vec2 pivot;
    double angle = 0.0;
    double scale = 1.0;
    Vec2 translation;

    Vec2 apply(Vec2 p) const;
};

// La geometría transformada.
Geometry transformed(const Geometry& geometry, const Similarity& t);

// Mueve (copy = false) o copia las entidades. Las copias conservan las
// restricciones entre ellas; al escalar se escalan sus cotas y al girar se
// quitan las horizontales/verticales que dejarían de cumplirse.
std::vector<EntityId> transform(Sketch& sketch, const std::vector<EntityId>& entities, const Similarity& t,
                                bool copy);

// Patrones: copias (con sus restricciones internas) en filas y columnas, o
// alrededor de un centro repartidas en «totalAngle».
std::vector<EntityId> rectangularPattern(Sketch& sketch, const std::vector<EntityId>& entities, int columns,
                                         int rows, Vec2 step);
std::vector<EntityId> circularPattern(Sketch& sketch, const std::vector<EntityId>& entities, Vec2 center,
                                      int count, double totalAngle);

} // namespace nuf::sketch
