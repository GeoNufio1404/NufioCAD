#pragma once

// Malla para dibujar un cuerpo, preparada en un hilo de trabajo para que la
// interfaz no se pare (una rosca tarda segundos en mallarse y en calcular sus
// normales). El hilo trabaja sobre una copia; la malla se pasa luego, rápido,
// a las caras del cuerpo original.

#include <nuf/geometry/Shape.hpp>

#include <atomic>
#include <memory>

namespace nuf::geometry {

class PreparedMesh {
public:
    struct Impl;
    PreparedMesh();
    explicit PreparedMesh(std::shared_ptr<Impl> impl);
    ~PreparedMesh();
    PreparedMesh(PreparedMesh&&) noexcept;
    PreparedMesh& operator=(PreparedMesh&&) noexcept;

    bool valid() const { return impl_ != nullptr; }
    const Impl* impl() const { return impl_.get(); }

private:
    std::shared_ptr<Impl> impl_;
};

// ¿Tiene alguna cara spline con muchísimos puntos de control (una rosca)?
// Esos cuerpos se mallan con una desviación absoluta gruesa y en segundo plano.
bool hasHeavySurface(const Shape& shape);

// Desviación y ángulo con los que se malla un cuerpo pesado.
constexpr double kHeavyDeviation = 0.1;  // mm
constexpr double kHeavyAngle = 0.5;      // rad

// Se llama desde un hilo de trabajo. Malla una copia del cuerpo y le calcula las
// normales; «cancel» a true la interrumpe (devuelve una malla no válida).
PreparedMesh prepareMesh(const Shape& shape, double deviation, double angle, const std::atomic<bool>& cancel);

// Se llama desde el hilo de la interfaz (solo pasa punteros): deja la malla en
// las caras del cuerpo, de modo que dibujarlo ya no tenga que mallarlo.
void applyMesh(const Shape& shape, const PreparedMesh& mesh);

// ¿Tienen ya todas las caras una malla? (la de applyMesh o la de un dibujo anterior)
bool hasMesh(const Shape& shape);

} // namespace nuf::geometry
