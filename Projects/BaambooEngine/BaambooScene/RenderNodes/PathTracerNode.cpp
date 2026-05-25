#include "BaambooPch.h"
#include "PathTracerNode.h"

#include "RenderCommon/CommandContext.h"
#include "RenderCommon/RenderDevice.h"

#include "BaambooScene/Scene.h"

#include <imgui.h>

namespace baamboo
{

PathTracerNode::PathTracerNode(render::RenderDevice& rd)
    : Super(rd, "PathTracer")
{
    using namespace render;

    m_pDisplay =
        Texture::Create(
            m_RenderDevice,
            "PathTracer::Display",
            {
                .resolution = { m_RenderDevice.WindowWidth(), m_RenderDevice.WindowHeight(), 1 },
                .format     = eFormat::RGBA16_FLOAT,
                .imageUsage = eTextureUsage_Sample | eTextureUsage_Storage | eTextureUsage_TransferSource
            });

    m_pAccumBuffer =
        Texture::Create(
            m_RenderDevice,
            "PathTracer::Accum",
            {
                .resolution = { m_RenderDevice.WindowWidth(), m_RenderDevice.WindowHeight(), 1 },
                .format     = eFormat::RGBA32_FLOAT,
                .imageUsage = eTextureUsage_Sample | eTextureUsage_Storage
            });

    m_pPSO = RaytracingPipeline::Create(m_RenderDevice, "PathTracerPSO");
    m_pPSO->SetShaderLibrary(
            Shader::Create(m_RenderDevice, "PathTracerLIB",
                {
                    .stage    = eShaderStage::RayGeneration,
                    .filename = "PathTracerLIB"
                }))
          .SetRayGenerationShader("RayGen")
          .AddMissShader("PrimaryMiss")
          .AddHitGroup(
              {
                  .hitGroupName           = "PrimaryHitGroup",
                  .closestHitShaderExport = "ClosestHit_Primary",
              })
          .AddHitGroup(
              {
                  .hitGroupName       = "ShadowHitGroup",
                  .anyHitShaderExport = "AnyHit_Shadow",
              })
          .SetMaxPayloadSize(sizeof(float) * 33)
          .SetMaxAttributeSize(sizeof(float) * 2)
          .SetMaxRecursionDepth(1)
          .Build();

    m_pSBT = ShaderBindingTable::Create(m_RenderDevice, "PathTracerSBT");
    m_pSBT->SetRayGenerationRecord(m_pPSO->GetShaderIdentifier("RayGen"), nullptr, 0)
        .AddMissRecord("PrimaryMiss", m_pPSO->GetShaderIdentifier("PrimaryMiss"))
        .AddHitGroupRecord("PrimaryHitGroup", m_pPSO->GetShaderIdentifier("PrimaryHitGroup"))
        .AddHitGroupRecord("ShadowHitGroup", m_pPSO->GetShaderIdentifier("ShadowHitGroup"))
        .Build();

    m_PrevWidth  = m_RenderDevice.WindowWidth();
    m_PrevHeight = m_RenderDevice.WindowHeight();
}

bool PathTracerNode::HasViewChanged(const SceneRenderView& renderView) const
{
    const mat4 curViewProj = renderView.camera.mProj * renderView.camera.mView;

    const float* a = &m_PrevViewProj[0][0];
    const float* b = &curViewProj[0][0];
    for (int i = 0; i < 16; ++i)
    {
        if (a[i] != b[i])
        {
            m_PrevViewProj = curViewProj;
            return true;
        }
    }

    const u32 width  = m_RenderDevice.WindowWidth();
    const u32 height = m_RenderDevice.WindowHeight();
    if (width != m_PrevWidth || height != m_PrevHeight)
    {
        m_PrevWidth    = width;
        m_PrevHeight   = height;
        m_PrevViewProj = curViewProj;
        return true;
    }

    return false;
}

void PathTracerNode::Apply(render::CommandContext& context, const SceneRenderView& renderView)
{
    using namespace render;

    auto& rm = m_RenderDevice.GetResourceManager();

    auto pTLAS = rm.GetSceneResource().GetTLAS();
    if (!pTLAS || !pTLAS->IsBuilt())
    {
        g_FrameData.pColor = rm.GetFlatBlackTexture();
        return;
    }

    const auto& pSkyboxLUT = g_FrameData.pSkyboxLUT.valid() ? g_FrameData.pSkyboxLUT.lock() : rm.GetFlatBlackTextureCube();

    const bool bViewChanged = HasViewChanged(renderView);
    bool bResetThisFrame = false;
    if (settings.bRequestReset || m_AccumFrames == 0 || bViewChanged)
    {
        bResetThisFrame        = true;
        settings.bRequestReset = false;
        m_AccumFrames          = 0;
    }

    context.SetRenderPipeline(m_pPSO.get());

    context.TransitionBarrier(pSkyboxLUT,     eTextureLayout::ShaderReadOnly);
    context.TransitionBarrier(m_pDisplay,     eTextureLayout::General);
    context.TransitionBarrier(m_pAccumBuffer, eTextureLayout::General);

    struct PushConstants
    {
        u32 frameIndex;
        u32 accumReset;
        u32 numSamples;
    } pc;
    pc.frameIndex = static_cast<u32>(renderView.frame);
    pc.accumReset = bResetThisFrame ? 1u : 0u;
    pc.numSamples = settings.samplesPerFrame;

    struct PathTracerSettings
    {
        u32 maxDepth;
        u32 debugMode;
        u32 padding1;
        u32 padding2;
    } cb;
    cb.maxDepth  = settings.maxDepth;
    cb.debugMode = static_cast<u32>(settings.debugMode);
    cb.padding1  = 0;
    cb.padding2  = 0;

    context.SetComputeConstants(sizeof(pc), &pc);
    context.SetComputeDynamicUniformBuffer("g_Settings", cb);
    context.SetAccelerationStructure("g_Scene", *pTLAS);

    context.StageDescriptor("g_Skybox",      pSkyboxLUT);
    context.StageDescriptor("g_AccumBuffer", m_pAccumBuffer);
    context.StageDescriptor("g_Display",     m_pDisplay);

    context.DispatchRays(*m_pSBT, m_pDisplay->Width(), m_pDisplay->Height());

    ++m_AccumFrames;

    g_FrameData.pColor = m_pDisplay;
}

void PathTracerNode::DrawUI()
{
    if (ImGui::Begin("Path Tracer Reference"))
    {
        auto& s = settings;

        int samples = static_cast<int>(s.samplesPerFrame);
        if (ImGui::SliderInt("Paths / frame", &samples, 1, 16))
        {
            s.samplesPerFrame = static_cast<u32>(samples);
            s.bRequestReset   = true;
        }

        int depth = static_cast<int>(s.maxDepth);
        if (ImGui::SliderInt("Max Depth", &depth, 1, 128))
        {
            s.maxDepth      = static_cast<u32>(depth);
            s.bRequestReset = true;
        }

        if (ImGui::Button("Reset Accumulation"))
            s.bRequestReset = true;

        ImGui::Separator();

        const char* debugModes[] = { "Off", "Albedo", "Emission", "Material Type", "Selected Lobe" };
        int debugMode = static_cast<int>(s.debugMode);
        if (ImGui::Combo("Debug Mode", &debugMode, debugModes, IM_ARRAYSIZE(debugModes)))
        {
            s.debugMode     = static_cast<ePathTracerDebugMode>(debugMode);
            s.bRequestReset = true;
        }
    }
    ImGui::End();
}

void PathTracerNode::Resize(u32 width, u32 height, u32 depth)
{
    UNUSED(depth);

    if (m_pDisplay)
        m_pDisplay->Resize(width, height, 1);
    if (m_pAccumBuffer)
        m_pAccumBuffer->Resize(width, height, 1);

    m_AccumFrames = 0;
}

} // namespace baamboo
