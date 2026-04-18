#include "BaambooPch.h"
#include "PathTracerNode.h"

#include "RenderCommon/RenderDevice.h"
#include "RenderCommon/CommandContext.h"

#include "BaambooScene/Scene.h"

#include <imgui.h>

namespace baamboo
{

PathTracerNode::PathTracerNode(render::RenderDevice& rd)
    : Super(rd, "PathTracer_Phase1")
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
          .SetMaxPayloadSize(sizeof(float) * 16)
          .SetMaxAttributeSize(sizeof(float) * 2)
          .SetMaxRecursionDepth(1) // iterative walk in RayGen
          .Build();

    m_pSBT = ShaderBindingTable::Create(m_RenderDevice, "PathTracerSBT");
    m_pSBT->SetRayGenerationRecord(m_pPSO->GetShaderIdentifier("RayGen"), nullptr, 0)
          .AddMissRecord("PrimaryMiss", m_pPSO->GetShaderIdentifier("PrimaryMiss"))
          .AddHitGroupRecord("PrimaryHitGroup", m_pPSO->GetShaderIdentifier("PrimaryHitGroup"))
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

    const u32 w = m_RenderDevice.WindowWidth();
    const u32 h = m_RenderDevice.WindowHeight();
    if (w != m_PrevWidth || h != m_PrevHeight)
    {
        m_PrevWidth    = w;
        m_PrevHeight   = h;
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

    bool bResetThisFrame = false;
    if (settings.bRequestReset || m_AccumFrames == 0 || HasViewChanged(renderView))
    {
        bResetThisFrame        = true;
        settings.bRequestReset = false;   // consume the one-shot
        m_AccumFrames          = 0;
    }

    context.SetRenderPipeline(m_pPSO.get());

    context.TransitionBarrier(pSkyboxLUT,     eTextureLayout::ShaderReadOnly);
    context.TransitionBarrier(m_pDisplay,     eTextureLayout::General);
    context.TransitionBarrier(m_pAccumBuffer, eTextureLayout::General);

    // Layout must match the `PushConstants` cbuffer in PathTracerLIB.hlsl.
    struct PushConstants
    {
        u32   frameIndex;
        u32   accumReset;
        u32   numSamples;
        u32   maxDepth;
        u32   enableRR;
        u32   rrMinDepth;
        u32   furnaceMode;
        float furnaceLenvR;
        float furnaceLenvG;
        float furnaceLenvB;
        float testAlbedo;
    } pc;
    pc.frameIndex   = static_cast<u32>(renderView.frame);
    pc.accumReset   = bResetThisFrame ? 1u : 0u;
    pc.numSamples   = settings.samplesPerFrame;
    pc.maxDepth     = settings.maxDepth;
    pc.enableRR     = settings.bEnableRussianRoulette ? 1u : 0u;
    pc.rrMinDepth   = settings.rrMinDepth;
    pc.furnaceMode  = settings.bFurnaceMode ? 1u : 0u;
    pc.furnaceLenvR = settings.furnaceLenv.x;
    pc.furnaceLenvG = settings.furnaceLenv.y;
    pc.furnaceLenvB = settings.furnaceLenv.z;
    pc.testAlbedo   = settings.testAlbedo;

    context.SetComputeConstants(sizeof(pc), &pc);
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
    if (ImGui::Begin("Path Tracer (Phase 1)"))
    {
        auto& s = settings;

        int paths = static_cast<int>(s.samplesPerFrame);
        if (ImGui::SliderInt("Paths / frame", &paths, 1, 16))
        {
            s.samplesPerFrame = static_cast<u32>(paths);
            s.bRequestReset = true;
        }

        int depth = static_cast<int>(s.maxDepth);
        if (ImGui::SliderInt("Max Depth", &depth, 1, 64))
        {
            s.maxDepth = static_cast<u32>(depth);
            s.bRequestReset = true;
        }

        if (ImGui::Checkbox("Russian Roulette", &s.bEnableRussianRoulette))
            s.bRequestReset = true;

        if (s.bEnableRussianRoulette)
        {
            int rrMin = static_cast<int>(s.rrMinDepth);
            if (ImGui::SliderInt("RR Min Depth", &rrMin, 0, 4))
            {
                s.rrMinDepth = static_cast<u32>(rrMin);
                s.bRequestReset = true;
            }
        }

        if (ImGui::SliderFloat("Test Albedo (rho)", &s.testAlbedo, 0.0f, 1.0f))
            s.bRequestReset = true;

        // Quick-set buttons for the furnace test sweep
        ImGui::SameLine();
        if (ImGui::SmallButton("1.0")) { s.testAlbedo = 1.0f; s.bRequestReset = true; }
        ImGui::SameLine();
        if (ImGui::SmallButton("0.5")) { s.testAlbedo = 0.5f; s.bRequestReset = true; }
        ImGui::SameLine();
        if (ImGui::SmallButton("0.0")) { s.testAlbedo = 0.0f; s.bRequestReset = true; }

        ImGui::Separator();
        ImGui::TextUnformatted("Furnace Test");

        if (ImGui::Checkbox("Enable Furnace Mode", &s.bFurnaceMode))
            s.bRequestReset = true;

        if (s.bFurnaceMode)
        {
            if (ImGui::ColorEdit3("Environment Radiance (L_env)", &s.furnaceLenv.x,
                ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR))
            {
                s.bRequestReset = true;
            }
        }

        ImGui::Separator();
        if (ImGui::Button("Reset Accumulation"))
            s.bRequestReset = true;
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
