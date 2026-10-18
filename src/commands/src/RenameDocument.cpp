#include <nuf/commands/RenameDocument.hpp>

#include <nuf/document/Document.hpp>

#include <utility>

namespace nuf::commands {

RenameDocument::RenameDocument(std::string newName)
    : newName_(std::move(newName))
{
}

void RenameDocument::execute(document::Document& doc)
{
    oldName_ = doc.name();
    doc.setName(newName_);
}

void RenameDocument::undo(document::Document& doc)
{
    if (oldName_) {
        doc.setName(*oldName_);
    }
}

std::string RenameDocument::label() const
{
    return "Renombrar diseño";
}

} // namespace nuf::commands
