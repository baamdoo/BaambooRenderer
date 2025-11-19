#include "BaambooPch.h"
#include "CloudNode.h"
#include "RenderCommon/RenderDevice.h"
#include "RenderCommon/CommandContext.h"
#include "BaambooScene/Scene.h"

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
static constexpr uint3 BASE_NOISE_TEXTURE_RESOLUTION       = { 128, 128, 128 };
static constexpr uint3 DETAIL_NOISE_TEXTURE_RESOLUTION     = { 64, 64, 64 };
static constexpr uint3 VERTICAL_PROFILE_TEXTURE_RESOLUTION = { 256, 256, 1 };
static constexpr uint3 WEATHERMAP_TEXTURE_RESOLUTION       = { 1024, 1024, 1 };

CloudShapeNode::CloudShapeNode(render::RenderDevice& rd)
    : Super(rd, "CloudShapePass")
{
    using namespace render;

    /*m_pBaseNoiseTexture =
        Texture::Create(
            m_RenderDevice,
            "CloudShape::BaseNoise",
            {
                .imageType  = eImageType::Texture3D,
                .resolution = BASE_NOISE_TEXTURE_RESOLUTION,
                .format     = eFormat::RGBA16_FLOAT,
                .imageUsage = eTextureUsage_Sample | eTextureUsage_Storage
            });*/

    /*m_pDetailNoiseTexture =
        Texture::Create(
            m_RenderDevice,
            "CloudShape::DetailNoise",
            {
                .imageType  = eImageType::Texture3D,
                .resolution = DETAIL_NOISE_TEXTURE_RESOLUTION,
                .format     = eFormat::RGBA16_FLOAT,
                .imageUsage = eTextureUsage_Sample | eTextureUsage_Storage
            });*/
    /*m_pVerticalProfileTexture =
        Texture::Create(
            m_RenderDevice,
            "CloudShape::VerticalProfile",
            {
                .resolution = VERTICAL_PROFILE_TEXTURE_RESOLUTION,
                .format     = eFormat::R8_UNORM,
                .imageUsage = eTextureUsage_Sample | eTextureUsage_Storage | eTextureUsage_TransferSource
            });*/

    /*m_pCloudShapeBasePSO = ComputePipeline::Create(m_RenderDevice, "CloudShapeBasePSO");
    m_pCloudShapeBasePSO->SetComputeShader(
        Shader::Create(m_RenderDevice, "CloudShapeBaseCS",
            {
                .stage    = eShaderStage::Compute,
                .filename = "CloudShapeBase"
            })).Build();

    m_pCloudShapeDetailPSO = ComputePipeline::Create(m_RenderDevice, "CloudShapeDetailPSO");
    m_pCloudShapeDetailPSO->SetComputeShader(
        Shader::Create(m_RenderDevice, "CloudShapeDetailCS",
            {
                .stage    = eShaderStage::Compute,
                .filename = "CloudShapeDetail"
            })).Build();

    m_pVerticalProfilePSO = ComputePipeline::Create(m_RenderDevice, "CloudVerticalProfilePSO");
    m_pVerticalProfilePSO->SetComputeShader(
        Shader::Create(m_RenderDevice, "CloudVerticalProfileCS",
            {
                .stage    = eShaderStage::Compute,
                .filename = "CloudVerticalProfile"
            })).Build();*/

    /*m_pWeatherMapPSO = ComputePipeline::Create(m_RenderDevice, "CloudWeatherMapPSO");
    m_pWeatherMapPSO->SetComputeShader(
        Shader::Create(m_RenderDevice, "CloudWeatherMapCS",
            {
                .stage    = eShaderStage::Compute,
                .filename = "CloudWeatherMap"
            })).Build();*/
}

CloudShapeNode::~CloudShapeNode()
{
}

void CloudShapeNode::Apply(render::CommandContext& context, const SceneRenderView& renderView)
{
    using namespace render;
    {
        //context.SetRenderPipeline(m_pCloudShapeBasePSO.get());

        //context.TransitionBarrier(m_pBaseNoiseTexture, eTextureLayout::General);

        //struct
        //{
        //    // Perlin
        //    float fPerlin;
        //    u32   octaves;
        //    float persistence;
        //    float lacunarityPerlin;

        //    // Worley
        //    float fWorley;
        //    float lacunarityWorley;

        //    // misc
        //    float time_s;
        //} constant = { 4.0, 7, exp(-0.85f), 2.0f, 6.0f, 2.0f, renderView.time };
        //context.SetComputeConstants(sizeof(constant), &constant);
        //context.StageDescriptor("g_BaseNoise", m_pBaseNoiseTexture);

        //context.Dispatch3D< 8, 8, 8 >(BASE_NOISE_TEXTURE_RESOLUTION.x, BASE_NOISE_TEXTURE_RESOLUTION.y, BASE_NOISE_TEXTURE_RESOLUTION.z);
    }
    {
        //context.SetRenderPipeline(m_pCloudShapeDetailPSO.get());

        //context.TransitionBarrier(m_pDetailNoiseTexture, eTextureLayout::General);

        //struct
        //{
        //    // r-channel
        //    float rWeight;
        //    float rFrequency;
        //    u32   rOctaves;
        //    float rPersistence;
        //    float rLacunarity;

        //    // g-channel
        //    float gWeight;
        //    float gFrequency;
        //    u32   gOctaves;
        //    float gPersistence;
        //    float gLacunarity;

        //    // b-channel
        //    float bWeight;
        //    float bFrequency;
        //    u32   bOctaves;
        //    float bPersistence;
        //    float bLacunarity;
        //} constant = { 1.0f, 3.0, 16, 0.5f, 2.0f, 0.5f, 8.0f, 4, 0.5f, 2.0f, 0.25f, 16.0f, 4, 0.5f, 2.0f };
        //context.SetComputeConstants(sizeof(constant), &constant);
        //context.StageDescriptor("g_DetailNoise", m_pDetailNoiseTexture);

        //context.Dispatch3D< 8, 8, 8 >(DETAIL_NOISE_TEXTURE_RESOLUTION.x, DETAIL_NOISE_TEXTURE_RESOLUTION.y, DETAIL_NOISE_TEXTURE_RESOLUTION.z);
    }
    {
        //context.SetRenderPipeline(m_pVerticalProfilePSO.get());

        //context.TransitionBarrier(m_pVerticalProfileTexture, eTextureLayout::General);

        ////struct
        ////{
        ////    // cloud type
        ////    float fType;
        ////    i32   oType;
        ////    float pType;
        ////    float lType;

        ////    // coverage
        ////    float fCoverage;
        ////    i32   oCoverage;
        ////    float pCoverage;
        ////    float lCoverage;
        ////} constant = { 100.0f, 10, 0.75f, 3.0f, 76.0f, 4, 0.5f, 3.0f };
        ////context.SetComputeConstants(sizeof(constant), &constant);
        //context.StageDescriptor("g_DetailNoise", m_pVerticalProfileTexture);

        //context.Dispatch2D< 8, 8 >(VERTICAL_PROFILE_TEXTURE_RESOLUTION.x, VERTICAL_PROFILE_TEXTURE_RESOLUTION.y);
    }
    {
        /*context.SetRenderPipeline(m_pWeatherMapPSO.get());

        context.TransitionBarrier(m_pWeatherMapTexture, eTextureLayout::General);

        context.StageDescriptor("g_WeatherMap", m_pWeatherMapTexture);

        context.Dispatch2D< 8, 8 >(WEATHERMAP_TEXTURE_RESOLUTION.x, WEATHERMAP_TEXTURE_RESOLUTION.y);*/
    }
}


//-------------------------------------------------------------------------
// Cloud Scattering
//-------------------------------------------------------------------------
static constexpr uint3 CLOUD_SHADOWMAP_RESOLUTION = { 2048, 2048, 1 };

CloudScatteringNode::CloudScatteringNode(render::RenderDevice& device)
    : Super(device, "CloudScatteringPass")
{
    using namespace render;
    auto& rm = m_RenderDevice.GetResourceManager();

    m_pBaseNoiseTexture    = rm.LoadTexture(TEXTURE_PATH.string() + "PerlinWorley_Volume.dds", true);
    m_pErosionNoiseTexture = rm.LoadTexture(TEXTURE_PATH.string() + "NubisVoxelCloudNoise128_Volume.dds");

    m_pDensityTopGradientTexture    = rm.LoadTexture(TEXTURE_PATH.string() + "top_density_gradient.png");
    m_pDensityBottomGradientTexture = rm.LoadTexture(TEXTURE_PATH.string() + "bottom_density_gradient.png");

    m_pCloudShadowMap =
        Texture::Create(
            m_RenderDevice,
            "CloudScattering::ShadowMap",
            {
                .resolution = CLOUD_SHADOWMAP_RESOLUTION,
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


    m_pCloudShadowPSO = ComputePipeline::Create(m_RenderDevice, "CloudShadowPSO");
    m_pCloudShadowPSO->SetComputeShader(
        Shader::Create(m_RenderDevice, "CloudShadowMapCS",
            {
                .stage    = eShaderStage::Compute,
                .filename = "CloudShadowMapCS"
            })).Build();

    m_pCloudRaymarchPSO = ComputePipeline::Create(m_RenderDevice, "CloudScatteringPSO");
    m_pCloudRaymarchPSO->SetComputeShader(
        Shader::Create(m_RenderDevice, "CloudScatteringCS",
            {
                .stage    = eShaderStage::Compute,
                .filename = "CloudRaymarchCS"
            })).Build();

    m_pCloudTemporalUprezPSO = ComputePipeline::Create(m_RenderDevice, "CloudTemporalUprezPSO");
    m_pCloudTemporalUprezPSO->SetComputeShader(
        Shader::Create(m_RenderDevice, "CloudTemporalUprezCS",
            {
                .stage    = eShaderStage::Compute,
                .filename = "CloudTemporalUprezCS"
            })).Build();
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
    }

    {
        context.SetRenderPipeline(m_pCloudShadowPSO.get());

        context.TransitionBarrier(m_pBaseNoiseTexture, eTextureLayout::ShaderReadOnly);
        context.TransitionBarrier(m_pErosionNoiseTexture, eTextureLayout::ShaderReadOnly);
        context.TransitionBarrier(m_pDensityTopGradientTexture, eTextureLayout::ShaderReadOnly);
        context.TransitionBarrier(m_pDensityBottomGradientTexture, eTextureLayout::ShaderReadOnly);
        context.TransitionBarrier(m_pCloudShadowMap, eTextureLayout::General);

        struct
        {
            u32   numLightRaymarchSteps;
            float planetRadiusKm;

            float time_s;
            u64   frame;
        } constant = { renderView.cloud.numLightRaymarchSteps, renderView.atmosphere.data.planetRadius_km, renderView.time, g_FrameData.frame };
        context.SetComputeConstants(sizeof(constant), &constant);
        context.SetComputeDynamicUniformBuffer("g_Cloud", renderView.cloud);
        context.SetComputeDynamicUniformBuffer("g_CloudShadow", renderView.cloud.shadow);
        context.StageDescriptor("g_CloudBaseNoise", m_pBaseNoiseTexture, g_FrameData.pLinearWrap);
        context.StageDescriptor("g_CloudErosionNoise", m_pErosionNoiseTexture, g_FrameData.pLinearWrap);
        context.StageDescriptor("g_TopGradientLUT", m_pDensityTopGradientTexture, g_FrameData.pLinearWrap);
        context.StageDescriptor("g_BottomGradientLUT", m_pDensityBottomGradientTexture, g_FrameData.pLinearWrap);
        context.StageDescriptor("g_CloudShadowMap", m_pCloudShadowMap);

        context.Dispatch2D< 8, 8 >(CLOUD_SHADOWMAP_RESOLUTION.x, CLOUD_SHADOWMAP_RESOLUTION.y);

        g_FrameData.pCloudShadowMap = m_pCloudShadowMap;
    }
    {
        context.SetRenderPipeline(m_pCloudRaymarchPSO.get());

        assert(
            g_FrameData.pDepth &&
            g_FrameData.pTransmittanceLUT &&
            g_FrameData.pAerialPerspectiveLUT &&
            g_FrameData.pAtmosphereAmbientLUT
        );
        context.TransitionBarrier(m_pBaseNoiseTexture, eTextureLayout::ShaderReadOnly);
        context.TransitionBarrier(m_pErosionNoiseTexture, eTextureLayout::ShaderReadOnly);
        context.TransitionBarrier(m_pDensityTopGradientTexture, eTextureLayout::ShaderReadOnly);
        context.TransitionBarrier(m_pDensityBottomGradientTexture, eTextureLayout::ShaderReadOnly);
        context.TransitionBarrier(g_FrameData.pDepth.lock(), eTextureLayout::ShaderReadOnly);
        context.TransitionBarrier(g_FrameData.pTransmittanceLUT.lock(), eTextureLayout::ShaderReadOnly);
        context.TransitionBarrier(g_FrameData.pAerialPerspectiveLUT.lock(), eTextureLayout::ShaderReadOnly);
        context.TransitionBarrier(g_FrameData.pAtmosphereAmbientLUT.lock(), eTextureLayout::ShaderReadOnly);
        context.TransitionBarrier(m_pCloudShadowMap, eTextureLayout::ShaderReadOnly);
        context.TransitionBarrier(m_pBlueNoiseTexture, eTextureLayout::ShaderReadOnly);
        context.TransitionBarrier(m_pCloudScatteringLUT, eTextureLayout::General);

        struct
        {
            mat4 mSunView;
            mat4 mSunViewProj;

            u32 numCloudRaymarchSteps;
            u32 numLightRaymarchSteps;
            float frontDepthBias;

            float time_s;
            u64   frame;
        } constant = { renderView.cloud.shadow.mSunView, renderView.cloud.shadow.mSunViewProj, renderView.cloud.numCloudRaymarchSteps, renderView.cloud.numLightRaymarchSteps, renderView.cloud.frontDepthBias, renderView.time, g_FrameData.frame };
        context.SetComputeConstants(sizeof(constant), &constant);
        context.SetComputeDynamicUniformBuffer("g_Camera", g_FrameData.camera);
        context.SetComputeDynamicUniformBuffer("g_Atmosphere", renderView.atmosphere.data);
        context.SetComputeDynamicUniformBuffer("g_Cloud", renderView.cloud);
        context.StageDescriptor("g_CloudBaseNoise", m_pBaseNoiseTexture, g_FrameData.pLinearWrap);
        context.StageDescriptor("g_CloudErosionNoise", m_pErosionNoiseTexture, g_FrameData.pLinearWrap);
        context.StageDescriptor("g_TopGradientLUT", m_pDensityTopGradientTexture, g_FrameData.pLinearWrap);
        context.StageDescriptor("g_BottomGradientLUT", m_pDensityBottomGradientTexture, g_FrameData.pLinearWrap);
        context.StageDescriptor("g_DepthBuffer", g_FrameData.pDepth.lock(), g_FrameData.pPointClamp);
        context.StageDescriptor("g_TransmittanceLUT", g_FrameData.pTransmittanceLUT.lock(), g_FrameData.pLinearClamp);
        context.StageDescriptor("g_AerialPerspectiveLUT", g_FrameData.pAerialPerspectiveLUT.lock(), g_FrameData.pLinearClamp);
        context.StageDescriptor("g_AtmosphereAmbientLUT", g_FrameData.pAtmosphereAmbientLUT.lock(), g_FrameData.pLinearClamp);
        context.StageDescriptor("g_CloudShadowMap", m_pCloudShadowMap, g_FrameData.pPointClamp);
        context.StageDescriptor("g_BlueNoiseArray", m_pBlueNoiseTexture, g_FrameData.pLinearClamp);
        context.StageDescriptor("g_CloudScatteringLUT", m_pCloudScatteringLUT);

        context.Dispatch2D< 8, 8 >(m_pCloudScatteringLUT->Width(), m_pCloudScatteringLUT->Height());
    }
    {
        context.SetRenderPipeline(m_pCloudTemporalUprezPSO.get());

        context.TransitionBarrier(m_pCloudScatteringLUT, eTextureLayout::ShaderReadOnly);
        context.TransitionBarrier(m_pPrevUprezzedCloudScatteringLUT, eTextureLayout::ShaderReadOnly);
        context.TransitionBarrier(g_FrameData.pDepth.lock(), eTextureLayout::ShaderReadOnly);
        context.TransitionBarrier(m_pUprezzedCloudScatteringLUT, eTextureLayout::General);

        struct
        {
            float  blendAlpha;
            float2 invLowResTexSize;
        } constant = { renderView.cloud.temporalBlendAlpha, float2(1.0 / m_pCloudScatteringLUT->Width(), 1.0 / m_pCloudScatteringLUT->Height()) };
        context.SetComputeConstants(sizeof(constant), &constant);
        context.SetComputeDynamicUniformBuffer("g_Camera", g_FrameData.camera);
        context.StageDescriptor("g_CloudScatteringLUT", m_pCloudScatteringLUT, g_FrameData.pLinearClamp);
        context.StageDescriptor("g_PrevUprezzedCloudScatteringLUT", m_pPrevUprezzedCloudScatteringLUT, g_FrameData.pLinearClamp);
        context.StageDescriptor("g_DepthBuffer", g_FrameData.pDepth.lock(), g_FrameData.pPointClamp);
        context.StageDescriptor("g_UprezzedCloudScatteringLUT", m_pUprezzedCloudScatteringLUT);

        context.Dispatch2D< 8, 8 >(m_pUprezzedCloudScatteringLUT->Width(), m_pUprezzedCloudScatteringLUT->Height());

        context.CopyTexture(m_pPrevUprezzedCloudScatteringLUT, m_pUprezzedCloudScatteringLUT);
        
    }
    g_FrameData.pCloudScatteringLUT = m_pUprezzedCloudScatteringLUT;
}

void CloudScatteringNode::Resize(u32 width, u32 height, u32 depth)
{
    UNUSED(depth);

    auto halfRes = HalfResolution(width, height);
    m_pCloudScatteringLUT->Resize(halfRes.x, halfRes.y, 1);
    m_pPrevUprezzedCloudScatteringLUT->Resize(width, height, 1);
    m_pUprezzedCloudScatteringLUT->Resize(width, height, 1);
}

} // namespace baamboo
