#pragma once
#include "Core/Dx12ResourcePool.h"
#include "RenderResource/Dx12Shader.h"
#include "RenderResource/Dx12Buffer.h"
#include "RenderResource/Dx12Texture.h"
#include "RenderResource/Dx12Sampler.h"

#include <BaambooCore/ResourceHandle.h>

namespace dx12
{

class ResourceManager
{
public:
    ResourceManager(RenderContext& context);
    virtual ~ResourceManager();

    template< typename TResource >
    baamboo::ResourceHandle< TResource > Create(std::wstring_view name, typename TResource::CreationInfo&& info);
    template< typename TResource >
    TResource* CreateEmpty(std::wstring_view name);

    template< typename TResource >
    baamboo::ResourceHandle< TResource > Add(TResource* pResource);

    template< typename TResource >
    TResource* Get(baamboo::ResourceHandle< TResource > handle) const;

    template< typename TResource >
    void Remove(baamboo::ResourceHandle< TResource > handle);

    [[nodiscard]]
    DescriptorAllocation AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE type, u32 numDescriptors = 1);

private:
    RenderContext& m_RenderContext;

    ResourcePool< VertexBuffer >     m_VertexBufferPool;
    ResourcePool< IndexBuffer >      m_IndexBufferPool;
    ResourcePool< ConstantBuffer >   m_ConstantBufferPool;
    ResourcePool< StructuredBuffer > m_StructuredBufferPool;

    ResourcePool< Texture > m_TexturePool;
    ResourcePool< Sampler > m_SamplerPool;
    ResourcePool< Shader >  m_ShaderPool;

    class DescriptorPool* m_pDescriptorPools[D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES] = {};
};

template< typename TResource >
baamboo::ResourceHandle< TResource > ResourceManager::Create(std::wstring_view name, typename TResource::CreationInfo&& info)
{
    static_assert(std::is_base_of_v< Resource, TResource >);

    if constexpr (std::is_same_v< TResource, VertexBuffer > )
        return m_VertexBufferPool.Create(m_RenderContext, name, std::move(info));
    if constexpr (std::is_same_v< TResource, IndexBuffer >)
        return m_IndexBufferPool.Create(m_RenderContext, name, std::move(info));
    if constexpr (std::is_same_v< TResource, ConstantBuffer >)
        return m_ConstantBufferPool.Create(m_RenderContext, name, std::move(info));
    if constexpr (std::is_same_v< TResource, StructuredBuffer >)
        return m_StructuredBufferPool.Create(m_RenderContext, name, std::move(info));

    if constexpr (std::is_same_v< TResource, Texture >)
        return m_TexturePool.Create(m_RenderContext, name, std::move(info));
    if constexpr (std::is_same_v< TResource, Sampler >)
        return m_SamplerPool.Create(m_RenderContext, name, std::move(info));
    if constexpr (std::is_same_v< TResource, Shader >)
        return m_ShaderPool.Create(m_RenderContext, name, std::move(info));
    return baamboo::ResourceHandle< TResource >();
}

template< typename TResource >
TResource* ResourceManager::CreateEmpty(std::wstring_view name)
{
    static_assert(std::is_base_of_v< Resource, TResource >);

    if constexpr (std::is_same_v< TResource, Buffer >)
        return new Buffer(m_RenderContext, name);
    if constexpr (std::is_same_v< TResource, Texture >)
        return new Texture(m_RenderContext, name);
    return nullptr;
}

template< typename TResource >
baamboo::ResourceHandle< TResource > ResourceManager::Add(TResource* pResource)
{
    static_assert(std::is_base_of_v< Resource, TResource >);

    if constexpr (std::is_same_v< TResource, VertexBuffer >)
        return m_VertexBufferPool.Add(pResource);
    if constexpr (std::is_same_v< TResource, IndexBuffer >)
        return m_IndexBufferPool.Add(pResource);
    if constexpr (std::is_same_v< TResource, ConstantBuffer >)
        return m_ConstantBufferPool.Add(pResource);
    if constexpr (std::is_same_v< TResource, StructuredBuffer >)
        return m_StructuredBufferPool.Add(pResource);

    if constexpr (std::is_same_v< TResource, Texture >)
        return m_TexturePool.Add(pResource);
    if constexpr (std::is_same_v< TResource, Sampler >)
        return m_SamplerPool.Add(pResource);
    if constexpr (std::is_same_v< TResource, Shader >)
        return m_ShaderPool.Add(pResource);
    return baamboo::ResourceHandle< TResource >();
}

template< typename TResource >
inline TResource* ResourceManager::Get(baamboo::ResourceHandle< TResource > handle) const
{
    static_assert(std::is_base_of_v< Resource, TResource >);

    if constexpr (std::is_same_v< TResource, VertexBuffer >)
        return m_VertexBufferPool.Get(handle);
    if constexpr (std::is_same_v< TResource, IndexBuffer >)
        return m_IndexBufferPool.Get(handle);
    if constexpr (std::is_same_v< TResource, ConstantBuffer >)
        return m_ConstantBufferPool.Get(handle);
    if constexpr (std::is_same_v< TResource, StructuredBuffer >)
        return m_StructuredBufferPool.Get(handle);

    if constexpr (std::is_same_v< TResource, Texture >)
        return m_TexturePool.Get(handle);
    if constexpr (std::is_same_v< TResource, Sampler >)
        return m_SamplerPool.Get(handle);
    if constexpr (std::is_same_v< TResource, Shader >)
        return m_ShaderPool.Get(handle);
    return nullptr;
}

template< typename TResource >
inline void ResourceManager::Remove(baamboo::ResourceHandle< TResource > handle)
{
    static_assert(std::is_base_of_v< Resource, TResource >);

    if constexpr (std::is_same_v< TResource, VertexBuffer >)
        m_VertexBufferPool.Remove(handle);
    if constexpr (std::is_same_v< TResource, IndexBuffer >)
        m_IndexBufferPool.Remove(handle);
    if constexpr (std::is_same_v< TResource, ConstantBuffer >)
        m_ConstantBufferPool.Remove(handle);
    if constexpr (std::is_same_v< TResource, StructuredBuffer >)
        m_StructuredBufferPool.Remove(handle);

    if constexpr (std::is_same_v< TResource, Texture >)
        m_TexturePool.Remove(handle);
    if constexpr (std::is_same_v< TResource, Sampler >)
        m_SamplerPool.Remove(handle);
    if constexpr (std::is_same_v< TResource, Shader >)
        m_ShaderPool.Remove(handle);
}

}
