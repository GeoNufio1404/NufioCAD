#pragma once

// Un solo grupo de hilos de trabajo para todo lo que se calcula fuera del hilo de la interfaz: el recálculo del
// historial, las vistas previas de las herramientas y las mallas del visor. Antes cada uno abría un hilo propio por
// tarea, los cancelaba a su manera y los recogía con un «join». Aquí:
//  - los hilos se crean solo cuando hacen falta (hasta un máximo) y se reutilizan;
//  - cancelar una tarea que aún espera en la cola la descarta sin llegar a ejecutarla;
//  - una tarea en marcha ve la petición con geometry::cancelRequested() (las construcciones largas la miran);
//  - al cerrar el programa se cancela todo y se espera a los hilos, sin dejar nada suelto.
// No usa Qt: cada quien avisa a su hilo de interfaz con lo suyo, desde dentro de la tarea.

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace nuf::geometry {

class TaskPool {
public:
    class Task {
    public:
        // Pide cancelar: si aún espera en la cola se descarta sin ejecutarse; si ya corre, se levanta la bandera que
        // miran geometry::cancelRequested() y las construcciones largas.
        void cancel() noexcept;
        bool cancelled() const noexcept { return cancel_.load(); }
        // true cuando terminó de ejecutarse o se descartó.
        bool finished() const noexcept { return finished_.load(); }
        // Espera a que termine o se descarte. No llamar desde dentro de una tarea del mismo grupo.
        void wait();

    private:
        friend class TaskPool;
        explicit Task(std::function<void()> work, TaskPool* pool)
            : work_(std::move(work))
            , pool_(pool)
        {
        }
        void markFinished();

        std::function<void()> work_;
        TaskPool* pool_;
        std::atomic<bool> cancel_{false};
        std::atomic<bool> finished_{false};
        std::mutex mutex_;
        std::condition_variable done_;
    };
    using TaskPtr = std::shared_ptr<Task>;

    // El grupo de todo el programa (se crea al primer uso; al cerrar, cancela y espera).
    static TaskPool& instance();

    // «maxThreads»: cuántos hilos como máximo (por defecto, los núcleos del equipo, mínimo 4).
    explicit TaskPool(unsigned maxThreads = 0);
    ~TaskPool();
    TaskPool(const TaskPool&) = delete;
    TaskPool& operator=(const TaskPool&) = delete;

    // Encola «work»; corre en un hilo del grupo con un CancelScope de la bandera de la tarea.
    TaskPtr submit(std::function<void()> work);

    // Cuántos hilos se han creado hasta ahora (para las pruebas).
    std::size_t threadCount() const;

private:
    void workerLoop();
    // Saca de la cola (y da por terminada) una tarea cancelada que aún no empezó.
    void dropIfQueued(Task* task);

    mutable std::mutex mutex_;
    std::condition_variable ready_;
    std::deque<TaskPtr> queue_;
    std::vector<TaskPtr> running_;
    std::vector<std::thread> workers_;
    unsigned maxThreads_;
    unsigned idle_ = 0;
    bool stopping_ = false;
};

} // namespace nuf::geometry
