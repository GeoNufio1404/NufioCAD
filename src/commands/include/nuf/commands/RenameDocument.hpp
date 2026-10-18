#pragma once

#include <nuf/commands/Command.hpp>

#include <optional>
#include <string>

namespace nuf::commands {

// Primer comando real; sirve de modelo para los demás (patrón memento):
// guarda el valor anterior al ejecutar y lo restaura al deshacer.
class RenameDocument final : public Command {
public:
    explicit RenameDocument(std::string newName);

    void execute(document::Document& doc) override;
    void undo(document::Document& doc) override;
    std::string label() const override;

private:
    std::string newName_;
    std::optional<std::string> oldName_;
};

} // namespace nuf::commands
