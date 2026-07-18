#pragma once
#include "RenderResources.h"

namespace render
{

// =========================================================================
// GpuPipelineStats
// =========================================================================
struct GpuPipelineStats
{
    u64  iaPrimitives         = 0;     // input-assembler primitives (graphics VS-pipeline only)
    u64  vsInvocations        = 0;     // vertex shader invocations (VS-pipeline)
    u64  clippingInvocations  = 0;     // primitives submitted to clipping (post VS/MS)
    u64  clippingPrimitives   = 0;     // primitives that survived clipping (rasterized)
    u64  fsInvocations        = 0;     // fragment shader invocations
    u64  csInvocations        = 0;     // compute shader invocations (compute dispatches)
    u64  taskInvocations      = 0;     // task (amplification) shader invocations — mesh pipeline
    u64  meshInvocations      = 0;     // mesh shader invocations — mesh pipeline
    bool bHasMeshCounters     = false; // task/mesh invocations are real (not always-zero)
};

// =========================================================================
// GPU Profiling
// =========================================================================
struct GpuProfileEntry
{
    const char*      name;              // caller-owned string (string literal lifetime recommended)
    u32              depth;             // 0 = top-level (typically the implicit "Frame" scope)
    double           elapsedMs;         // wall-clock GPU time for this scope
    bool             bHasStats = false; // true if pipeline statistics were collected for this scope
    GpuPipelineStats stats     = {};    // zero when !bHasStats
};

inline u32 GetGpuMarkerColor(const char* name)
{
    if (!name || !name[0]) return 0xFF808080u; // gray

    // Simple prefix match. Not exhaustive — covers common pass categories.
    auto starts = [name](const char* prefix)
    {
        size_t i = 0;
        while (prefix[i] && name[i] && prefix[i] == name[i]) ++i;
        return prefix[i] == '\0';
    };

    if (starts("Frame"))                                     return 0xFFCCCCFFu; // light blue
    if (starts("Cull") || starts("Phase1Cull") || starts("Phase2Cull")) return 0xFF6060FFu; // red
    if (starts("Draw") || starts("GBuffer") || starts("Phase1Draw") || starts("Phase2Draw")) return 0xFF60FF60u; // green
    if (starts("Build") || starts("HiZ") || starts("Pyramid")) return 0xFF60FFFFu; // yellow
    if (starts("TAA")  || starts("Tone")  || starts("Sharp")) return 0xFFFF80FFu; // magenta
    if (starts("Atmosphere") || starts("Cloud") || starts("Sky")) return 0xFFFF8060u; // cyan-ish
    return 0xFF808080u; // gray
}


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

    // === Resource Operations ===
    virtual void UploadData(const Arc< Buffer >& pDstBuffer, const void* pData, u32 numElements, u64 elemSizeInBytes, u64 dstOffsetInBytes = 0) = 0;
    virtual void CopyBuffer(const Arc< Buffer >& pDstBuffer, const Arc< Buffer >& pSrcBuffer, u64 dstOffsetInBytes = 0, u64 srcOffsetInBytes = 0) = 0;
    virtual void CopyBufferRegion(const Arc< Buffer >& pDstBuffer, const Arc< Buffer >& pSrcBuffer, u64 sizeInBytes, u64 dstOffsetInBytes = 0, u64 srcOffsetInBytes = 0) = 0;
    virtual void CopyTexture(const Arc< Texture >& pDstTexture, const Arc< Texture >& pSrcTexture, u64 offsetInBytes = 0) = 0;

    virtual void ClearBuffer(const Arc< Buffer >& pBuffer, u32 value, u64 offsetInBytes = 0) = 0;
    virtual void ClearTexture(const Arc< Texture >& pTexture, render::eTextureLayout newLayout) = 0;

    // === Barriers ===
    virtual void TransitionBufferToRead(const Arc< Buffer >& pBuffer, render::ePipelineStage dstStage, u64 offsetInBytes = 0, bool bFlushImmediate = false) = 0;
    virtual void TransitionBufferToWrite(const Arc< Buffer >& pBuffer, render::ePipelineStage dstStage, u64 offsetInBytes = 0, bool bFlushImmediate = false) = 0;
    virtual void TransitionTextureToRead(const Arc< Texture >& pTexture, render::ePipelineStage dstStage, u32 subresource = ALL_SUBRESOURCES, bool bFlushImmediate = false) = 0;
    virtual void TransitionTextureToWrite(const Arc< Texture >& pTexture, render::ePipelineStage dstStage, u32 subresource = ALL_SUBRESOURCES, bool bFlushImmediate = false) = 0;
    virtual void TransitionBarrier(const Arc< Texture >& pTexture, eTextureLayout newState, u32 subresource = ALL_SUBRESOURCES, bool flushImmediate = false) = 0;
    virtual void UAVBarrier(const Arc< Buffer >& pBuffer, bool bFlushImmediate = false) = 0;

    virtual void TransitionBufferToIndirectArgs(const Arc< Buffer >& pBuffer, u64 offsetInBytes = 0, bool bFlushImmediate = true)
    {
        TransitionBufferToRead(pBuffer, ePipelineStage::DrawIndirect, offsetInBytes, bFlushImmediate);
    }

    // === Render Target ===
    virtual void BeginRenderPass(Arc< RenderTarget > renderTarget) = 0;
    virtual void EndRenderPass() = 0;

    // === Acceleration Structure ===
    virtual void BuildBLAS(BottomLevelAccelerationStructure& blas) = 0;
    virtual void BuildTLAS(TopLevelAccelerationStructure& tlas) = 0;

    // === Resource Binding ===
    virtual void SetRenderPipeline(ComputePipeline* pPipeline) = 0;
    virtual void SetRenderPipeline(GraphicsPipeline* pPipeline) = 0;
    virtual void SetRenderPipeline(render::RaytracingPipeline* pRenderPipeline) = 0;

    virtual void SetConstants(u32 sizeInBytes, const void* pData, eShaderStage stage, u32 offsetInBytes = 0) = 0;
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

    virtual void SetAccelerationStructure(const std::string& name, TopLevelAccelerationStructure& tlas) = 0;

    virtual void StageDescriptor(const std::string&, Arc< Buffer > buffer, u32 offset = 0) = 0;
    virtual void StageDescriptor(const std::string&, Arc< Texture > texture, Arc< Sampler > samplerInCharge = nullptr, u32 offset = 0) = 0;
    virtual void StageDescriptorMip(const std::string&, Arc< Texture > texture, u32 mipLevel, Arc< Sampler > samplerInCharge = nullptr) = 0;

    // === Draw Commands ===
    virtual void Draw(u32 vertexCount, u32 instanceCount = 1, u32 firstVertex = 0, u32 firstInstance = 0) = 0;
    virtual void DrawIndexed(u32 indexCount, u32 instanceCount = 1, u32 firstIndex = 0, i32 vertexOffset = 0, u32 firstInstance = 0) = 0;
    //virtual void DrawIndirect(Arc< Buffer > argumentBuffer, u32 argumentBufferOffset = 0) = 0;
    //virtual void DrawIndexedIndirect(Arc< Buffer > argumentBuffer, u32 argumentBufferOffset = 0) = 0;
    virtual void DrawMeshTasksIndirect(const Arc< Buffer >& pArgumentBuffer, u64 offsetInBytes, u32 numDraws, u32 strideInBytes) = 0;
    virtual void DrawMeshTasksIndirectCount(const Arc< Buffer >& pArgumentBuffer, u64 offsetInBytes, const Arc< Buffer >& pCountBuffer, u32 numDraws, u32 strideInBytes) = 0;

    // === Compute Commands ===
    virtual void Dispatch(u32 threadGroupCountX, u32 threadGroupCountY, u32 threadGroupCountZ) = 0;
    //virtual void DispatchIndirect(Arc< Buffer > argumentBuffer, u32 argumentBufferOffset = 0) = 0;
    virtual void DispatchRays(ShaderBindingTable& sbt, u32 width, u32 height, u32 depth = 1) = 0;

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

    // === GPU Profiling Markers ===
    virtual void BeginGpuMarker(const char* name, bool bWithStats = false) = 0;
    virtual void EndGpuMarker() = 0;

    virtual const std::vector< GpuProfileEntry >& GetLastFrameProfile() const = 0;

    virtual double GetLastFrameElapsedTime() const = 0;
    void SetRenderSequence(u64 sequence) { m_RenderSequence = sequence; }
    [[nodiscard]]
    u64 RenderSequence() const { return m_RenderSequence; }


private:
    u64 m_RenderSequence = 0;

    template< typename T >
    constexpr T RoundUpAndDivide(T Value, size_t Alignment)
    {
        return (T)((Value + Alignment - 1) / Alignment);
    }
};

// =========================================================================
// GpuScope — RAII helper for paired BeginGpuMarker / EndGpuMarker calls.
// =========================================================================
class GpuScope
{
public:
    GpuScope(CommandContext& ctx, const char* name, bool bWithStats = false) : m_Ctx(ctx) { m_Ctx.BeginGpuMarker(name, bWithStats); }
    ~GpuScope() { m_Ctx.EndGpuMarker(); }
    GpuScope(const GpuScope&) = delete;
    GpuScope& operator=(const GpuScope&) = delete;
private:
    CommandContext& m_Ctx;
};

}

#define BAAMBOO_GPU_SCOPE_CONCAT_INNER_(a, b) a##b
#define BAAMBOO_GPU_SCOPE_CONCAT_(a, b) BAAMBOO_GPU_SCOPE_CONCAT_INNER_(a, b)
#define BAAMBOO_GPU_SCOPE(ctx, name)       ::render::GpuScope BAAMBOO_GPU_SCOPE_CONCAT_(_gpu_scope_, __LINE__)((ctx), name, false)
#define BAAMBOO_GPU_SCOPE_STATS(ctx, name) ::render::GpuScope BAAMBOO_GPU_SCOPE_CONCAT_(_gpu_scope_, __LINE__)((ctx), name, true)
