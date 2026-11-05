#include <nuf/commands/SetBodyInfo.hpp>

#include <utility>

namespace nuf::commands {

SetBodyInfo::SetBodyInfo(std::map<std::string, document::BodyInfo> info)
    : new_(std::move(info))
{
}

void SetBodyInfo::execute(document::Document& doc)
{
    old_ = doc.bodyInfo();
    doc.setBodyInfo(new_);
}

void SetBodyInfo::undo(document::Document& doc)
{
    if (old_) {
        doc.setBodyInfo(*old_);
    }
}

std::string SetBodyInfo::label() const
{
    return "Nombres y componentes de los cuerpos";
}

} // namespace nuf::commands
