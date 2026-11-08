#pragma once

#include <nuf/commands/Command.hpp>
#include <nuf/document/Document.hpp>

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace nuf::commands {

// Cambia la tabla de parámetros (nombres y expresiones) del documento. Si se
// pasan renombrados (nombre antiguo → nuevo), las expresiones de las operaciones
// que los usan se actualizan con ellos (las de los parámetros ya vienen hechas).
class SetParameters final : public Command {
public:
    explicit SetParameters(std::vector<document::Parameter> parameters, std::map<std::string, std::string> renames = {});

    void execute(document::Document& doc) override;
    void undo(document::Document& doc) override;
    std::string label() const override;

private:
    std::vector<document::Parameter> new_;
    std::map<std::string, std::string> renames_;
    std::optional<std::vector<document::Parameter>> old_;
    document::Document::ExpressionSnapshot oldExpressions_;
};

} // namespace nuf::commands
