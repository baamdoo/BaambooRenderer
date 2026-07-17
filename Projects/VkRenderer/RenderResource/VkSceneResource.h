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

    virtual void UpdateSceneResources(const SceneRenderView& sceneView, render::CommandContext& context) override;
    virtual void BindSceneResources(render::CommandContext& context) override;

    BufferHandle GetOrUpdateVertex(u64 entity, const std::string& filepath, const void* pData, u32 count);
    BufferHandle GetOrUpdateIndex(u64 entity, const std::string& filepath, const void* pData, u32 count);

    BufferHandle GetOrUpdateMeshlets(u64 entity, const std::string& filepath, const void* pData, u32 count);
    BufferHandle GetOrUpdateMeshletVertices(u64 entity, const std::string& filepath, const void* pData, u32 count);
    BufferHandle GetOrUpdateMeshletTriangles(u64 entity, const std::string& filepath, const void* pData, u32 count);

    Arc< VulkanTexture > GetOrLoadTexture(u64 entity, const std::string& filepath, render::eTextureColorSpace colorSpace = render::eTextureColorSpace::Linear);
    Arc< VulkanTexture > GetTexture(const std::string& filepath);

    void SetCurrentContextIndex(u32 index) { m_ContextIndex = index; }

    [[nodiscard]]
    VkDescriptorSet GetSceneDescriptorSet() const;
    [[nodiscard]]
    VkDescriptorSetLayout GetSceneDescriptorSetLayout() const { return m_vkSetLayout; }
    [[nodiscard]]
    VkDescriptorBufferInfo GetIndexBufferInfo() const;
    [[nodiscard]]
    VkDescriptorBufferInfo GetMeshletBufferInfo() const;

    [[nodiscard]]
    VkDescriptorBufferInfo GetInstanceInfo() const;

    virtual const Arc< render::Buffer >& GetArgumentBuffer() const;

    [[nodiscard]]
    virtual Arc< render::Buffer > GetMeshDataBuffer() const override;

    // TEMP
    std::vector< VkDescriptorImageInfo > imageInfos;

private:
    void ResetFrameBuffers();
    void UpdateFrameBuffer(VkCommandContext& context, const void* pData, u32 count, u64 elementSizeInBytes, StaticBufferAllocator& targetBuffer, VkPipelineStageFlags2 dstStageMask);
    void UpdateCameraAndEnvironment(const SceneRenderView& sceneView, VkCommandContext& ctx);

private:
    VkRenderDevice& m_RenderDevice;

    VkDescriptorSetLayout m_vkSetLayout            = VK_NULL_HANDLE;
    VkPipelineLayout      m_vkGlobalPipelineLayout = VK_NULL_HANDLE;

	DescriptorPool*       m_pDescriptorPool = nullptr;

    Box< StaticBufferAllocator > m_pVertexAllocator;
    Box< StaticBufferAllocator > m_pIndexAllocator;
    Box< StaticBufferAllocator > m_pMeshletAllocator;
    Box< StaticBufferAllocator > m_pMeshletVertexAllocator;
    Box< StaticBufferAllocator > m_pMeshletTriangleAllocator;

    CameraData m_CameraCache = {};
    CullData   m_CullData = {};

    struct PerFrameData
    {
        Box< StaticBufferAllocator > pMeshDataAllocator;
        Box< StaticBufferAllocator > pInstanceAllocator;
        //Box< StaticBufferAllocator > pIndirectCommandAllocator;

        Box< StaticBufferAllocator > pTransformAllocator;
        Box< StaticBufferAllocator > pMaterialAllocator;
        Box< StaticBufferAllocator > pLightAllocator;

        Arc< VulkanUniformBuffer > pCameraBuffer;
        Arc< VulkanUniformBuffer > pCullBuffer;
        Arc< VulkanUniformBuffer > pSceneEnvironmentBuffer;
        Arc< VulkanUniformBuffer > pFrozenCameraBuffer;

        bool bInitialized = false;

        void Reset();
    };
    std::array< PerFrameData, kMaxFramesInFlight > m_FrameData;
    u64 m_LastSceneRevision = 0;


    std::unordered_map< std::string, BufferHandle >         m_VertexCache;
    std::unordered_map< std::string, BufferHandle >         m_IndexCache;
    std::unordered_map< std::string, Arc< VulkanTexture > > m_TextureCache;

    std::unordered_map< std::string, BufferHandle > m_MeshletCache;
    std::unordered_map< std::string, BufferHandle > m_MeshletVertexCache;
    std::unordered_map< std::string, BufferHandle > m_MeshletTriangleCache;

    Arc< VulkanSampler > m_pDefaultSampler;
};


} // namespace dx12
