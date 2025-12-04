#pragma once
#include "RenderResources.h"

namespace render
{

enum class eResourceState
{
    Common                  = 0,
    VertexAndConstantBuffer = 1 << 0,
    IndexBuffer             = 1 << 1,
    RenderTarget            = 1 << 2,
    UnorderedAccess         = 1 << 3,
    DepthWrite              = 1 << 4,
    DepthRead               = 1 << 5,
    ShaderResource          = 1 << 6,
    StreamOut               = 1 << 7,
    IndirectArgument        = 1 << 8,
    CopyDest                = 1 << 9,
    CopySource              = 1 << 10,
    Present                 = 1 << 11,
};

class CommandContext : public ArcBase
{
public:
    virtual ~CommandContext() = default;

    // === Texture Operations ===
    virtual void CopyBuffer(Arc< Buffer > pDstBuffer, Arc< Buffer > pSrcBuffer, u64 offsetInBytes = 0) = 0;
    virtual void CopyTexture(Arc< Texture > pDstTexture, Arc< Texture > pSrcTexture, u64 offsetInBytes = 0) = 0;

    virtual void ClearTexture(Arc< render::Texture > pTexture, render::eTextureLayout newLayout) = 0;

    // === Barriers ===
    virtual void TransitionBarrier(Arc< Texture > texture, eTextureLayout newState, u32 subresource = ALL_SUBRESOURCES, bool flushImmediate = false) = 0;

    // === Render Target ===
    virtual void BeginRenderPass(Arc< RenderTarget > renderTarget) = 0;
    virtual void EndRenderPass() = 0;

    // === Resource Binding ===
    virtual void SetRenderPipeline(ComputePipeline* pPipeline) = 0;
    virtual void SetRenderPipeline(GraphicsPipeline* pPipeline) = 0;

    virtual void SetComputeConstants(u32 sizeInBytes, const void* pData, u32 offsetInBytes = 0) = 0;
    virtual void SetGraphicsConstants(u32 sizeInBytes, const void* pData, u32 offsetInBytes = 0) = 0;

    virtual void SetComputeDynamicUniformBuffer(const std::string& name, u32 size, const void* pData) = 0;
    template< typename T >
    void SetComputeDynamicUniformBuffer(const std::string& name, const T& data)
    {
        SetComputeDynamicUniformBuffer(name, sizeof(T), &data);
    }
    virtual void SetGraphicsDynamicUniformBuffer(const std::string& name, u32 size, const void* pData) = 0;
    template< typename T >
    void SetGraphicsDynamicUniformBuffer(const std::string& name, const T& data)
    {
        SetGraphicsDynamicUniformBuffer(name, sizeof(T), &data);
    }
    
    virtual void SetComputeShaderResource(const std::string&, Arc< Buffer > buffer) = 0;
    virtual void SetGraphicsShaderResource(const std::string&, Arc< Buffer > buffer) = 0;
    virtual void SetComputeShaderResource(const std::string&, Arc< Texture > texture, Arc< Sampler > samplerInCharge = nullptr) = 0;
    virtual void SetGraphicsShaderResource(const std::string&, Arc< Texture > texture, Arc< Sampler > samplerInCharge = nullptr) = 0;

    virtual void StageDescriptor(const std::string&, Arc< Buffer > buffer, u32 offset = 0) = 0;
    virtual void StageDescriptor(const std::string&, Arc< Texture > texture, Arc< Sampler > samplerInCharge = nullptr, u32 offset = 0) = 0;

    // === Draw Commands ===
    virtual void Draw(u32 vertexCount, u32 instanceCount = 1, u32 firstVertex = 0, u32 firstInstance = 0) = 0;
    virtual void DrawIndexed(u32 indexCount, u32 instanceCount = 1, u32 firstIndex = 0, i32 vertexOffset = 0, u32 firstInstance = 0) = 0;
    //virtual void DrawIndirect(Arc< Buffer > argumentBuffer, u32 argumentBufferOffset = 0) = 0;
    //virtual void DrawIndexedIndirect(Arc< Buffer > argumentBuffer, u32 argumentBufferOffset = 0) = 0;
    virtual void DrawScene(const SceneResource& sceneResource) = 0;

    // === Compute Commands ===
    virtual void Dispatch(u32 threadGroupCountX, u32 threadGroupCountY, u32 threadGroupCountZ) = 0;
    //virtual void DispatchIndirect(Arc< Buffer > argumentBuffer, u32 argumentBufferOffset = 0) = 0;

    template< u32 numThreadsPerGroupX >
    void Dispatch1D(u32 numThreadsX)
    {
        u32 numGroupsX = RoundUpAndDivide(numThreadsX, numThreadsPerGroupX);
        Dispatch(numGroupsX, 1, 1);
    }

    template< u32 numThreadsPerGroupX, u32 numThreadsPerGroupY >
    void Dispatch2D(u32 numThreadsX, u32 numThreadsY)
    {
        u32 numGroupsX = RoundUpAndDivide(numThreadsX, numThreadsPerGroupX);
        u32 numGroupsY = RoundUpAndDivide(numThreadsY, numThreadsPerGroupY);
        Dispatch(numGroupsX, numGroupsY, 1);
    }

    template< u32 numThreadsPerGroupX, u32 numThreadsPerGroupY, u32 numThreadsPerGroupZ >
    void Dispatch3D(u32 numThreadsX, u32 numThreadsY, u32 numThreadsZ)
    {
        u32 numGroupsX = RoundUpAndDivide(numThreadsX, numThreadsPerGroupX);
        u32 numGroupsY = RoundUpAndDivide(numThreadsY, numThreadsPerGroupY);
        u32 numGroupsZ = RoundUpAndDivide(numThreadsZ, numThreadsPerGroupZ);
        Dispatch(numGroupsX, numGroupsY, numGroupsZ);
    }

private:
    template< typename T >
    constexpr T RoundUpAndDivide(T Value, size_t Alignment)
    {
        return (T)((Value + Alignment - 1) / Alignment);
    }
};

}