#pragma once

#include <atomic>

namespace nuf::geometry {

// Permite interrumpir un cálculo largo hecho en un hilo de trabajo (una booleana de una rosca,
// por ejemplo): mientras exista un CancelScope en ese hilo, las operaciones que lo soportan
// miran su bandera y se abandonan con un GeometryError cuando se levanta. En el hilo de la
// interfaz no hay ninguno, así que allí nada se interrumpe.
class CancelScope {
public:
    explicit CancelScope(const std::atomic<bool>& flag) noexcept;
    ~CancelScope();
    CancelScope(const CancelScope&) = delete;
    CancelScope& operator=(const CancelScope&) = delete;

private:
    const std::atomic<bool>* previous_;
};

// true si el cálculo de este hilo se pidió cancelar.
bool cancelRequested() noexcept;

} // namespace nuf::geometry
