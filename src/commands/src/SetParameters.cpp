#include <nuf/commands/SetParameters.hpp>

#include <utility>

namespace nuf::commands {

SetParameters::SetParameters(std::vector<document::Parameter> parameters)
    : new_(std::move(parameters))
{
}

void SetParameters::execute(document::Document& doc)
{
    old_ = doc.parameters();
    doc.setParameters(new_);
}

void SetParameters::undo(document::Document& doc)
{
    if (old_) {
        doc.setParameters(*old_);
    }
}

std::string SetParameters::label() const
{
    return "Editar parámetros";
}

} // namespace nuf::commands
