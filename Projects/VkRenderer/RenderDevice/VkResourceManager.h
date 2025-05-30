#pragma once
#include "Core/VkResourcePool.h"
#include "RenderResource/VkShader.h"
#include "RenderResource/VkBuffer.h"
#include "RenderResource/VkTexture.h"
#include "RenderResource/VkSampler.h"

#include <Scene/SceneRenderView.h>

namespace vk
{

class ResourceManager
{
public:
    ResourceManager(RenderContext& context);
    virtual ~ResourceManager();

    Buffer* GetStagingBuffer(u32 numElements, u64 elementSize) const;

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
    RenderContext& m_RenderContext;

    ResourcePool< Buffer >  m_Buffers;
    ResourcePool< Texture > m_Textures;
    ResourcePool< Sampler > m_Samplers;
    ResourcePool< Shader >  m_Shaders;
};

template< typename TResource >
baamboo::ResourceHandle< TResource > ResourceManager::Create(std::wstring_view name, typename TResource::CreationInfo&& desc)
{
    static_assert(std::is_base_of_v< Resource< TResource >, TResource >);

    if constexpr (std::is_same_v< TResource, Buffer >)
        return m_Buffers.Create(m_RenderContext, name, std::move(desc));
    if constexpr (std::is_same_v< TResource, Texture >)
        return m_Textures.Create(m_RenderContext, name, std::move(desc));
    if constexpr (std::is_same_v< TResource, Sampler >)
        return m_Samplers.Create(m_RenderContext, name, std::move(desc));
    if constexpr (std::is_same_v< TResource, Shader >)
        return m_Shaders.Create(m_RenderContext, name, std::move(desc));
    return baamboo::ResourceHandle< TResource >();
}

template< typename TResource >
TResource* ResourceManager::CreateEmpty(std::wstring_view name)
{
    static_assert(std::is_base_of_v< Resource< TResource >, TResource >);

    if constexpr (std::is_same_v< TResource, Buffer >)
        return new Buffer(m_RenderContext, name);
    if constexpr (std::is_same_v< TResource, Texture >)
        return new Texture(m_RenderContext, name);
    return nullptr;
}

template< typename TResource >
baamboo::ResourceHandle< TResource > ResourceManager::Add(TResource* pResource)
{
    static_assert(std::is_base_of_v< Resource< TResource >, TResource >);

    if constexpr (std::is_same_v< TResource, Buffer >)
        return m_Buffers.Add(pResource);
    if constexpr (std::is_same_v< TResource, Texture >)
        return m_Textures.Add(pResource);
    if constexpr (std::is_same_v< TResource, Sampler >)
        return m_Samplers.Add(pResource);
    if constexpr (std::is_same_v< TResource, Shader >)
        return m_Shaders.Add(pResource);
    return baamboo::ResourceHandle< TResource >();
}

template< typename TResource >
inline TResource* ResourceManager::Get(baamboo::ResourceHandle< TResource > handle) const
{
    static_assert(std::is_base_of_v< Resource< TResource >, TResource >);

    if constexpr (std::is_same_v< TResource, Buffer >)
        return m_Buffers.Get(handle);
    if constexpr (std::is_same_v< TResource, Texture >)
        return m_Textures.Get(handle);
    if constexpr (std::is_same_v< TResource, Sampler >)
        return m_Samplers.Get(handle);
    if constexpr (std::is_same_v< TResource, Shader >)
        return m_Shaders.Get(handle);
    return nullptr;
}

template< typename TResource >
inline void ResourceManager::Remove(baamboo::ResourceHandle< TResource > handle)
{
    static_assert(std::is_base_of_v< Resource< TResource >, TResource >);

    if constexpr (std::is_same_v< TResource, Buffer >)
        m_Buffers.Remove(handle);
    if constexpr (std::is_same_v< TResource, Texture >)
        m_Textures.Remove(handle);
    if constexpr (std::is_same_v< TResource, Sampler >)
        m_Samplers.Remove(handle);
    if constexpr (std::is_same_v< TResource, Shader >)
        m_Shaders.Remove(handle);
}

} // namespace vk