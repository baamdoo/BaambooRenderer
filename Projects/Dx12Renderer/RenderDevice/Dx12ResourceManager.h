#pragma once
#include "BaambooCore/BackendAPI.h"
#include "BaambooCore/ResourceHandle.h"
#include "Core/Dx12ResourcePool.h"
#include "RenderResource/Dx12Shader.h"
#include "RenderResource/Dx12Buffer.h"
#include "RenderResource/Dx12Texture.h"
#include "RenderResource/Dx12Sampler.h"

namespace dx12
{

class ResourceManager : public ResourceManagerAPI
{
public:
    ResourceManager(RenderContext& context);
    virtual ~ResourceManager();

    virtual VertexHandle CreateVertexBuffer(std::wstring_view name, u32 numVertices, u64 elementSizeInBytes, void* data) override;
    virtual IndexHandle CreateIndexBuffer(std::wstring_view name, u32 numIndices, u64 elementSizeInBytes, void* data) override;
    virtual TextureHandle CreateTexture(std::string_view filepath, bool bGenerateMips) override;

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

    ResourcePool< Buffer >  m_BufferPool;
    ResourcePool< Texture > m_TexturePool;
    ResourcePool< Sampler > m_SamplerPool;
    ResourcePool< Shader >  m_ShaderPool;

    class DescriptorPool* m_pDescriptorPools[D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES] = {};
};

template< typename TResource >
baamboo::ResourceHandle< TResource > ResourceManager::Create(std::wstring_view name, typename TResource::CreationInfo&& info)
{
    static_assert(std::is_base_of_v< Resource, TResource >);

    if constexpr (std::is_same_v< TResource, Buffer >)
        return m_BufferPool.Create(m_RenderContext, name, std::move(info));
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

    if constexpr (std::is_same_v< TResource, Buffer >)
        return m_BufferPool.Add(pResource);
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

    if constexpr (std::is_same_v< TResource, Buffer >)
        return m_BufferPool.Get(handle);
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

    if constexpr (std::is_same_v< TResource, Buffer >)
        m_BufferPool.Remove(handle);
    if constexpr (std::is_same_v< TResource, Texture >)
        m_TexturePool.Remove(handle);
    if constexpr (std::is_same_v< TResource, Sampler >)
        m_SamplerPool.Remove(handle);
    if constexpr (std::is_same_v< TResource, Shader >)
        m_ShaderPool.Remove(handle);
}

}
