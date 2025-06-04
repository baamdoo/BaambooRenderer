#pragma once

namespace dx12
{

class DescriptorPool;

class ResourceManager
{
public:
    ResourceManager(RenderDevice& device);
    ~ResourceManager() = default;

    [[nodiscard]]
    DescriptorAllocation AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE type, u32 numDescriptors = 1);

private:
    RenderDevice& m_RenderDevice;

    Box< DescriptorPool > m_pViewDescriptorPool;
    Box< DescriptorPool > m_pRtvDescriptorPool;
    Box< DescriptorPool > m_pDsvDescriptorPool;
};

}
