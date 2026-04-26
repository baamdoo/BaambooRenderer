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
          .AddMissShader("ShadowMiss")
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
          .SetMaxPayloadSize(sizeof(float) * 16)
          .SetMaxAttributeSize(sizeof(float) * 2)
          .SetMaxRecursionDepth(1) // iterative walk in RayGen
          .Build();

    m_pSBT = ShaderBindingTable::Create(m_RenderDevice, "PathTracerSBT");
    m_pSBT->SetRayGenerationRecord(m_pPSO->GetShaderIdentifier("RayGen"), nullptr, 0)
          .AddMissRecord("PrimaryMiss",       m_pPSO->GetShaderIdentifier("PrimaryMiss"))
          .AddMissRecord("ShadowMiss",        m_pPSO->GetShaderIdentifier("ShadowMiss"))
          .AddHitGroupRecord("PrimaryHitGroup", m_pPSO->GetShaderIdentifier("PrimaryHitGroup"))
          .AddHitGroupRecord("ShadowHitGroup",  m_pPSO->GetShaderIdentifier("ShadowHitGroup"))
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
        u32   maxDepth;
        u32   enableRR;
        u32   rrMinDepth;
        u32   samplingStrategy;

        u32   furnaceMode;
        float testAlbedo;
        u32   neeEnable;
        u32   _pad0;

        float furnaceLenvR;
        float furnaceLenvG;
        float furnaceLenvB;
        float _pad1;

        u32   misEnable;
        u32   misHeuristic;
        u32   misForceWeight;
        u32   _pad2;
    } cb;
    cb.maxDepth         = settings.maxDepth;
    cb.enableRR         = settings.bEnableRussianRoulette ? 1u : 0u;
    cb.rrMinDepth       = settings.rrMinDepth;
    cb.samplingStrategy = static_cast<u32>(settings.samplingStrategy);
    cb.furnaceMode      = settings.bFurnaceMode ? 1u : 0u;
    cb.testAlbedo       = settings.testAlbedo;
    cb.neeEnable        = settings.bEnableNEE ? 1u : 0u;
    cb._pad0            = 0u;
    cb.furnaceLenvR     = settings.furnaceLenv.x;
    cb.furnaceLenvG     = settings.furnaceLenv.y;
    cb.furnaceLenvB     = settings.furnaceLenv.z;
    cb._pad1            = 0.0f;
    cb.misEnable        = settings.bEnableMIS ? 1u : 0u;
    cb.misHeuristic     = static_cast<u32>(settings.misHeuristic);
    cb.misForceWeight   = static_cast<u32>(settings.misForceWeight);
    cb._pad2            = 0u;

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

        const char* samplerNames[] = { "Uniform Hemisphere", "Cosine-Weighted" };
        int currentSampler = static_cast<int>(s.samplingStrategy);
        if (ImGui::Combo("Sampling", &currentSampler, samplerNames, IM_ARRAYSIZE(samplerNames)))
        {
            s.samplingStrategy = static_cast<eSamplingStrategy>(currentSampler);
            s.bRequestReset    = true;
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
        ImGui::TextUnformatted("Next Event Estimation (NEE)");

        if (ImGui::Checkbox("Enable NEE", &s.bEnableNEE))
            s.bRequestReset = true;

        ImGui::TextDisabled("With MIS off, BSDF random walks miss analytic lights -- only NEE reaches them.");
        ImGui::TextDisabled("With MIS on,  BSDF samples that hit a light contribute too.");

        ImGui::Separator();
        ImGui::TextUnformatted("Multiple Importance Sampling (MIS)");

        if (ImGui::Checkbox("Enable MIS (BSDF + NEE)", &s.bEnableMIS))
            s.bRequestReset = true;

        if (s.bEnableMIS)
        {
            const char* heuristics[] = { "Balance", "Power(beta=2)" };
            int h = static_cast<int>(s.misHeuristic);
            if (ImGui::Combo("Heuristic", &h, heuristics, IM_ARRAYSIZE(heuristics)))
            {
                s.misHeuristic  = static_cast<eMisHeuristic>(h);
                s.bRequestReset = true;
            }

            const char* forces[] = {
                "Off (use heuristic)",
                "Force w_NEE=1 (NEE-only)",
                "Force w_NEE=0 (BSDF-only)",
                "Force w_NEE=0.5 (avg)"
            };
            int fw = static_cast<int>(s.misForceWeight);
            if (ImGui::Combo("Force Weight (debug)", &fw, forces, IM_ARRAYSIZE(forces)))
            {
                s.misForceWeight = static_cast<eMisForceWeight>(fw);
                s.bRequestReset  = true;
            }
        }

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
