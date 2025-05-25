#pragma once
#include "Core/VkResourcePool.h"
#include "RenderResource/VkShader.h"
#include "RenderResource/VkBuffer.h"
#include "RenderResource/VkTexture.h"
#include "RenderResource/VkSampler.h"

#include <Scene/SceneRenderView.h>

namespace vk
{

class StaticBufferAllocator;

struct BufferHandle
{
    VkBuffer vkBuffer;
    
    u32 count;
    u32 offset;
    u64 elementSizeInBytes;
};
using TextureHandle = baamboo::ResourceHandle< Texture >;

struct SceneResource
{
    SceneResource(RenderContext& context);
    ~SceneResource();

    void UpdateSceneResources(const SceneRenderView& sceneView);

    BufferHandle GetOrUpdateVertex(u32 entity, std::string_view filepath, const void* pData, u32 count);
    BufferHandle GetOrUpdateIndex(u32 entity, std::string_view filepath, const void* pData, u32 count);
    TextureHandle GetOrLoadTexture(u32 entity, std::string_view filepath);

    [[nodiscard]]
    VkDescriptorBufferInfo GetVertexDescriptorInfo() const;
    [[nodiscard]]
    VkDescriptorBufferInfo GetIndexDescriptorInfo() const;
    [[nodiscard]]
    VkDescriptorBufferInfo GetIndirectDrawDescriptorInfo() const;
    [[nodiscard]]
    VkDescriptorBufferInfo GetTransformDescriptorInfo() const;
    [[nodiscard]]
    VkDescriptorBufferInfo GetMaterialDescriptorInfo() const;

    [[nodiscard]]
    VkDescriptorSet GetSceneDescriptorSet() const;
    [[nodiscard]]
    VkDescriptorSetLayout GetSceneDescriptorSetLayout() const { return m_vkSetLayout; }

private:
    void ResetFrameBuffers();
    void UpdateFrameBuffer(const void* pData, u32 count, u64 elementSizeInBytes, StaticBufferAllocator* pTargetBuffer);

    RenderContext& m_renderContext;

    VkDescriptorSetLayout m_vkSetLayout = VK_NULL_HANDLE;
    DescriptorPool*       m_pDescriptorPool = nullptr;

    StaticBufferAllocator* m_pVertexBufferPool = nullptr;
    StaticBufferAllocator* m_pIndexBufferPool = nullptr;
    StaticBufferAllocator* m_pIndirectDrawBufferPool = nullptr;
    StaticBufferAllocator* m_pTransformBufferPool = nullptr;
    StaticBufferAllocator* m_pMaterialBufferPool = nullptr;

    std::unordered_map< std::string, BufferHandle >  m_vertexCache;
    std::unordered_map< std::string, BufferHandle >  m_indexCache;
    std::unordered_map< std::string, TextureHandle > m_textureCache;

    baamboo::ResourceHandle< Sampler > m_defaultSampler;
};

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