#include <nuf/commands/CommandStack.hpp>

#include <stdexcept>
#include <utility>

namespace nuf::commands {

CommandStack::CommandStack(document::Document& doc)
    : doc_(doc)
{
}

void CommandStack::push(std::unique_ptr<Command> command)
{
    if (!command) {
        throw std::invalid_argument("CommandStack::push recibió un comando nulo");
    }
    command->execute(doc_);
    done_.push_back(std::move(command));
    undone_.clear();
    changed.notify();
}

void CommandStack::undo()
{
    if (done_.empty()) {
        return;
    }
    auto command = std::move(done_.back());
    done_.pop_back();
    command->undo(doc_);
    undone_.push_back(std::move(command));
    changed.notify();
}

void CommandStack::redo()
{
    if (undone_.empty()) {
        return;
    }
    auto command = std::move(undone_.back());
    undone_.pop_back();
    command->execute(doc_);
    done_.push_back(std::move(command));
    changed.notify();
}

std::string CommandStack::undoLabel() const
{
    return done_.empty() ? std::string{} : done_.back()->label();
}

std::string CommandStack::redoLabel() const
{
    return undone_.empty() ? std::string{} : undone_.back()->label();
}

void CommandStack::clear() noexcept
{
    done_.clear();
    undone_.clear();
    changed.notify();
}

} // namespace nuf::commands
