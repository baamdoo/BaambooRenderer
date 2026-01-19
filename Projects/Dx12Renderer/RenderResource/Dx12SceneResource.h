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

    virtual void UpdateSceneResources(const SceneRenderView& sceneView) override;
    virtual void BindSceneResources(render::CommandContext& context) override;

    BufferHandle GetOrUpdateVertex(u64 entity, const std::string& filepath, const void* pData, u32 count);
    Arc< Dx12IndexBuffer > GetOrUpdateIndex(u64 entity, const std::string& filepath, const void* pData, u32 count);

    BufferHandle GetOrUpdateMeshlets(u64 entity, const std::string& filepath, const void* pData, u32 count);
    BufferHandle GetOrUpdateMeshletVertices(u64 entity, const std::string& filepath, const void* pData, u32 count);
    BufferHandle GetOrUpdateMeshletTriangles(u64 entity, const std::string& filepath, const void* pData, u32 count);

    Arc< Dx12Texture > GetOrLoadTexture(u64 entity, const std::string& filepath);
    Arc< Dx12Texture > GetTexture(const std::string& filepath);

    const Arc< Dx12RootSignature >& GetSceneRootSignature() const { return m_pRootSignature; }
    ID3D12CommandSignature* GetSceneD3D12CommandSignature() const;

    Arc< Dx12StructuredBuffer > GetIndirectBuffer() const;
    Arc< Dx12StructuredBuffer > GetTransformBuffer() const;
    Arc< Dx12StructuredBuffer > GetMaterialBuffer() const;
    Arc< Dx12StructuredBuffer > GetLightBuffer() const;
    Arc< Dx12StructuredBuffer > GetMeshletBuffer() const;

    [[nodiscard]]
    inline u32 NumMeshes() const { return m_NumMeshes; }

    std::unordered_map< std::string, u64 > resourceBindingMapTemp;

private:
    void ResetFrameBuffers();
    void UpdateFrameBuffer(const void* pData, u32 count, u64 elementSizeInBytes, StaticBufferAllocator& targetBuffer, D3D12_RESOURCE_STATES stateAfter);

    Dx12RenderDevice& m_RenderDevice;

    Arc< Dx12RootSignature > m_pRootSignature;
    CommandSignature* m_pIndirectDrawSignature     = nullptr;
    CommandSignature* m_pIndirectDispatchSignature = nullptr;

    Box< StaticBufferAllocator > m_pIndirectDataAllocator;
    Box< StaticBufferAllocator > m_pTransformAllocator;
    Box< StaticBufferAllocator > m_pMaterialAllocator;
    Box< StaticBufferAllocator > m_pLightAllocator;

    Box< StaticBufferAllocator > m_pVertexAllocator;
    Box< StaticBufferAllocator > m_pMeshletAllocator;
    Box< StaticBufferAllocator > m_pMeshletVertexAllocator;
    Box< StaticBufferAllocator > m_pMeshletTriangleAllocator;

    CameraData                m_CameraCache = {};
    Arc< Dx12ConstantBuffer > m_pCameraBuffer;
    Arc< Dx12ConstantBuffer > m_pSceneEnvironmentBuffer;

    std::unordered_map< std::string, BufferHandle > m_VertexCache;
    std::unordered_map< std::string, BufferHandle > m_MeshletCache;
    std::unordered_map< std::string, BufferHandle > m_MeshletVertexCache;
    std::unordered_map< std::string, BufferHandle > m_MeshletTriangleCache;

    std::unordered_map< std::string, Arc< Dx12IndexBuffer > > m_IndexCache;

    std::unordered_map< std::string, Arc< Dx12Texture > > m_TextureCache;

    u32 m_NumMeshes = 0;
};

} // namespace dx12