#include "BaambooPch.h"
#include "CloudNode.h"
#include "RenderCommon/RenderDevice.h"
#include "RenderCommon/CommandContext.h"
#include "BaambooScene/Scene.h"

namespace baamboo
{

static inline uint3 HalfResolution(render::RenderDevice& rd)
{
    return { std::max(1u, rd.WindowWidth() / 2u), std::max(1u, rd.WindowHeight() / 2u), 1u };
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

    m_pBaseNoiseTexture =
        Texture::Create(
            m_RenderDevice,
            "CloudShape::BaseNoise",
            {
                .type       = eTextureType::Texture3D,
                .resolution = BASE_NOISE_TEXTURE_RESOLUTION,
                .format     = eFormat::RGBA16_FLOAT,
                .imageUsage = eTextureUsage_Sample | eTextureUsage_Storage | eTextureUsage_TransferSource
            });
    m_pDetailNoiseTexture =
        Texture::Create(
            m_RenderDevice,
            "CloudShape::DetailNoise",
            {
                .type       = eTextureType::Texture3D,
                .resolution = DETAIL_NOISE_TEXTURE_RESOLUTION,
                .format     = eFormat::RGBA16_FLOAT,
                .imageUsage = eTextureUsage_Sample | eTextureUsage_Storage | eTextureUsage_TransferSource
            });
    m_pVerticalProfileTexture =
        Texture::Create(
            m_RenderDevice,
            "CloudShape::VerticalProfile",
            {
                .resolution = VERTICAL_PROFILE_TEXTURE_RESOLUTION,
                .format     = eFormat::R8_UNORM,
                .imageUsage = eTextureUsage_Sample | eTextureUsage_Storage | eTextureUsage_TransferSource
            });
    m_pWeatherMapTexture =
        Texture::Create(
            m_RenderDevice,
            "CloudShape::WeatherMap",
            {
                .resolution = WEATHERMAP_TEXTURE_RESOLUTION,
                .format     = eFormat::RG8_UNORM,
                .imageUsage = eTextureUsage_Sample | eTextureUsage_Storage | eTextureUsage_TransferSource
            });

    m_pCloudShapeBasePSO = ComputePipeline::Create(m_RenderDevice, "CloudShapeBasePSO");
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
            })).Build();

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
        context.SetRenderPipeline(m_pCloudShapeBasePSO.get());

        context.TransitionBarrier(m_pBaseNoiseTexture, eTextureLayout::General);

        context.StageDescriptor("g_BaseNoise", m_pBaseNoiseTexture);

        context.Dispatch3D< 8, 8, 8 >(BASE_NOISE_TEXTURE_RESOLUTION.x, BASE_NOISE_TEXTURE_RESOLUTION.y, BASE_NOISE_TEXTURE_RESOLUTION.z);

        g_FrameData.pCloudBaseLUT = m_pBaseNoiseTexture;
    }
    {
        context.SetRenderPipeline(m_pCloudShapeDetailPSO.get());

        context.TransitionBarrier(m_pDetailNoiseTexture, eTextureLayout::General);

        struct
        {
            // r-channel
            float rWeight;
            float rFrequency;
            u32   rOctaves;
            float rPersistence;
            float rLacunarity;

            // g-channel
            float gWeight;
            float gFrequency;
            u32   gOctaves;
            float gPersistence;
            float gLacunarity;

            // b-channel
            float bWeight;
            float bFrequency;
            u32   bOctaves;
            float bPersistence;
            float bLacunarity;
        } constant = { 1.0f, 3.0, 16, 0.5f, 2.0f, 0.5f, 8.0f, 4, 0.5f, 2.0f, 0.25f, 16.0f, 4, 0.5f, 2.0f };
        context.SetComputeConstants(sizeof(constant), &constant);
        context.StageDescriptor("g_DetailNoise", m_pDetailNoiseTexture);

        context.Dispatch3D< 8, 8, 8 >(DETAIL_NOISE_TEXTURE_RESOLUTION.x, DETAIL_NOISE_TEXTURE_RESOLUTION.y, DETAIL_NOISE_TEXTURE_RESOLUTION.z);

        g_FrameData.pCloudDetailLUT = m_pDetailNoiseTexture;
    }
    {
        context.SetRenderPipeline(m_pVerticalProfilePSO.get());

        context.TransitionBarrier(m_pVerticalProfileTexture, eTextureLayout::General);

        //struct
        //{
        //    // cloud type
        //    float fType;
        //    i32   oType;
        //    float pType;
        //    float lType;

        //    // coverage
        //    float fCoverage;
        //    i32   oCoverage;
        //    float pCoverage;
        //    float lCoverage;
        //} constant = { 100.0f, 10, 0.75f, 3.0f, 76.0f, 4, 0.5f, 3.0f };
        //context.SetComputeConstants(sizeof(constant), &constant);
        context.StageDescriptor("g_VerticalProfileLUT", m_pVerticalProfileTexture);

        context.Dispatch2D< 8, 8 >(VERTICAL_PROFILE_TEXTURE_RESOLUTION.x, VERTICAL_PROFILE_TEXTURE_RESOLUTION.y);

        g_FrameData.pVerticalProfileLUT = m_pVerticalProfileTexture;
    }
    {
        /*context.SetRenderPipeline(m_pWeatherMapPSO.get());

        context.TransitionBarrier(m_pWeatherMapTexture, eTextureLayout::General);

        context.StageDescriptor("g_WeatherMap", m_pWeatherMapTexture);

        context.Dispatch2D< 8, 8 >(WEATHERMAP_TEXTURE_RESOLUTION.x, WEATHERMAP_TEXTURE_RESOLUTION.y);*/

        g_FrameData.pWeatherMapLUT = m_pWeatherMapTexture;
    }
}


//-------------------------------------------------------------------------
// Cloud Scattering
//-------------------------------------------------------------------------
CloudScatteringNode::CloudScatteringNode(render::RenderDevice& device)
    : Super(device, "CloudScatteringPass")
{
    using namespace render;

    m_pCloudScatteringLUT =
        Texture::Create(
            m_RenderDevice,
            "CloudScattering::ScatteringLUT",
            {
                //.resolution = HalfResolution(m_RenderDevice),
                .resolution = { m_RenderDevice.WindowWidth(), m_RenderDevice.WindowHeight(), 1 },
                .format     = eFormat::RGBA16_FLOAT,
                .imageUsage = eTextureUsage_Sample | eTextureUsage_Storage | eTextureUsage_TransferSource
            });

    m_pCloudRaymarchPSO = ComputePipeline::Create(m_RenderDevice, "CloudRaymarchPSO");
    m_pCloudRaymarchPSO->SetComputeShader(
        Shader::Create(m_RenderDevice, "CloudRaymarchCS",
            {
                .stage    = eShaderStage::Compute,
                .filename = "CloudRaymarch"
            })).Build();
}

CloudScatteringNode::~CloudScatteringNode()
{
}

void CloudScatteringNode::Apply(render::CommandContext& context, const SceneRenderView& renderView)
{
    using namespace render;

    // Dispatch compute shader
    context.SetRenderPipeline(m_pCloudRaymarchPSO.get());

    assert(
        g_FrameData.pCloudBaseLUT &&
        g_FrameData.pCloudDetailLUT &&
        g_FrameData.pVerticalProfileLUT && 
        g_FrameData.pDepth && 
        g_FrameData.pTransmittanceLUT && 
        g_FrameData.pMultiScatteringLUT
    );
    context.TransitionBarrier(g_FrameData.pCloudBaseLUT.lock(), eTextureLayout::ShaderReadOnly);
    context.TransitionBarrier(g_FrameData.pCloudDetailLUT.lock(), eTextureLayout::ShaderReadOnly);
    context.TransitionBarrier(g_FrameData.pVerticalProfileLUT.lock(), eTextureLayout::ShaderReadOnly);
    context.TransitionBarrier(g_FrameData.pWeatherMapLUT.lock(), eTextureLayout::ShaderReadOnly);
    context.TransitionBarrier(g_FrameData.pDepth.lock(), eTextureLayout::ShaderReadOnly);
    context.TransitionBarrier(g_FrameData.pTransmittanceLUT.lock(), eTextureLayout::ShaderReadOnly);
    context.TransitionBarrier(g_FrameData.pMultiScatteringLUT.lock(), eTextureLayout::ShaderReadOnly);
    context.TransitionBarrier(m_pCloudScatteringLUT, eTextureLayout::General);

    struct
    {
        float time_s;
        u64   frame;
    } constant = { renderView.time, g_FrameData.frame };
    context.SetComputeConstants(sizeof(constant), &constant);
    context.SetComputeDynamicUniformBuffer("g_Camera", g_FrameData.camera);
    context.SetComputeDynamicUniformBuffer("g_Atmosphere", renderView.atmosphere.data);
    context.SetComputeDynamicUniformBuffer("g_Cloud", renderView.cloud);
    context.StageDescriptor("g_CloudBaseNoise", g_FrameData.pCloudBaseLUT.lock(), g_FrameData.pLinearWrap);
    context.StageDescriptor("g_CloudDetailNoise", g_FrameData.pCloudDetailLUT.lock(), g_FrameData.pLinearWrap);
    context.StageDescriptor("g_VerticalProfileLUT", g_FrameData.pVerticalProfileLUT.lock(), g_FrameData.pLinearClamp);
    context.StageDescriptor("g_DepthBuffer", g_FrameData.pDepth.lock(), g_FrameData.pPointClamp);
    context.StageDescriptor("g_TransmittanceLUT", g_FrameData.pTransmittanceLUT.lock(), g_FrameData.pLinearClamp);
    context.StageDescriptor("g_MultiScatteringLUT", g_FrameData.pMultiScatteringLUT.lock(), g_FrameData.pLinearClamp);
    context.StageDescriptor("g_CloudScatteringLUT", m_pCloudScatteringLUT);

    // Dispatch with proper group size
    context.Dispatch2D< 8, 8 >(m_pCloudScatteringLUT->Width(), m_pCloudScatteringLUT->Height());

    g_FrameData.pCloudScatteringLUT = m_pCloudScatteringLUT;
}

} // namespace baamboo
