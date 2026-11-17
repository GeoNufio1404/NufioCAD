#include <nuf/geometry/Cancel.hpp>
#include <nuf/geometry/TaskPool.hpp>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <future>
#include <thread>
#include <vector>

using nuf::geometry::TaskPool;
using namespace std::chrono_literals;

namespace {

// Espera (con tope) a que se cumpla algo que otro hilo tiene que hacer.
template <typename Condition>
bool eventually(Condition condition, std::chrono::milliseconds limit = 5000ms)
{
    const auto end = std::chrono::steady_clock::now() + limit;
    while (!condition()) {
        if (std::chrono::steady_clock::now() > end) {
            return false;
        }
        std::this_thread::sleep_for(1ms);
    }
    return true;
}

} // namespace

TEST_CASE("El grupo de hilos ejecuta las tareas y se puede esperar a cada una", "[taskpool]")
{
    TaskPool pool(4);
    std::atomic<int> sum{0};
    std::vector<TaskPool::TaskPtr> tasks;
    for (int i = 1; i <= 20; ++i) {
        tasks.push_back(pool.submit([&sum, i] { sum += i; }));
    }
    for (const auto& task : tasks) {
        task->wait();
        REQUIRE(task->finished());
        REQUIRE_FALSE(task->cancelled());
    }
    REQUIRE(sum == 210);
}

TEST_CASE("Una tarea cancelada que aún espera en la cola no llega a ejecutarse", "[taskpool]")
{
    TaskPool pool(1);  // un solo hilo: la segunda tarea espera a que acabe la primera
    std::promise<void> release;
    std::shared_future<void> gate = release.get_future().share();
    std::atomic<bool> firstRunning{false};
    std::atomic<bool> secondRan{false};
    const auto first = pool.submit([&] {
        firstRunning = true;
        gate.wait();
    });
    const auto second = pool.submit([&] { secondRan = true; });
    REQUIRE(eventually([&] { return firstRunning.load(); }));

    second->cancel();  // se descarta en el acto, sin esperar a que se libere el hilo
    REQUIRE(second->finished());
    second->wait();
    release.set_value();
    first->wait();
    REQUIRE_FALSE(secondRan);
}

TEST_CASE("Una tarea en marcha ve la petición de cancelar con cancelRequested", "[taskpool]")
{
    TaskPool pool(2);
    std::atomic<bool> started{false};
    std::atomic<bool> sawCancel{false};
    const auto task = pool.submit([&] {
        started = true;
        const auto end = std::chrono::steady_clock::now() + 5s;
        while (!nuf::geometry::cancelRequested() && std::chrono::steady_clock::now() < end) {
            std::this_thread::sleep_for(1ms);
        }
        sawCancel = nuf::geometry::cancelRequested();
    });
    REQUIRE(eventually([&] { return started.load(); }));
    task->cancel();
    task->wait();
    REQUIRE(sawCancel);
    REQUIRE(task->cancelled());
}

TEST_CASE("Los hilos se crean solo cuando hacen falta y no pasan del máximo", "[taskpool]")
{
    TaskPool pool(3);
    REQUIRE(pool.threadCount() == 0);
    pool.submit([] {})->wait();
    REQUIRE(pool.threadCount() == 1);

    std::promise<void> release;
    std::shared_future<void> gate = release.get_future().share();
    std::atomic<int> running{0};
    std::vector<TaskPool::TaskPtr> tasks;
    for (int i = 0; i < 10; ++i) {
        tasks.push_back(pool.submit([&] {
            ++running;
            gate.wait();
        }));
    }
    REQUIRE(eventually([&] { return running.load() == 3; }));
    REQUIRE(pool.threadCount() == 3);
    release.set_value();
    for (const auto& task : tasks) {
        task->wait();
    }
    REQUIRE(running == 10);
    REQUIRE(pool.threadCount() == 3);
}

TEST_CASE("Al destruir el grupo se cancela lo que corre y lo que espera, y no queda ningún hilo", "[taskpool]")
{
    std::atomic<bool> started{false};
    std::atomic<bool> queuedRan{false};
    TaskPool::TaskPtr running;
    TaskPool::TaskPtr queued;
    {
        TaskPool pool(1);
        running = pool.submit([&] {
            started = true;
            const auto end = std::chrono::steady_clock::now() + 5s;
            while (!nuf::geometry::cancelRequested() && std::chrono::steady_clock::now() < end) {
                std::this_thread::sleep_for(1ms);
            }
        });
        queued = pool.submit([&] { queuedRan = true; });
        REQUIRE(eventually([&] { return started.load(); }));
    }  // el destructor cancela, espera y recoge los hilos
    REQUIRE(running->finished());
    REQUIRE(queued->finished());
    REQUIRE(running->cancelled());
    REQUIRE_FALSE(queuedRan);
}

TEST_CASE("Una excepción dentro de una tarea no tumba el hilo ni el programa", "[taskpool]")
{
    TaskPool pool(2);
    const auto throwing = pool.submit([] { throw 42; });
    throwing->wait();
    REQUIRE(throwing->finished());
    std::atomic<bool> ran{false};
    pool.submit([&] { ran = true; })->wait();
    REQUIRE(ran);
}
