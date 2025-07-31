#pragma once
#include "Dx12Buffer.h"
#include "Dx12Texture.h"
#include "Dx12Sampler.h"

struct SceneRenderView;

namespace dx12
{

class RootSignature;
class CommandSignature;
class StaticBufferAllocator;

struct BufferHandle
{
    u32 count;
    u32 offset;
    u64 elementSizeInBytes;

    D3D12_GPU_VIRTUAL_ADDRESS gpuHandle;
};

struct FrameData
{
    u64 frameCounter;

    // data
    CameraData camera = {};

    u64 componentMarker;

    // scene-resource
    struct SceneResource* pSceneResource = nullptr;

    // LUTs
    Weak< Texture > pSkyViewLUT;
    Weak< Texture > pAerialPerspectiveLUT;

    // render-targets
    Weak< Texture > pGBuffer0;
    Weak< Texture > pGBuffer1;
    Weak< Texture > pGBuffer2;
    Weak< Texture > pGBuffer3;
    Weak< Texture > pColor;
    Weak< Texture > pDepth;
};
inline FrameData g_FrameData = {};

struct SceneResource
{
    SceneResource(RenderDevice& device);
    ~SceneResource();

    void UpdateSceneResources(const SceneRenderView& sceneView);

    Arc< VertexBuffer > GetOrUpdateVertex(u64 entity, const std::string& filepath, const void* pData, u32 count);
    Arc< IndexBuffer >  GetOrUpdateIndex(u64 entity, const std::string& filepath, const void* pData, u32 count);
    Arc< Texture >      GetOrLoadTexture(u64 entity, const std::string& filepath);
    Arc< Texture >      GetTexture(const std::string& filepath);

    [[nodiscard]]
    inline RootSignature* GetSceneRootSignature() const { return m_pRootSignature; }
    [[nodiscard]]
    ID3D12CommandSignature* GetSceneD3D12CommandSignature() const;

    [[nodiscard]]
    Arc< StructuredBuffer > GetIndirectBuffer() const;
    [[nodiscard]]
    Arc< StructuredBuffer > GetTransformBuffer() const;
    [[nodiscard]]
    Arc< StructuredBuffer > GetMaterialBuffer() const;
    [[nodiscard]]
    Arc< StructuredBuffer > GetLightBuffer() const;

    [[nodiscard]]
    inline u32 NumMeshes() const { return m_NumMeshes; }

    std::vector< D3D12_CPU_DESCRIPTOR_HANDLE > sceneTexSRVs;

private:
    void ResetFrameBuffers();
    void UpdateFrameBuffer(const void* pData, u32 count, u64 elementSizeInBytes, StaticBufferAllocator& targetBuffer, D3D12_RESOURCE_STATES stateAfter);

    RenderDevice& m_RenderDevice;

    RootSignature*    m_pRootSignature = nullptr;
    CommandSignature* m_pCommandSignature = nullptr;

    Box< StaticBufferAllocator > m_pIndirectDrawAllocator;
    Box< StaticBufferAllocator > m_pTransformAllocator;
    Box< StaticBufferAllocator > m_pMaterialAllocator;
    Box< StaticBufferAllocator > m_pLightAllocator;

    std::unordered_map< std::string, Arc< VertexBuffer > > m_VertexCache;
    std::unordered_map< std::string, Arc< IndexBuffer >  > m_IndexCache;
    std::unordered_map< std::string, Arc< Texture > >      m_TextureCache;

    u32 m_NumMeshes = 0;
};

} // namespace dx12