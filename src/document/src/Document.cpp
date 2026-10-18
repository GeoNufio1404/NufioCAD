#include <nuf/document/Document.hpp>

#include <utility>

namespace nuf::document {

void Document::setName(std::string name)
{
    if (name == name_) {
        return;
    }
    name_ = std::move(name);
    markChanged();
}

void Document::markChanged()
{
    ++revision_;
    changed.notify();
}

} // namespace nuf::document
