#pragma once

#include <string>

namespace nuf::document {
class Document;
}

namespace nuf::commands {

// Un cambio reversible al documento.
// Implementación recomendada (memento): guardar los parámetros afectados
// antes y después, y restaurarlos en undo(). Nunca guardar geometría.
class Command {
public:
    virtual ~Command() = default;

    virtual void execute(document::Document& doc) = 0;
    virtual void undo(document::Document& doc) = 0;

    // Texto para la interfaz, ej. "Editar Extrusión2".
    virtual std::string label() const = 0;
};

} // namespace nuf::commands
