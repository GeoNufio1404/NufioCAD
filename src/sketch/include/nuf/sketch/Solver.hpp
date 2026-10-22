#pragma once

// Solver de restricciones del sketch. La interfaz no expone el solver
// concreto (PlaneGCS), para poder cambiarlo sin tocar el resto.

#include <nuf/sketch/Sketch.hpp>

#include <memory>
#include <vector>

namespace nuf::sketch {

enum class SolveStatus {
    Solved,       // la geometría cumple todas las restricciones
    Conflicting,  // hay restricciones incompatibles; la geometría no cambia
    Failed,       // el solver no convergió; la geometría no cambia
};

struct SolveResult {
    SolveStatus status = SolveStatus::Failed;
    // Grados de libertad restantes (0 = totalmente restringido).
    int degreesOfFreedom = 0;
    // Restricciones incompatibles entre sí: hay que quitar alguna.
    std::vector<ConstraintId> conflicting;
    // Restricciones que sobran (ya las implican otras); el sketch se
    // resuelve igual, pero conviene avisar.
    std::vector<ConstraintId> redundant;
    // Entidades que aún pueden moverse (para pintarlas como no restringidas).
    std::vector<EntityId> underconstrained;
};

// Hacia dónde se arrastra algo: un punto (Ref a un punto) hasta target, o
// el contorno de un círculo/arco (Ref::of) para que pase por target.
struct DragTarget {
    Ref what;
    Vec2 target;
};

class ISolver {
public:
    virtual ~ISolver() = default;

    // Mueve la geometría del sketch para cumplir las restricciones y
    // actualiza el valor de las cotas de referencia.
    virtual SolveResult solve(Sketch& sketch) = 0;

    // Como solve, pero además acerca lo arrastrado a su destino tanto como
    // las restricciones lo permitan (lo que está fijado no se mueve).
    virtual SolveResult drag(Sketch& sketch, const std::vector<DragTarget>& targets) = 0;
};

std::unique_ptr<ISolver> makePlaneGcsSolver();

} // namespace nuf::sketch
