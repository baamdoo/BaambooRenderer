#pragma once
#include "RenderCommon/RenderNode.h"

namespace baamboo
{

enum class eSamplingStrategy : u32
{
    UniformHemisphere = 0,
    CosineWeighted    = 1,
    // GGX_VNDF       = 2,   // reserved for microfacet sampling
};

enum class eMisHeuristic : u32
{
    Balance = 0,
    Power2  = 1,   // beta = 2
};

enum class eMisForceWeight : u32
{
    Off          = 0,
    NeeOnly      = 1,   // w_NEE = 1   (recovers Phase 3 NEE-only)
    BsdfOnly     = 2,   // w_NEE = 0   (BSDF-only with double-counting absent here)
    UnbiasedHalf = 3,   // w_NEE = 0.5 (uniform average, no IS)
};

class PathTracerNode : public render::RenderNode
{
using Super = render::RenderNode;
public:
    PathTracerNode(render::RenderDevice& rd);
    virtual ~PathTracerNode() = default;

    virtual void Apply(render::CommandContext& context, const SceneRenderView& renderView) override;
    virtual void DrawUI() override;

    virtual void Resize(u32 width, u32 height, u32 depth = 1) override;

    struct Settings
    {
        u32 samplesPerFrame = 1;

        u32 maxDepth = 8;

        eSamplingStrategy samplingStrategy = eSamplingStrategy::CosineWeighted;

        bool bEnableRussianRoulette = true;
        u32  rrMinDepth             = 2;

        bool bFurnaceMode = false;

        float3 furnaceLenv = { 1.0f, 1.0f, 1.0f };

        float testAlbedo = 0.8f;

        bool bEnableNEE = true;

        bool            bEnableMIS     = true;
        eMisHeuristic   misHeuristic   = eMisHeuristic::Balance;
        eMisForceWeight misForceWeight = eMisForceWeight::Off;

        bool bRequestReset = false;
    } settings;

private:
    bool HasViewChanged(const SceneRenderView& renderView) const;

private:
    Arc< render::Texture > m_pDisplay;
    Arc< render::Texture > m_pAccumBuffer;

    Arc< render::ShaderBindingTable > m_pSBT;
    Box< render::RaytracingPipeline > m_pPSO;

    mutable mat4 m_PrevViewProj = mat4(0.0f);
    mutable u32  m_PrevWidth    = 0;
    mutable u32  m_PrevHeight   = 0;
    u64          m_AccumFrames  = 0;
};

} // namespace baamboo
