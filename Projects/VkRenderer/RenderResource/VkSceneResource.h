#pragma once
#include "VkBuffer.h"
#include "VkTexture.h"
#include "VkSampler.h"

struct SceneRenderView;

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

struct VkSceneResource : public render::SceneResource
{
    VkSceneResource(VkRenderDevice& rd);
    ~VkSceneResource();

    virtual void UpdateSceneResources(const SceneRenderView& sceneView) override;
    virtual void BindSceneResources(render::CommandContext& context) override;

    BufferHandle GetOrUpdateVertex(u64 entity, const std::string& filepath, const void* pData, u32 count);
    BufferHandle GetOrUpdateIndex(u64 entity, const std::string& filepath, const void* pData, u32 count);
    Arc< VulkanTexture > GetOrLoadTexture(u64 entity, const std::string& filepath);
    Arc< VulkanTexture > GetTexture(const std::string& filepath);

    [[nodiscard]]
    VkDescriptorSet GetSceneDescriptorSet() const;
    [[nodiscard]]
    VkDescriptorSetLayout GetSceneDescriptorSetLayout() const { return m_vkSetLayout; }
    [[nodiscard]]
    VkDescriptorBufferInfo GetIndexBufferInfo() const;
    [[nodiscard]]
    VkDescriptorBufferInfo GetIndirectBufferInfo() const;

    // TEMP
    std::vector< VkDescriptorImageInfo > imageInfos;

private:
    void ResetFrameBuffers();
    void UpdateFrameBuffer(const void* pData, u32 count, u64 elementSizeInBytes, StaticBufferAllocator& targetBuffer);

private:
    VkRenderDevice& m_RenderDevice;

    VkDescriptorSetLayout m_vkSetLayout     = VK_NULL_HANDLE;
    DescriptorPool*       m_pDescriptorPool = nullptr;

    Box< StaticBufferAllocator > m_pVertexAllocator;
    Box< StaticBufferAllocator > m_pIndexAllocator;
    Box< StaticBufferAllocator > m_pIndirectDrawAllocator;
    Box< StaticBufferAllocator > m_pTransformAllocator;
    Box< StaticBufferAllocator > m_pMaterialAllocator;
    Box< StaticBufferAllocator > m_pLightAllocator;

    std::unordered_map< std::string, BufferHandle >         m_VertexCache;
    std::unordered_map< std::string, BufferHandle >         m_IndexCache;
    std::unordered_map< std::string, Arc< VulkanTexture > > m_TextureCache;

    Arc< VulkanSampler > m_pDefaultSampler;
};


} // namespace dx12