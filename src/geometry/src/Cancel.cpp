#include <nuf/geometry/Cancel.hpp>

namespace nuf::geometry {

namespace {
thread_local const std::atomic<bool>* gFlag = nullptr;
}

CancelScope::CancelScope(const std::atomic<bool>& flag) noexcept
    : previous_(gFlag)
{
    gFlag = &flag;
}

CancelScope::~CancelScope()
{
    gFlag = previous_;
}

bool cancelRequested() noexcept
{
    return gFlag != nullptr && gFlag->load();
}

} // namespace nuf::geometry
