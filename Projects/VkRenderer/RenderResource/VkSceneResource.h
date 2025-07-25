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
    // frame static data
    CameraData     camera     = {};
    struct
    {
        AtmosphereData data = {};

        u32  msIsoSampleCount = 0;
        u32  msNumRaySteps    = 0;
        u32  svMinRaySteps    = 0;
        u32  svMaxRaySteps    = 0;
        bool bMark = false;
    } atmosphere;

    // scene-resource
    struct SceneResource* pSceneResource = nullptr;

    // LUTs
    Weak< Texture > pSkyViewLUT;
    Weak< Texture > pAerialPerspectiveLUT;

    // framebuffers
    Weak< Texture > pGBuffer0;
    Weak< Texture > pGBuffer1;
    Weak< Texture > pGBuffer2;
    Weak< Texture > pGBuffer3;
    Weak< Texture > pColor;
    Weak< Texture > pDepth;
};
inline FrameData g_FrameData;

struct SceneResource
{
    SceneResource(RenderDevice& device);
    ~SceneResource();

    void UpdateSceneResources(const SceneRenderView& sceneView);

    BufferHandle GetOrUpdateVertex(u32 entity, const std::string& filepath, const void* pData, u32 count);
    BufferHandle GetOrUpdateIndex(u32 entity, const std::string& filepath, const void* pData, u32 count);
    Arc< Texture > GetOrLoadTexture(u32 entity, const std::string& filepath);
    Arc< Texture > GetTexture(const std::string& filepath);

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