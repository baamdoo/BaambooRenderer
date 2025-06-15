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

struct FrameData
{
    // data
    CameraData camera = {};

    // scene-resource
    struct SceneResource* pSceneResource = nullptr;

    // framebuffer
    Weak< Texture > pColor;
    Weak< Texture > pDepth;
};
inline FrameData g_FrameData;

struct SceneResource
{
    SceneResource(RenderDevice& device);
    ~SceneResource();

    void UpdateSceneResources(const SceneRenderView& sceneView);

    BufferHandle GetOrUpdateVertex(u32 entity, std::string_view filepath, const void* pData, u32 count);
    BufferHandle GetOrUpdateIndex(u32 entity, std::string_view filepath, const void* pData, u32 count);
    Arc< Texture > GetOrLoadTexture(u32 entity, std::string_view filepath);
    Arc< Texture > GetTexture(std::string_view filepath);

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
    RenderDevice& m_RenderDevice;

    VkDescriptorSetLayout m_vkSetLayout     = VK_NULL_HANDLE;
    DescriptorPool*       m_pDescriptorPool = nullptr;

    Box< StaticBufferAllocator > m_pVertexAllocator;
    Box< StaticBufferAllocator > m_pIndexAllocator;
    Box< StaticBufferAllocator > m_pIndirectDrawAllocator;
    Box< StaticBufferAllocator > m_pTransformAllocator;
    Box< StaticBufferAllocator > m_pMaterialAllocator;
    Box< StaticBufferAllocator > m_pLightAllocator;

    std::unordered_map< std::string, BufferHandle >   m_VertexCache;
    std::unordered_map< std::string, BufferHandle >   m_IndexCache;
    std::unordered_map< std::string, Arc< Texture > > m_TextureCache;

    Arc< Sampler > m_pDefaultSampler;
};


} // namespace dx12