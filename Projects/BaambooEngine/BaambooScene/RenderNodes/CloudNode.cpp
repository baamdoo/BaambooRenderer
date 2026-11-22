#include "BaambooPch.h"
#include "CloudNode.h"
#include "RenderCommon/RenderDevice.h"
#include "RenderCommon/SceneResource.h"
#include "RenderCommon/CommandContext.h"
#include "BaambooScene/Scene.h"
#include "PipelineFactory.h"

namespace baamboo
{

static inline uint3 HalfResolution(u32 width, u32 height)
{
    return { std::max(1u, width / 2u), std::max(1u, height / 2u), 1u };
}

static inline uint3 QuatResolution(u32 width, u32 height)
{
    return { std::max(1u, width / 4u), std::max(1u, height / 4u), 1u };
}


//-------------------------------------------------------------------------
// Cloud Shape
//-------------------------------------------------------------------------
static constexpr uint3 kBaseNoiseTextureResolution       = { 128, 128, 128 };
static constexpr uint3 kDetailNoiseTextureResolution     = { 64, 64, 64 };
static constexpr uint3 kVerticalProfileTextureResolution = { 256, 256, 1 };

CloudShapeNode::CloudShapeNode(render::RenderDevice& rd, shader::PipelineFactory& pipelineFactory)
    : Super(rd, "CloudShapePass")
{
    using namespace render;
    auto& rm = m_RenderDevice.GetResourceManager();

    m_pCloudWeatherMap  = rm.LoadTexture(TEXTURE_PATH.string() + "CloudsMacroMap.png");
    m_pCloudProfileLUT  = rm.LoadTexture(TEXTURE_PATH.string() + "CloudDensityGradient.png");
    m_pBaseNoiseTexture =
        Texture::Create(
            m_RenderDevice,
            "CloudScattering::BaseNoise",
            {
                .imageType = eImageType::Texture3D,
                .resolution = kBaseNoiseTextureResolution,
                .format = eFormat::RGBA8_UNORM,
                .imageUsage = eTextureUsage_Sample | eTextureUsage_Storage
            });

    m_pCloudShapeBasePSO = pipelineFactory.CreateAndBuildComputePipeline("CloudBaseNoisePSO", { .source = "CloudShapeBaseCS.slang" });
}

CloudShapeNode::~CloudShapeNode()
{
}

void CloudShapeNode::Apply(render::CommandContext& context, const SceneRenderView& renderView)
{
    UNUSED(renderView);
    using namespace render;

    if (m_BaseNoiseBuildCount != m_pCloudShapeBasePSO->BuildCount())
    {
        context.SetRenderPipeline(m_pCloudShapeBasePSO.get());

        context.TransitionBarrier(m_pBaseNoiseTexture, eTextureLayout::General);

        context.StageDescriptor("ID_OutBaseNoise", m_pBaseNoiseTexture);

        context.Dispatch3D< 8, 8, 8 >(kBaseNoiseTextureResolution.x, kBaseNoiseTextureResolution.y, kBaseNoiseTextureResolution.z);
        m_BaseNoiseBuildCount = m_pCloudShapeBasePSO->BuildCount();
    }

    g_FrameData.pCloudWeatherMap   = m_pCloudWeatherMap;
	g_FrameData.pCloudProfileLUT   = m_pCloudProfileLUT;
    g_FrameData.pCloudBaseNoiseLUT = m_pBaseNoiseTexture;
}


//-------------------------------------------------------------------------
// Cloud Scattering
//-------------------------------------------------------------------------
static constexpr uint3 kCloudShadowMapResolution = { 512, 512, 1 };

CloudScatteringNode::CloudScatteringNode(render::RenderDevice& device, shader::PipelineFactory& pipelineFactory)
    : Super(device, "CloudScatteringPass")
{
    using namespace render;

    m_pCloudShadowMap =
        Texture::Create(
            m_RenderDevice,
            "CloudScattering::ShadowMap",
            {
                .resolution = kCloudShadowMapResolution,
                .format     = eFormat::RG11B10_UFLOAT,
                .imageUsage = eTextureUsage_Sample | eTextureUsage_Storage
            });
    m_pCloudScatteringLUT =
        Texture::Create(
            m_RenderDevice,
            "CloudScattering::ScatteringLUT",
            {
                .resolution = HalfResolution(m_RenderDevice.WindowWidth(), m_RenderDevice.WindowHeight()),
                .format     = eFormat::RGBA16_FLOAT,
                .imageUsage = eTextureUsage_Sample | eTextureUsage_Storage
            });
    m_pUprezzedCloudScatteringLUT =
        Texture::Create(
            m_RenderDevice,
            "CloudScattering::UprezzedScatteringLUT",
            {
                .resolution = { m_RenderDevice.WindowWidth(), m_RenderDevice.WindowHeight(), 1 },
                .format     = eFormat::RGBA16_FLOAT,
                .imageUsage = eTextureUsage_Sample | eTextureUsage_Storage | eTextureUsage_TransferSource
            });
    m_pPrevUprezzedCloudScatteringLUT =
        Texture::Create(
            m_RenderDevice,
            "CloudScattering::PrevUprezzedScatteringLUT",
            {
                .resolution = { m_RenderDevice.WindowWidth(), m_RenderDevice.WindowHeight(), 1 },
                .format     = eFormat::RGBA16_FLOAT,
                .imageUsage = eTextureUsage_Sample | eTextureUsage_Storage | eTextureUsage_TransferDest
            });


    m_pCloudShadowPSO        = pipelineFactory.CreateAndBuildComputePipeline("CloudShadowPSO", { .source = "CloudShadowMapCS.slang" });
    m_pCloudRaymarchPSO      = pipelineFactory.CreateAndBuildComputePipeline("CloudRaymarchPSO", { .source = "CloudRaymarchCS.slang" });
    m_pCloudTemporalUprezPSO = pipelineFactory.CreateAndBuildComputePipeline("CloudTemporalUprezPSO", { .source = "CloudTemporalUprezCS.slang" });
}

CloudScatteringNode::~CloudScatteringNode()
{
}

void CloudScatteringNode::Apply(render::CommandContext& context, const SceneRenderView& renderView)
{
    using namespace render;

    if (!m_pBlueNoiseTexture)
    {
        auto& rm = m_RenderDevice.GetResourceManager();
        m_pBlueNoiseTexture = rm.LoadTexture(renderView.cloud.blueNoiseTex);
        assert(m_pBlueNoiseTexture);
    }

    if (m_CurrentUprezRatio != renderView.cloud.uprezRatio)
    {
        uint3 resolution = { 1, 1, 1 };
        switch (renderView.cloud.uprezRatio)
        {
        case eCloudUprezRatio::X1:
            resolution.x = m_RenderDevice.WindowWidth();
            resolution.y = m_RenderDevice.WindowHeight();
            break;
        case eCloudUprezRatio::X2:
            resolution = HalfResolution(m_RenderDevice.WindowWidth(), m_RenderDevice.WindowHeight());
            break;
        case eCloudUprezRatio::X4:
            resolution = QuatResolution(m_RenderDevice.WindowWidth(), m_RenderDevice.WindowHeight());
            break;
        }
        m_pCloudScatteringLUT->Resize(resolution.x, resolution.y, resolution.z);

        m_CurrentUprezRatio = renderView.cloud.uprezRatio;
        m_bHistoryValid = false;
    }

    auto& sr = m_RenderDevice.GetResourceManager().GetSceneResource();

    {
        assert(g_FrameData.pCloudProfileLUT && g_FrameData.pCloudBaseNoiseLUT);

        context.SetRenderPipeline(m_pCloudShadowPSO.get());

        context.TransitionBarrier(g_FrameData.pCloudWeatherMap.lock(), eTextureLayout::ShaderReadOnly);
        context.TransitionBarrier(g_FrameData.pCloudProfileLUT.lock(), eTextureLayout::ShaderReadOnly);
        context.TransitionBarrier(g_FrameData.pCloudBaseNoiseLUT.lock(), eTextureLayout::ShaderReadOnly);
        context.TransitionBarrier(m_pCloudShadowMap, eTextureLayout::General);

        const CloudShadowMapConstants constants =
        {
            .mSunViewProjInv       = renderView.cloud.shadow.mSunViewProjInv,
            .numLightRaymarchSteps = renderView.cloud.numLightRaymarchSteps
        };
        context.SetComputeConstants(sizeof(constants), &constants);
        context.StageDescriptor("ID_Scene", sr.GetSceneGlobals());
        context.StageDescriptor("ID_CloudMacroMap", g_FrameData.pCloudWeatherMap.lock(), g_FrameData.pLinearWrap);
        context.StageDescriptor("ID_CloudProfileLUT", g_FrameData.pCloudProfileLUT.lock(), g_FrameData.pLinearWrap);
        context.StageDescriptor("ID_CloudBaseNoise", g_FrameData.pCloudBaseNoiseLUT.lock(), g_FrameData.pLinearWrap);
        context.StageDescriptor("ID_OutCloudShadowMap", m_pCloudShadowMap);

        context.Dispatch2D< 8, 8 >(kCloudShadowMapResolution.x, kCloudShadowMapResolution.y);

        g_FrameData.pCloudShadowMap = m_pCloudShadowMap;
    }
    {
        context.SetRenderPipeline(m_pCloudRaymarchPSO.get());

        assert(
            g_FrameData.pCloudWeatherMap &&
            g_FrameData.pCloudProfileLUT &&
            g_FrameData.pCloudBaseNoiseLUT &&
            g_FrameData.pDepth &&
            g_FrameData.pTransmittanceLUT &&
            g_FrameData.pAerialPerspectiveLUT &&
            g_FrameData.pAtmosphereAmbientLUT
        );
        context.TransitionBarrier(g_FrameData.pCloudWeatherMap.lock(), eTextureLayout::ShaderReadOnly);
        context.TransitionBarrier(g_FrameData.pCloudProfileLUT.lock(), eTextureLayout::ShaderReadOnly);
        context.TransitionBarrier(g_FrameData.pCloudBaseNoiseLUT.lock(), eTextureLayout::ShaderReadOnly);
        context.TransitionBarrier(g_FrameData.pDepth.lock(), eTextureLayout::ShaderReadOnly);
        context.TransitionBarrier(g_FrameData.pTransmittanceLUT.lock(), eTextureLayout::ShaderReadOnly);
        context.TransitionBarrier(g_FrameData.pAerialPerspectiveLUT.lock(), eTextureLayout::ShaderReadOnly);
        context.TransitionBarrier(g_FrameData.pAtmosphereAmbientLUT.lock(), eTextureLayout::ShaderReadOnly);
        context.TransitionBarrier(m_pBlueNoiseTexture, eTextureLayout::ShaderReadOnly);
        context.TransitionBarrier(m_pCloudScatteringLUT, eTextureLayout::General);

        const CloudRaymarchConstants constants =
        {
            .numCloudRaymarchSteps = renderView.cloud.numCloudRaymarchSteps,
            .numLightRaymarchSteps = renderView.cloud.numLightRaymarchSteps,
            .frameIndex            = static_cast< u32 >(context.RenderSequence())
        };
        context.SetComputeConstants(sizeof(constants), &constants);
        context.StageDescriptor("ID_Scene", sr.GetSceneGlobals());
        context.StageDescriptor("ID_CloudMacroMap", g_FrameData.pCloudWeatherMap.lock(), g_FrameData.pLinearWrap);
        context.StageDescriptor("ID_CloudBaseNoise", g_FrameData.pCloudBaseNoiseLUT.lock(), g_FrameData.pLinearWrap);
        context.StageDescriptor("ID_CloudProfileLUT", g_FrameData.pCloudProfileLUT.lock(), g_FrameData.pLinearWrap);
        context.StageDescriptor("ID_DepthBuffer", g_FrameData.pDepth.lock(), g_FrameData.pPointClamp);
        context.StageDescriptor("ID_TransmittanceLUT", g_FrameData.pTransmittanceLUT.lock(), g_FrameData.pLinearClamp);
        context.StageDescriptor("ID_AerialPerspectiveLUT", g_FrameData.pAerialPerspectiveLUT.lock(), g_FrameData.pLinearClamp);
        context.StageDescriptor("ID_AtmosphereAmbientLUT", g_FrameData.pAtmosphereAmbientLUT.lock(), g_FrameData.pLinearWrap);
        context.StageDescriptor("ID_BlueNoiseLUT", m_pBlueNoiseTexture, g_FrameData.pLinearClamp);
        context.StageDescriptor("ID_OutCloudScatteringLUT", m_pCloudScatteringLUT);

        context.Dispatch2D< 8, 8 >(m_pCloudScatteringLUT->Width(), m_pCloudScatteringLUT->Height());
    }
    {
        context.SetRenderPipeline(m_pCloudTemporalUprezPSO.get());

        context.TransitionBarrier(m_pCloudScatteringLUT, eTextureLayout::ShaderReadOnly);
        context.TransitionBarrier(m_pPrevUprezzedCloudScatteringLUT, eTextureLayout::ShaderReadOnly);
        context.TransitionBarrier(g_FrameData.pDepth.lock(), eTextureLayout::ShaderReadOnly);
        context.TransitionBarrier(m_pUprezzedCloudScatteringLUT, eTextureLayout::General);

        const CloudTemporalUprezConstants constants =
        {
            .blendAlpha   = renderView.cloud.temporalBlendAlpha,
            .historyValid = m_bHistoryValid ? 1u : 0u
        };
        context.SetComputeConstants(sizeof(constants), &constants);
        context.StageDescriptor("ID_Scene", sr.GetSceneGlobals());
        context.StageDescriptor("ID_CloudScatteringLUT", m_pCloudScatteringLUT, g_FrameData.pLinearClamp);
        context.StageDescriptor("ID_PrevUprezzedCloudScatteringLUT", m_pPrevUprezzedCloudScatteringLUT, g_FrameData.pLinearClamp);
        context.StageDescriptor("ID_DepthBuffer", g_FrameData.pDepth.lock(), g_FrameData.pPointClamp);
        context.StageDescriptor("ID_OutUprezzedCloudScatteringLUT", m_pUprezzedCloudScatteringLUT);

        context.Dispatch2D< 8, 8 >(m_pUprezzedCloudScatteringLUT->Width(), m_pUprezzedCloudScatteringLUT->Height());

        context.CopyTexture(m_pPrevUprezzedCloudScatteringLUT, m_pUprezzedCloudScatteringLUT);
        m_bHistoryValid = true;
    }
    g_FrameData.pCloudScatteringLUT = m_pUprezzedCloudScatteringLUT;
}

void CloudScatteringNode::Resize(u32 width, u32 height, u32 depth)
{
    UNUSED(depth);

    uint3 scatteringResolution = { width, height, 1 };
    switch (m_CurrentUprezRatio)
    {
    case eCloudUprezRatio::X1:
        break;
    case eCloudUprezRatio::X2:
        scatteringResolution = HalfResolution(width, height);
        break;
    case eCloudUprezRatio::X4:
        scatteringResolution = QuatResolution(width, height);
        break;
    }

    m_pCloudScatteringLUT->Resize(scatteringResolution.x, scatteringResolution.y, scatteringResolution.z);
    m_pPrevUprezzedCloudScatteringLUT->Resize(width, height, 1);
    m_pUprezzedCloudScatteringLUT->Resize(width, height, 1);
    m_bHistoryValid = false;
}

} // namespace baamboo
