#pragma once

#include <nuf/commands/Command.hpp>
#include <nuf/document/Document.hpp>

#include <map>
#include <optional>
#include <string>

namespace nuf::commands {

// Cambia los nombres propios y los componentes de los cuerpos.
class SetBodyInfo final : public Command {
public:
    explicit SetBodyInfo(std::map<std::string, document::BodyInfo> info);

    void execute(document::Document& doc) override;
    void undo(document::Document& doc) override;
    std::string label() const override;

private:
    std::map<std::string, document::BodyInfo> new_;
    std::optional<std::map<std::string, document::BodyInfo>> old_;
};

} // namespace nuf::commands
