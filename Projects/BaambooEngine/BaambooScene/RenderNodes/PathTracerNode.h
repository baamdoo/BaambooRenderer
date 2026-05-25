#pragma once
#include "RenderCommon/RenderNode.h"

namespace baamboo
{

enum class ePathTracerDebugMode : u32
{
    Off          = 0,
    Albedo       = 1,
    Emission     = 2,
    MaterialType = 3,
    SelectedLobe = 4,
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
        u32  samplesPerFrame = 1;
        u32  maxDepth        = 8;
        bool bRequestReset   = false;

        ePathTracerDebugMode debugMode = ePathTracerDebugMode::Off;
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
