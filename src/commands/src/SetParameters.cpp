#include <nuf/commands/SetParameters.hpp>

#include <utility>

namespace nuf::commands {

SetParameters::SetParameters(std::vector<document::Parameter> parameters, std::map<std::string, std::string> renames)
    : new_(std::move(parameters))
    , renames_(std::move(renames))
{
}

void SetParameters::execute(document::Document& doc)
{
    old_ = doc.parameters();
    oldExpressions_ = doc.expressionSnapshot();
    doc.setParameters(new_, renames_);
}

void SetParameters::undo(document::Document& doc)
{
    if (old_) {
        doc.restoreParameters(*old_, oldExpressions_);
    }
}

std::string SetParameters::label() const
{
    return renames_.empty() ? "Editar parámetros" : "Renombrar parámetros";
}

} // namespace nuf::commands
