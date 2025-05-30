#pragma once
#include "RenderDevice/VkResourceManager.h"

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

    RenderContext& m_RenderContext;

    VkDescriptorSetLayout m_vkSetLayout = VK_NULL_HANDLE;
    DescriptorPool*       m_pDescriptorPool = nullptr;

    StaticBufferAllocator* m_pVertexBufferPool = nullptr;
    StaticBufferAllocator* m_pIndexBufferPool = nullptr;
    StaticBufferAllocator* m_pIndirectDrawBufferPool = nullptr;
    StaticBufferAllocator* m_pTransformBufferPool = nullptr;
    StaticBufferAllocator* m_pMaterialBufferPool = nullptr;

    std::unordered_map< std::string, BufferHandle >  m_VertexCache;
    std::unordered_map< std::string, BufferHandle >  m_IndexCache;
    std::unordered_map< std::string, TextureHandle > m_TextureCache;

    baamboo::ResourceHandle< Sampler > m_DefaultSampler;
};


} // namespace dx12