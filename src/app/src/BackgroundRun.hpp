#pragma once

#include <functional>
#include <memory>

namespace nuf::app {

struct BackgroundTask;

// Un cálculo largo (la vista previa de una herramienta) hecho en un hilo de trabajo: la interfaz sigue
// viva y, si llega otro encargo antes de que acabe, el anterior se cancela y su resultado se descarta.
// «work» corre en el hilo de trabajo (con las booleanas interrumpibles) y solo debe usar copias;
// «done» corre en el hilo de la interfaz, y solo si nadie lo canceló ni se destruyó este objeto.
class BackgroundRun {
public:
    BackgroundRun() = default;
    ~BackgroundRun();
    BackgroundRun(const BackgroundRun&) = delete;
    BackgroundRun& operator=(const BackgroundRun&) = delete;

    void start(std::function<void()> work, std::function<void()> done);
    // Descarta lo que corre (su resultado no se usará).
    void cancel();
    bool running() const noexcept { return current_ != nullptr; }

private:
    void finish(const std::shared_ptr<BackgroundTask>& task);

    std::shared_ptr<BackgroundTask> current_;
};

} // namespace nuf::app
