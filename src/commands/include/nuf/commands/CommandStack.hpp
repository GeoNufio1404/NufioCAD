#pragma once

#include <nuf/commands/Command.hpp>
#include <nuf/document/Signal.hpp>

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace nuf::commands {

// Pila de deshacer/rehacer de un documento.
class CommandStack {
public:
    explicit CommandStack(document::Document& doc);

    // Ejecuta el comando y lo apila. Borra lo que se podía rehacer.
    void push(std::unique_ptr<Command> command);

    bool canUndo() const noexcept { return !done_.empty(); }
    bool canRedo() const noexcept { return !undone_.empty(); }

    void undo();
    void redo();

    // Etiquetas para los menús ("Deshacer Editar Extrusión2").
    std::string undoLabel() const;
    std::string redoLabel() const;

    // Vacía las dos pilas (al abrir otro diseño); el punto guardado se pierde.
    void clear() noexcept;

    // Marca el estado actual como el guardado. isClean() es true mientras se
    // esté en ese punto del historial (también tras deshacer o rehacer hasta
    // él) y false si el punto quedó en una rama de rehacer descartada.
    void markSaved() noexcept { savedDepth_ = done_.size(); }
    bool isClean() const noexcept { return savedDepth_ && *savedDepth_ == done_.size(); }

    // Se notifica tras push, undo, redo y clear (para actualizar menús).
    Signal<> changed;

private:
    document::Document& doc_;
    std::vector<std::unique_ptr<Command>> done_;
    std::vector<std::unique_ptr<Command>> undone_;
    std::optional<std::size_t> savedDepth_ = 0;  // comandos hechos al guardar
};

} // namespace nuf::commands
