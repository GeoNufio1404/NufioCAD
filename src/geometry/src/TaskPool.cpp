#include <nuf/geometry/TaskPool.hpp>

#include <nuf/geometry/Cancel.hpp>

#include <algorithm>

namespace nuf::geometry {

void TaskPool::Task::markFinished()
{
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        finished_ = true;
    }
    done_.notify_all();
}

void TaskPool::Task::cancel() noexcept
{
    cancel_ = true;
    if (pool_ != nullptr) {
        pool_->dropIfQueued(this);
    }
}

void TaskPool::Task::wait()
{
    std::unique_lock<std::mutex> lock(mutex_);
    done_.wait(lock, [this] { return finished_.load(); });
}

TaskPool& TaskPool::instance()
{
    static TaskPool pool;
    return pool;
}

TaskPool::TaskPool(unsigned maxThreads)
    : maxThreads_(maxThreads != 0 ? maxThreads : std::max(4u, std::thread::hardware_concurrency()))
{
}

TaskPool::~TaskPool()
{
    std::vector<TaskPtr> dropped;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
        for (const TaskPtr& task : running_) {
            task->cancel_ = true;
        }
        for (TaskPtr& task : queue_) {
            task->cancel_ = true;
            dropped.push_back(std::move(task));
        }
        queue_.clear();
    }
    ready_.notify_all();
    for (const TaskPtr& task : dropped) {
        task->work_ = nullptr;
        task->markFinished();
    }
    for (std::thread& worker : workers_) {
        worker.join();
    }
}

TaskPool::TaskPtr TaskPool::submit(std::function<void()> work)
{
    // make_shared no puede usar el constructor privado.
    TaskPtr task(new Task(std::move(work), this));
    bool spawn = false;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) {
            task->cancel_ = true;
            task->work_ = nullptr;
            task->markFinished();
            return task;
        }
        queue_.push_back(task);
        // Un hilo nuevo solo si no hay ninguno libre para esta tarea y no se llegó al máximo.
        spawn = idle_ < queue_.size() && workers_.size() < maxThreads_;
        if (spawn) {
            workers_.emplace_back([this] { workerLoop(); });
        }
    }
    ready_.notify_one();
    return task;
}

std::size_t TaskPool::threadCount() const
{
    const std::lock_guard<std::mutex> lock(mutex_);
    return workers_.size();
}

void TaskPool::dropIfQueued(Task* task)
{
    TaskPtr dropped;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        const auto it = std::ranges::find_if(queue_, [task](const TaskPtr& queued) { return queued.get() == task; });
        if (it == queue_.end()) {
            return;  // ya corre (o ya terminó): se enterará por la bandera
        }
        dropped = std::move(*it);
        queue_.erase(it);
    }
    dropped->work_ = nullptr;  // suelta lo que capturaba, sin esperar a ningún hilo
    dropped->markFinished();
}

void TaskPool::workerLoop()
{
    for (;;) {
        TaskPtr task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            ++idle_;
            ready_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
            --idle_;
            if (queue_.empty()) {
                return;  // cerrando y sin nada pendiente
            }
            task = std::move(queue_.front());
            queue_.pop_front();
            running_.push_back(task);
        }
        if (!task->cancelled()) {
            const CancelScope scope(task->cancel_);
            try {
                task->work_();
            } catch (...) {
                // Cada tarea cuenta sus propios fallos en su resultado.
            }
        }
        task->work_ = nullptr;
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            std::erase(running_, task);
        }
        task->markFinished();
    }
}

} // namespace nuf::geometry
