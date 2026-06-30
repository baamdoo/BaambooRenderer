#pragma once
#include "Dx12Buffer.h"
#include "Dx12Texture.h"
#include "Dx12Sampler.h"

struct SceneRenderView;

namespace dx12
{

class Dx12RootSignature;
class CommandSignature;
class StaticBufferAllocator;
class Dx12BottomLevelAS;
class Dx12TopLevelAS;

struct BufferHandle
{
    u32 count;
    u32 offset;
    u64 elementSizeInBytes;

    D3D12_GPU_VIRTUAL_ADDRESS gpuHandle;
};

struct Dx12SceneResource : public render::SceneResource
{
    Dx12SceneResource(Dx12RenderDevice& rd);
    ~Dx12SceneResource();

    virtual void UpdateSceneResources(const SceneRenderView& sceneView, render::CommandContext& context) override;
    virtual void BindSceneResources(render::CommandContext& context) override;

    BufferHandle GetOrUpdateVertex(u64 entity, const std::string& filepath, const void* pData, u32 count);
    BufferHandle GetOrUpdateIndex(u64 entity, const std::string& filepath, const void* pData, u32 count);

    BufferHandle GetOrUpdateMeshlets(u64 entity, const std::string& filepath, const void* pData, u32 count);
    BufferHandle GetOrUpdateMeshletVertices(u64 entity, const std::string& filepath, const void* pData, u32 count);
    BufferHandle GetOrUpdateMeshletTriangles(u64 entity, const std::string& filepath, const void* pData, u32 count);

    Arc< Dx12BottomLevelAS > GetOrCreateBLAS(const std::string& tag, const BufferHandle& vHandle, const BufferHandle& iHandle);

    Arc< Dx12Texture > GetOrLoadTexture(u64 entity, const std::string& filepath, render::eTextureColorSpace colorSpace = render::eTextureColorSpace::Linear);
    Arc< Dx12Texture > GetTexture(const std::string& filepath);

    void SetCurrentContextIndex(u32 index) { m_ContextIndex = index; }

    const Arc< Dx12RootSignature >& GetSceneRootSignature() const { return m_pRootSignature; }
    ID3D12CommandSignature* GetSceneD3D12CommandSignature() const;

    Arc< Dx12StructuredBuffer > GetIndirectBuffer() const;
    Arc< Dx12StructuredBuffer > GetTransformBuffer() const;
    Arc< Dx12StructuredBuffer > GetMaterialBuffer() const;
    Arc< Dx12StructuredBuffer > GetLightBuffer() const;
    Arc< Dx12StructuredBuffer > GetMeshletBuffer() const;

    [[nodiscard]]
    virtual Arc< render::TopLevelAccelerationStructure > GetTLAS() const override;

    [[nodiscard]]
    virtual Arc< render::Buffer > GetMeshDataBuffer() const override;

private:
    void ResetFrameBuffers();
    void UpdateFrameBuffer(Dx12CommandContext& context, const void* pData, u32 count, u64 elementSizeInBytes, StaticBufferAllocator& targetBuffer, const BarrierState& stateAfter);
    void BuildAccelerationStructures();
    void UpdateCameraAndEnvironment(const SceneRenderView& sceneView, Dx12CommandContext& ctx);

    Dx12RenderDevice& m_RenderDevice;

    Arc< Dx12RootSignature > m_pRootSignature;
    CommandSignature* m_pIndirectDrawSignature     = nullptr;
    CommandSignature* m_pIndirectDispatchSignature = nullptr;

    Box< StaticBufferAllocator > m_pVertexAllocator;
    Box< StaticBufferAllocator > m_pIndexAllocator;
    Box< StaticBufferAllocator > m_pMeshletAllocator;
    Box< StaticBufferAllocator > m_pMeshletVertexAllocator;
    Box< StaticBufferAllocator > m_pMeshletTriangleAllocator;

    struct PerFrameData
    {
        Box< StaticBufferAllocator > pMeshDataAllocator;
        Box< StaticBufferAllocator > pInstanceAllocator;

        Box< StaticBufferAllocator > pTransformAllocator;
        Box< StaticBufferAllocator > pMaterialAllocator;
        Box< StaticBufferAllocator > pLightAllocator;

        Arc< Dx12ConstantBuffer > pCameraBuffer;
        Arc< Dx12ConstantBuffer > pCullBuffer;
        Arc< Dx12ConstantBuffer > pSceneEnvironmentBuffer;
        Arc< Dx12ConstantBuffer > pFrozenCameraBuffer;
        Arc< Dx12ConstantBuffer > pMeshStreamsBuffer; // CBV holding the 5 geometry-pool heap indices (g_MeshStreams)

        bool bInitialized = false;

        void Reset();
    };
    std::array< PerFrameData, kMaxFramesInFlight > m_FrameData;

    CullData   m_CullData    = {};
    CameraData m_CameraCache = {};

    std::unordered_map< std::string, BufferHandle > m_VertexCache;
    std::unordered_map< std::string, BufferHandle > m_IndexCache;
    std::unordered_map< std::string, BufferHandle > m_MeshletCache;
    std::unordered_map< std::string, BufferHandle > m_MeshletVertexCache;
    std::unordered_map< std::string, BufferHandle > m_MeshletTriangleCache;

    std::unordered_map< std::string, Arc< Dx12Texture > > m_TextureCache;

    Arc< Dx12TopLevelAS > m_pTLAS;
    std::unordered_map< std::string, Arc< Dx12BottomLevelAS > > m_BLASCache;
    std::vector< Dx12BottomLevelAS* >                           m_PendingBLASBuilds;
};

} // namespace dx12
