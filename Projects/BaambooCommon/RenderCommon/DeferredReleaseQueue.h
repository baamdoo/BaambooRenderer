#pragma once
#include "Defines.h"
#include "Primitives.h"

#include <deque>
#include <functional>

#pragma warning(disable : 4251)

namespace render
{

class BAAMBOO_API DeferredReleaseQueue
{
public:
    using ReleaseFn = std::move_only_function< void() >;

    DeferredReleaseQueue() = default;
    DeferredReleaseQueue(const DeferredReleaseQueue&) = delete;
    DeferredReleaseQueue& operator=(const DeferredReleaseQueue&) = delete;

    void Retire(u64 frame, ReleaseFn&& fnRelease);
    void Reclaim(u64 currentFrame);

private:
    struct Item
    {
        u64       frame;
        ReleaseFn fnRelease;
    };
    std::deque< Item > m_Items;
};

} // namespace render
