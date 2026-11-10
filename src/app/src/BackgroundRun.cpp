#include "BackgroundRun.hpp"

#include <nuf/geometry/Cancel.hpp>

#include <QCoreApplication>
#include <QMetaObject>

#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

namespace nuf::app {

struct BackgroundTask {
    std::atomic<bool> cancel{false};
    std::atomic<bool> finished{false};
    std::atomic<bool> abandoned{false};  // su dueño ya no existe: no hay a quién avisar
    std::function<void()> done;
    std::thread thread;
};

namespace {

// Los cálculos descartados que aún no han terminado: no se espera por ellos (no se cuelga la interfaz), se
// recogen al llegar otro o al cerrar.
class Discarded {
public:
    ~Discarded()
    {
        for (auto& task : tasks_) {
            task->cancel = true;
            if (task->thread.joinable()) {
                task->thread.join();
            }
        }
    }

    void add(std::shared_ptr<BackgroundTask> task)
    {
        std::lock_guard lock(mutex_);
        tasks_.push_back(std::move(task));
        std::erase_if(tasks_, [](const std::shared_ptr<BackgroundTask>& t) {
            if (!t->finished.load()) {
                return false;
            }
            if (t->thread.joinable()) {
                t->thread.join();
            }
            return true;
        });
    }

private:
    std::mutex mutex_;
    std::vector<std::shared_ptr<BackgroundTask>> tasks_;
};

Discarded gDiscarded;

} // namespace

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
        current_->cancel = true;
        gDiscarded.add(std::move(current_));
        current_.reset();
    }
}

void BackgroundRun::start(std::function<void()> work, std::function<void()> done)
{
    cancel();
    auto task = std::make_shared<BackgroundTask>();
    task->done = std::move(done);
    BackgroundTask* raw = task.get();
    const std::weak_ptr<BackgroundTask> weak = task;
    BackgroundRun* owner = this;
    current_ = task;
    raw->thread = std::thread([raw, weak, owner, work = std::move(work)] {
        geometry::CancelScope scope(raw->cancel);
        try {
            work();
        } catch (...) {
            // Lo que falle dentro lo cuenta el propio «work» en su resultado.
        }
        raw->finished = true;
        if (raw->cancel.load()) {
            return;
        }
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            [weak, owner] {
                const auto alive = weak.lock();
                if (alive && !alive->abandoned.load() && !alive->cancel.load()) {
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
    if (mine->thread.joinable()) {
        mine->thread.join();
    }
    const auto done = std::move(mine->done);
    if (done) {
        done();
    }
}

} // namespace nuf::app
