#include "RendererPch.h"
#include "Dx12ResourceManager.h"
#include "Dx12DescriptorPool.h"

namespace dx12
{

ResourceManager::ResourceManager(RenderDevice& device)
    : m_RenderDevice(device)
{
    m_pViewDescriptorPool =
        std::make_unique< DescriptorPool >(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, MAX_NUM_DESCRIPTOR_PER_POOL[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV]);
    m_pRtvDescriptorPool =
        std::make_unique< DescriptorPool >(device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, MAX_NUM_DESCRIPTOR_PER_POOL[D3D12_DESCRIPTOR_HEAP_TYPE_RTV]);
    m_pDsvDescriptorPool =
        std::make_unique< DescriptorPool >(device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, MAX_NUM_DESCRIPTOR_PER_POOL[D3D12_DESCRIPTOR_HEAP_TYPE_DSV]);
}

DescriptorAllocation ResourceManager::AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE type, u32 numDescriptors)
{
    switch (type)
    {
    case D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV:
        return m_pViewDescriptorPool->Allocate(numDescriptors);
    case D3D12_DESCRIPTOR_HEAP_TYPE_RTV:
        return m_pRtvDescriptorPool->Allocate(numDescriptors);
    case D3D12_DESCRIPTOR_HEAP_TYPE_DSV:
        return m_pDsvDescriptorPool->Allocate(numDescriptors);

    default:
        assert(false && "Invalid entry!");
    }

    return DescriptorAllocation();
}

}