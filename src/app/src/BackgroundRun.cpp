#include "BackgroundRun.hpp"

#include <nuf/geometry/Cancel.hpp>
#include <nuf/geometry/TaskPool.hpp>

#include <QCoreApplication>
#include <QMetaObject>

#include <atomic>

namespace nuf::app {

struct BackgroundTask {
    geometry::TaskPool::TaskPtr task;     // la tarea en el grupo de hilos
    std::atomic<bool> abandoned{false};   // su dueño ya no existe: no hay a quién avisar
    std::function<void()> done;
};

BackgroundRun::~BackgroundRun()
{
    if (current_) {
        current_->abandoned = true;
        cancel();
    }
}

void BackgroundRun::cancel()
{
    if (current_) {
        // Si aún esperaba en la cola no llega a ejecutarse; si ya corría, se levanta su bandera y las construcciones
        // largas se abandonan (el hilo sigue en el grupo, no hay que esperarlo ni recogerlo).
        current_->task->cancel();
        current_.reset();
    }
}

void BackgroundRun::start(std::function<void()> work, std::function<void()> done)
{
    cancel();
    auto task = std::make_shared<BackgroundTask>();
    task->done = std::move(done);
    const std::weak_ptr<BackgroundTask> weak = task;
    BackgroundRun* owner = this;
    current_ = task;
    task->task = geometry::TaskPool::instance().submit([weak, owner, work = std::move(work)] {
        try {
            work();
        } catch (...) {
            // Lo que falle dentro lo cuenta el propio «work» en su resultado.
        }
        if (geometry::cancelRequested()) {
            return;
        }
        QCoreApplication* app = QCoreApplication::instance();
        if (app == nullptr) {
            return;  // el programa ya se está cerrando
        }
        QMetaObject::invokeMethod(
            app,
            [weak, owner] {
                const auto alive = weak.lock();
                if (alive && !alive->abandoned.load() && !alive->task->cancelled()) {
                    owner->finish(alive);
                }
            },
            Qt::QueuedConnection);
    });
}

void BackgroundRun::finish(const std::shared_ptr<BackgroundTask>& task)
{
    if (current_ != task) {
        return;
    }
    const std::shared_ptr<BackgroundTask> mine = std::move(current_);
    current_.reset();
    const auto done = std::move(mine->done);
    if (done) {
        done();
    }
}

} // namespace nuf::app
