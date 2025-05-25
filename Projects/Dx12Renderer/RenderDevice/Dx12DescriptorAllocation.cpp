#include "RendererPch.h"
#include "Dx12DescriptorAllocation.h"
#include "Dx12RenderContext.h"
#include "Dx12DescriptorPool.h"

namespace dx12
{

DescriptorAllocation::DescriptorAllocation()
{
}

DescriptorAllocation::DescriptorAllocation(DescriptorPool* pool, D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle, D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle, u32 numDescriptors, u32 descriptorIndex)
	: m_pDescriptorPool(pool)
	, m_CPUHandle(cpuHandle)
	, m_GPUHandle(gpuHandle)
	, m_NumDescriptors(numDescriptors)
	, m_DescriptorBaseIndex(descriptorIndex)
{
}

DescriptorAllocation::DescriptorAllocation(DescriptorAllocation&& other) noexcept
	: m_pDescriptorPool(other.m_pDescriptorPool)
	, m_CPUHandle(other.m_CPUHandle)
	, m_GPUHandle(other.m_GPUHandle)
	, m_NumDescriptors(other.m_NumDescriptors)
	, m_DescriptorBaseIndex(other.m_DescriptorBaseIndex)
{
	other.m_pDescriptorPool = nullptr;
	other.m_CPUHandle = {};
	other.m_GPUHandle = {};
	other.m_NumDescriptors = 0;
	other.m_DescriptorBaseIndex = 0;
}

DescriptorAllocation& DescriptorAllocation::operator=(DescriptorAllocation&& other) noexcept
{
	if (this != &other)
	{
		Free();

		m_pDescriptorPool = other.m_pDescriptorPool;
		m_CPUHandle = other.m_CPUHandle;
		m_GPUHandle = other.m_GPUHandle;
		m_NumDescriptors = other.m_NumDescriptors;
		m_DescriptorBaseIndex = other.m_DescriptorBaseIndex;

		other.m_pDescriptorPool = nullptr;
		other.m_CPUHandle = {};
		other.m_GPUHandle = {};
		other.m_NumDescriptors = 0;
		other.m_DescriptorBaseIndex = 0;
	}

	return *this;
}

DescriptorAllocation::~DescriptorAllocation()
{
	Free();
}

void DescriptorAllocation::Free()
{
	if (m_pDescriptorPool)
	{
		m_pDescriptorPool->Free(*this);

		m_pDescriptorPool = nullptr;
		m_CPUHandle = {};
		m_GPUHandle = {};
		m_NumDescriptors = 0;
		m_DescriptorBaseIndex = 0;
	}
}

D3D12_CPU_DESCRIPTOR_HANDLE DescriptorAllocation::GetCPUHandle(u32 offset) const
{
	assert(offset < m_NumDescriptors);
	return CD3DX12_CPU_DESCRIPTOR_HANDLE(m_CPUHandle, offset, m_pDescriptorPool->GetDescriptorSize());
}

D3D12_GPU_DESCRIPTOR_HANDLE DescriptorAllocation::GetGPUHandle(u32 offset) const
{
	assert(offset < m_NumDescriptors);
	BB_ASSERT(m_GPUHandle.ptr != 0, "This allocation type doesn't support gpu handle!");

	return CD3DX12_GPU_DESCRIPTOR_HANDLE(m_GPUHandle, offset, m_pDescriptorPool->GetDescriptorSize());
}

}