#include "RendererPch.h"
#include "Dx12ResourceManager.h"
#include "Dx12DescriptorPool.h"
#include "RenderResource/Dx12Shader.h"

namespace dx12
{

ResourceManager::ResourceManager(RenderContext& context)
    : m_RenderContext(context)
{
    for (u32 i = 0; i < D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES; ++i)
        m_pDescriptorPools[i] = 
            new DescriptorPool(context, (D3D12_DESCRIPTOR_HEAP_TYPE)i, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, MAX_NUM_DESCRIPTOR_PER_POOL[i]);
}

ResourceManager::~ResourceManager()
{
    m_BufferPool.Release();
    m_TexturePool.Release();
    m_SamplerPool.Release();
    m_ShaderPool.Release();

    for (u32 i = 0; i < D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES; ++i)
        RELEASE(m_pDescriptorPools[i]);
}

DescriptorAllocation ResourceManager::AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE type, u32 numDescriptors)
{
    return m_pDescriptorPools[type]->Allocate(numDescriptors);
}

}