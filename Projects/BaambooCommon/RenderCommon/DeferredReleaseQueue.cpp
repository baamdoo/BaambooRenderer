#include "DeferredReleaseQueue.h"

namespace render
{

void DeferredReleaseQueue::Retire(u64 frame, ReleaseFn&& fnRelease)
{
	m_Items.push_back({ frame, std::move(fnRelease) });
}

void DeferredReleaseQueue::Reclaim(u64 currentFrame)
{
	u32 numRemoves = 0u;
	for (auto& item : m_Items)
	{
		if (item.frame + kMaxFramesInFlight > currentFrame)
			break;

		item.fnRelease();
		numRemoves++;
	}

	m_Items.erase(m_Items.begin(), m_Items.begin() + numRemoves);
}

} // namespace render
