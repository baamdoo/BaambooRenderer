#pragma once
#include "RenderDevice/Dx12ResourceManager.h"

namespace dx12
{

class CommandList;
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
using TextureHandle = baamboo::ResourceHandle< Texture >;

struct SceneResource
{
    SceneResource(RenderContext& context);
    ~SceneResource();

    void UpdateSceneResources(const SceneRenderView& sceneView);

    VertexBuffer* GetOrUpdateVertex(u32 entity, std::string_view filepath, const void* pData, u32 count);
    IndexBuffer*  GetOrUpdateIndex(u32 entity, std::string_view filepath, const void* pData, u32 count);
    Texture*      GetOrLoadTexture(u32 entity, std::string_view filepath);

    [[nodiscard]]
    inline RootSignature* GetSceneRootSignature() const { return m_pRootSignature; }
    [[nodiscard]]
    ID3D12CommandSignature* GetSceneD3D12CommandSignature() const;

    [[nodiscard]]
    StructuredBuffer* GetIndirectBuffer() const;
    [[nodiscard]]
    StructuredBuffer* GetTransformBuffer() const;
    [[nodiscard]]
    StructuredBuffer* GetMaterialBuffer() const;

    [[nodiscard]]
    inline u32 NumMeshes() const { return m_numMeshes; }

    // TEMP
    std::vector< D3D12_CPU_DESCRIPTOR_HANDLE > srvs;

private:
    void ResetFrameBuffers();
    void UpdateFrameBuffer(const void* pData, u32 count, u64 elementSizeInBytes, StaticBufferAllocator* pTargetBuffer);

    RenderContext& m_RenderContext;

    RootSignature*    m_pRootSignature = nullptr;
    CommandSignature* m_pCommandSignature = nullptr;

    StaticBufferAllocator* m_pIndirectDrawBufferPool = nullptr;
    StaticBufferAllocator* m_pTransformBufferPool = nullptr;
    StaticBufferAllocator* m_pMaterialBufferPool = nullptr;

    std::unordered_map< std::string, VertexBuffer* > m_vertexCache;
    std::unordered_map< std::string, IndexBuffer*  > m_indexCache;
    std::unordered_map< std::string, Texture* >      m_textureCache;

    u32 m_numMeshes = 0;
};

} // namespace dx12