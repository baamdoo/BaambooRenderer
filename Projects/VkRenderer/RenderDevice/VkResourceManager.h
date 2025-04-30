#pragma once
#include "BaambooCore/BackendAPI.h"
#include "Core/VkResourcePool.h"
#include "RenderResource/VkShader.h"
#include "RenderResource/VkBuffer.h"
#include "RenderResource/VkTexture.h"
#include "RenderResource/VkSampler.h"

namespace vk
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
    baamboo::ResourceHandle< TResource > Create(std::wstring_view name, typename TResource::CreationInfo&& desc);
    template< typename TResource >
	TResource* CreateEmpty(std::wstring_view name);

    template< typename TResource >
    baamboo::ResourceHandle< TResource > Add(TResource* pResource);

    template< typename TResource >
    TResource* Get(baamboo::ResourceHandle< TResource > handle) const;

    template< typename TResource >
    void Remove(baamboo::ResourceHandle< TResource > handle);

private:
    RenderContext& m_renderContext;

    ResourcePool< Buffer >  m_buffers;
    ResourcePool< Texture > m_textures;
    ResourcePool< Sampler > m_samplers;
    ResourcePool< Shader >  m_shaders;
};

template< typename TResource >
baamboo::ResourceHandle< TResource > ResourceManager::Create(std::wstring_view name, typename TResource::CreationInfo&& desc)
{
    static_assert(std::is_base_of_v< Resource< TResource >, TResource >);

    if constexpr (std::is_same_v< TResource, Buffer >)
        return m_buffers.Create(m_renderContext, name, std::move(desc));
    if constexpr (std::is_same_v< TResource, Texture >)
        return m_textures.Create(m_renderContext, name, std::move(desc));
    if constexpr (std::is_same_v< TResource, Sampler >)
        return m_samplers.Create(m_renderContext, name, std::move(desc));
    if constexpr (std::is_same_v< TResource, Shader >)
        return m_shaders.Create(m_renderContext, name, std::move(desc));
    return baamboo::ResourceHandle< TResource >();
}

template< typename TResource >
TResource* ResourceManager::CreateEmpty(std::wstring_view name)
{
    static_assert(std::is_base_of_v< Resource< TResource >, TResource >);

    if constexpr (std::is_same_v< TResource, Buffer >)
        return new Buffer(m_renderContext, name);
    if constexpr (std::is_same_v< TResource, Texture >)
        return new Texture(m_renderContext, name);
    return nullptr;
}

template< typename TResource >
baamboo::ResourceHandle< TResource > ResourceManager::Add(TResource* pResource)
{
    static_assert(std::is_base_of_v< Resource< TResource >, TResource >);

    if constexpr (std::is_same_v< TResource, Buffer >)
        return m_buffers.Add(pResource);
    if constexpr (std::is_same_v< TResource, Texture >)
        return m_textures.Add(pResource);
    if constexpr (std::is_same_v< TResource, Sampler >)
        return m_samplers.Add(pResource);
    if constexpr (std::is_same_v< TResource, Shader >)
        return m_shaders.Add(pResource);
    return baamboo::ResourceHandle< TResource >();
}

template< typename TResource >
inline TResource* ResourceManager::Get(baamboo::ResourceHandle< TResource > handle) const
{
    static_assert(std::is_base_of_v< Resource< TResource >, TResource >);

    if constexpr (std::is_same_v< TResource, Buffer >)
        return m_buffers.Get(handle);
    if constexpr (std::is_same_v< TResource, Texture >)
        return m_textures.Get(handle);
    if constexpr (std::is_same_v< TResource, Sampler >)
        return m_samplers.Get(handle);
    if constexpr (std::is_same_v< TResource, Shader >)
        return m_shaders.Get(handle);
    return nullptr;
}

template< typename TResource >
inline void ResourceManager::Remove(baamboo::ResourceHandle< TResource > handle)
{
    static_assert(std::is_base_of_v< Resource< TResource >, TResource >);

    if constexpr (std::is_same_v< TResource, Buffer >)
        m_buffers.Remove(handle);
    if constexpr (std::is_same_v< TResource, Texture >)
        m_textures.Remove(handle);
    if constexpr (std::is_same_v< TResource, Sampler >)
        m_samplers.Remove(handle);
    if constexpr (std::is_same_v< TResource, Shader >)
        m_shaders.Remove(handle);
}

} // namespace vk