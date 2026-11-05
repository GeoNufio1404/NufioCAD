#pragma once

#include <nuf/commands/Command.hpp>
#include <nuf/document/Document.hpp>

#include <optional>
#include <vector>

namespace nuf::commands {

// Cambia la tabla de parámetros (nombres y expresiones) del documento.
class SetParameters final : public Command {
public:
    explicit SetParameters(std::vector<document::Parameter> parameters);

    void execute(document::Document& doc) override;
    void undo(document::Document& doc) override;
    std::string label() const override;

private:
    std::vector<document::Parameter> new_;
    std::optional<std::vector<document::Parameter>> old_;
};

} // namespace nuf::commands
