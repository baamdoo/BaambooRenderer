#include "Pointer.hpp"

namespace ptr_detail
{

struct ControlBlock
{
	std::atomic< uint32_t > strongCount{ 0 };
	std::atomic< uint32_t > weakCount{ 1 };
};


bool TryAddStrong(ControlBlock* pControlBlock) noexcept
{
	uint32_t count = pControlBlock->strongCount.load(std::memory_order_acquire);
	while (count != 0)
	{
		assert(count != std::numeric_limits< uint32_t >::max());

		if (pControlBlock->strongCount.compare_exchange_weak(
			count,
			count + 1,
			std::memory_order_acquire,
			std::memory_order_relaxed))
		{
			return true;
		}
	}

	return false;
}

void AddWeak(ControlBlock* pControlBlock) noexcept
{
	const uint32_t previous = pControlBlock->weakCount.fetch_add(1, std::memory_order_relaxed);

	assert(previous > 0); // asserts if the arc exists.
	if (previous >= std::numeric_limits< uint32_t >::max())
	{
		__debugbreak();
	}
}

void ReleaseWeak(ControlBlock* pControlBlock) noexcept
{
	const uint32_t previous = pControlBlock->weakCount.fetch_sub(1, std::memory_order_acq_rel);
	assert(previous > 0);

	if (previous == 1)
		delete pControlBlock;
}

bool IsValid(ControlBlock* pControlBlock) noexcept
{
	return pControlBlock && pControlBlock->strongCount.load(std::memory_order_acquire) != 0;
}

} // namespace ptr_detail


ArcBase::ArcBase()
	: m_pControlBlock(new ptr_detail::ControlBlock)
{
}

ArcBase::~ArcBase()
{
	assert(RefCount() == 0);

	ptr_detail::ReleaseWeak(m_pControlBlock);
	m_pControlBlock = nullptr;
}

void ArcBase::AddRef() const noexcept
{
	const uint32_t previous = m_pControlBlock->strongCount.fetch_add(1, std::memory_order_relaxed);
	if (previous >= std::numeric_limits< uint32_t >::max())
	{
		__debugbreak();
	}
}

bool ArcBase::ReleaseRef() const noexcept
{
	const uint32_t previous = m_pControlBlock->strongCount.fetch_sub(1, std::memory_order_acq_rel);

	assert(previous > 0);
	return previous == 1;
}

uint32_t ArcBase::RefCount() const noexcept
{
	return m_pControlBlock->strongCount.load(std::memory_order_relaxed);
}
