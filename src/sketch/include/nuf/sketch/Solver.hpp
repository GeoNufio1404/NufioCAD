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

class ISolver {
public:
    virtual ~ISolver() = default;

    // Mueve la geometría del sketch para cumplir las restricciones y
    // actualiza el valor de las cotas de referencia.
    virtual SolveResult solve(Sketch& sketch) = 0;
};

std::unique_ptr<ISolver> makePlaneGcsSolver();

} // namespace nuf::sketch
