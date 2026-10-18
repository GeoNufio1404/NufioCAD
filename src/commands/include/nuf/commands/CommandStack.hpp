#pragma once

#include <nuf/commands/Command.hpp>
#include <nuf/document/Signal.hpp>

#include <memory>
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

    void clear() noexcept;

    // Se notifica tras push, undo, redo y clear (para actualizar menús).
    Signal<> changed;

private:
    document::Document& doc_;
    std::vector<std::unique_ptr<Command>> done_;
    std::vector<std::unique_ptr<Command>> undone_;
};

} // namespace nuf::commands
