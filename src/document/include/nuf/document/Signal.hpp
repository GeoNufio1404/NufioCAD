#pragma once

// Señal mínima para notificar cambios sin depender de Qt.
// Nota: el método se llama notify() y no emit() porque Qt define la macro
// `emit`, y este archivo también se incluye desde la interfaz.

#include <algorithm>
#include <cstddef>
#include <functional>
#include <utility>
#include <vector>

namespace nuf {

template <typename... Args>
class Signal {
public:
    using Slot = std::function<void(Args...)>;
    using Connection = std::size_t;

    Connection connect(Slot slot)
    {
        const Connection id = nextId_++;
        slots_.emplace_back(id, std::move(slot));
        return id;
    }

    void disconnect(Connection id)
    {
        std::erase_if(slots_, [id](const auto& entry) { return entry.first == id; });
    }

    // Copia la lista antes de llamar, para que un receptor pueda
    // desconectarse durante la notificación sin invalidar el recorrido.
    void notify(Args... args) const
    {
        const auto snapshot = slots_;
        for (const auto& [id, slot] : snapshot) {
            slot(args...);
        }
    }

    std::size_t connectionCount() const noexcept { return slots_.size(); }

private:
    std::vector<std::pair<Connection, Slot>> slots_;
    Connection nextId_ = 1;
};

} // namespace nuf
