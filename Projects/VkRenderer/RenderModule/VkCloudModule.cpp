#include "RendererPch.h"
#include "VkCloudModule.h"
#include "RenderDevice/VkRenderPipeline.h"
#include "RenderDevice/VkCommandContext.h"
#include "RenderDevice/VkResourceManager.h"
#include "RenderResource/VkShader.h"
#include "RenderResource/VkTexture.h"
#include "RenderResource/VkSceneResource.h"

namespace vk
{

static inline VkExtent3D HalfResolution(RenderDevice& rd) 
{
    return { std::max(1u, rd.WindowWidth() / 2u), std::max(1u, rd.WindowHeight() / 2u), 1u };
}


//-------------------------------------------------------------------------
// Cloud Shape
//-------------------------------------------------------------------------
static constexpr VkExtent3D BASE_NOISE_TEXTURE_RESOLUTION = { 128, 128, 128 };
static constexpr VkExtent3D DETAIL_NOISE_TEXTURE_RESOLUTION = { 64, 64, 64 };
static constexpr VkExtent3D VERTICAL_PROFILE_TEXTURE_RESOLUTION = { 256, 256, 1 };
static constexpr VkExtent3D WEATHERMAP_TEXTURE_RESOLUTION = { 1024, 1024, 1 };

CloudShapeModule::CloudShapeModule(RenderDevice& device)
	: Super(device)
{
    m_pBaseNoiseTexture =
        Texture::Create(
            m_RenderDevice,
            "CloudShape::BaseNoise",
            {
                .type       = eTextureType::Texture3D,
                .resolution = BASE_NOISE_TEXTURE_RESOLUTION,
                .format     = VK_FORMAT_R16G16B16A16_SFLOAT,
                .imageUsage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
            });
    m_pDetailNoiseTexture =
        Texture::Create(
            m_RenderDevice,
            "CloudShape::DetailNoise",
            {
                .type       = eTextureType::Texture3D,
                .resolution = DETAIL_NOISE_TEXTURE_RESOLUTION,
                .format     = VK_FORMAT_R16G16B16A16_SFLOAT,
                .imageUsage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
            });
    m_pVerticalProfileTexture =
        Texture::Create(
            m_RenderDevice,
            "CloudShape::VerticalProfile",
            {
                .resolution = VERTICAL_PROFILE_TEXTURE_RESOLUTION,
                .format     = VK_FORMAT_R8_UNORM,
                .imageUsage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
            });
    m_pWeatherMapTexture =
        Texture::Create(
            m_RenderDevice,
            "CloudShape::WeatherMap",
            {
                .resolution = WEATHERMAP_TEXTURE_RESOLUTION,
                .format     = VK_FORMAT_R8G8_UNORM,
                .imageUsage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
            });

    m_pCloudShapeBasePSO = MakeBox< ComputePipeline >(m_RenderDevice, "CloudShapeBasePSO");
    m_pCloudShapeBasePSO->SetComputeShader(
        Shader::Create(m_RenderDevice, "CloudShapeBaseCS",
            {
                .filepath = SPIRV_PATH.string() + "CloudShapeBase.comp.spv"
            })).Build();

    m_pCloudShapeDetailPSO = MakeBox< ComputePipeline >(m_RenderDevice, "CloudShapeDetailPSO");
    m_pCloudShapeDetailPSO->SetComputeShader(
        Shader::Create(m_RenderDevice, "CloudShapeDetailCS",
            {
                .filepath = SPIRV_PATH.string() + "CloudShapeDetail.comp.spv"
            })).Build();

    m_pVerticalProfilePSO = MakeBox< ComputePipeline >(m_RenderDevice, "CloudVerticalProfilePSO");
    m_pVerticalProfilePSO->SetComputeShader(
        Shader::Create(m_RenderDevice, "CloudVerticalProfileCS",
            {
                .filepath = SPIRV_PATH.string() + "CloudVerticalProfile.comp.spv"
            })).Build();

    m_pWeatherMapPSO = MakeBox< ComputePipeline >(m_RenderDevice, "CloudWeatherMapPSO");
    m_pWeatherMapPSO->SetComputeShader(
        Shader::Create(m_RenderDevice, "CloudWeatherMapCS",
            {
                .filepath = SPIRV_PATH.string() + "CloudWeatherMap.comp.spv"
            })).Build();
}

CloudShapeModule::~CloudShapeModule()
{
}

void CloudShapeModule::Apply(CommandContext& context, const SceneRenderView& renderView)
{
    {
        context.SetRenderPipeline(m_pCloudShapeBasePSO.get());

        context.TransitionImageLayout(m_pBaseNoiseTexture, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_IMAGE_ASPECT_COLOR_BIT);

        struct
        {
            // Perlin
            float fPerlin;
            u32   octaves;
            float persistence;
            float lacunarityPerlin;

            // Worley
            float fWorley;
            float lacunarityWorley;

            // misc
            float time_s;
        } constant = { 4.0, 7, exp(-0.85f), 2.0f, 6.0f, 2.0f, renderView.time };
        context.SetPushConstants(sizeof(constant), &constant, VK_SHADER_STAGE_COMPUTE_BIT);
        context.PushDescriptors(0, { VK_NULL_HANDLE, m_pBaseNoiseTexture->vkView(), VK_IMAGE_LAYOUT_GENERAL }, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);

        context.Dispatch3D< 8, 8, 8 >(BASE_NOISE_TEXTURE_RESOLUTION.width, BASE_NOISE_TEXTURE_RESOLUTION.height, BASE_NOISE_TEXTURE_RESOLUTION.depth);

        g_FrameData.pCloudBaseLUT = m_pBaseNoiseTexture;
    }
    {
        context.SetRenderPipeline(m_pCloudShapeDetailPSO.get());

        context.TransitionImageLayout(m_pDetailNoiseTexture, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_IMAGE_ASPECT_COLOR_BIT);

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
        context.SetPushConstants(sizeof(constant), &constant, VK_SHADER_STAGE_COMPUTE_BIT);
        context.PushDescriptors(0, { VK_NULL_HANDLE, m_pDetailNoiseTexture->vkView(), VK_IMAGE_LAYOUT_GENERAL }, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);

        context.Dispatch3D< 8, 8, 8 >(DETAIL_NOISE_TEXTURE_RESOLUTION.width, DETAIL_NOISE_TEXTURE_RESOLUTION.height, DETAIL_NOISE_TEXTURE_RESOLUTION.depth);

        g_FrameData.pCloudDetailLUT = m_pDetailNoiseTexture;
    }
    {
        context.SetRenderPipeline(m_pVerticalProfilePSO.get());

        context.TransitionImageLayout(m_pVerticalProfileTexture, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_IMAGE_ASPECT_COLOR_BIT);

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
        //context.SetPushConstants(sizeof(constant), &constant, VK_SHADER_STAGE_COMPUTE_BIT);
        context.PushDescriptors(
            0, 
            { 
                VK_NULL_HANDLE, 
                m_pVerticalProfileTexture->vkView(),
                VK_IMAGE_LAYOUT_GENERAL 
            }, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);

        context.Dispatch2D< 8, 8 >(VERTICAL_PROFILE_TEXTURE_RESOLUTION.width, VERTICAL_PROFILE_TEXTURE_RESOLUTION.height);

        g_FrameData.pVerticalProfileLUT = m_pVerticalProfileTexture;
    }
    {
        /*context.SetRenderPipeline(m_pWeatherMapPSO.get());

        context.TransitionImageLayout(m_pWeatherMapTexture, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_IMAGE_ASPECT_COLOR_BIT);

        context.PushDescriptors(
            0, 
            { 
                VK_NULL_HANDLE, 
                m_pWeatherMapTexture->vkView(), 
                VK_IMAGE_LAYOUT_GENERAL 
            }, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);

        context.Dispatch2D< 8, 8 >(WEATHERMAP_TEXTURE_RESOLUTION.width, WEATHERMAP_TEXTURE_RESOLUTION.height);

        g_FrameData.pWeatherMapLUT = m_pWeatherMapTexture;*/
    }
}


//-------------------------------------------------------------------------
// Cloud Scattering
//-------------------------------------------------------------------------
CloudScatteringModule::CloudScatteringModule(RenderDevice& device)
    : Super(device)
{
    m_pCloudScatteringLUT =
        Texture::Create(
            m_RenderDevice,
            "CloudScattering::ScatteringLUT",
            {
                //.resolution = HalfResolution(m_RenderDevice),
                .resolution = { m_RenderDevice.WindowWidth(), m_RenderDevice.WindowHeight(), 1 },
                .format     = VK_FORMAT_R16G16B16A16_SFLOAT,
                .imageUsage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
            });

    m_pCloudScatteringPSO = MakeBox< ComputePipeline >(m_RenderDevice, "CloudScatteringPSO");
    m_pCloudScatteringPSO->SetComputeShader(
        Shader::Create(m_RenderDevice, "CloudScatteringCS",
            {
                .filepath = SPIRV_PATH.string() + "CloudScatteringLUT.comp.spv"
            })).Build();
}

CloudScatteringModule::~CloudScatteringModule()
{
}

void CloudScatteringModule::Apply(CommandContext& context, const SceneRenderView& renderView)
{
    if (!m_pWeatherMap)
    {
        auto& rm = m_RenderDevice.GetResourceManager();

        m_pWeatherMap = rm.LoadTexture(renderView.cloud.weatherMap);
        assert(m_pWeatherMap);
    }
    if (!m_pCurlNoiseTexture)
    {
        auto& rm = m_RenderDevice.GetResourceManager();

        m_pCurlNoiseTexture = rm.LoadTexture(renderView.cloud.curlNoiseTex);
        assert(m_pCurlNoiseTexture);
    }
    if (!m_pBlueNoiseTexture)
    {
        auto& rm = m_RenderDevice.GetResourceManager();

        m_pBlueNoiseTexture = rm.LoadTexture(renderView.cloud.blueNoiseTex);
        assert(m_pBlueNoiseTexture);
    }

    // Dispatch compute shader
    context.SetRenderPipeline(m_pCloudScatteringPSO.get());

    context.TransitionImageLayout(g_FrameData.pCloudBaseLUT.lock(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    context.TransitionImageLayout(g_FrameData.pCloudDetailLUT.lock(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    context.TransitionImageLayout(g_FrameData.pVerticalProfileLUT.lock(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    context.TransitionImageLayout(m_pWeatherMap, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    //context.TransitionImageLayout(g_FrameData.pWeatherMapLUT.lock(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    context.TransitionImageLayout(m_pCurlNoiseTexture, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    context.TransitionImageLayout(m_pBlueNoiseTexture, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    context.TransitionImageLayout(g_FrameData.pDepth.lock(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);
    context.TransitionImageLayout(g_FrameData.pTransmittanceLUT.lock(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);
    context.TransitionImageLayout(g_FrameData.pMultiScatteringLUT.lock(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);
    context.TransitionImageLayout(g_FrameData.pDepth.lock(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);
    context.TransitionImageLayout(m_pCloudScatteringLUT, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_IMAGE_ASPECT_COLOR_BIT);

    struct
    {
        float time_s;
        u64   frame;
    } constant = { renderView.time, g_FrameData.frameCounter };
    context.SetPushConstants(sizeof(constant), &constant, VK_SHADER_STAGE_COMPUTE_BIT);
    context.SetDynamicUniformBuffer(0, g_FrameData.camera);
    context.SetDynamicUniformBuffer(1, renderView.atmosphere.data);
    context.SetDynamicUniformBuffer(2, renderView.cloud);
    context.PushDescriptors(
        3,
        {
            g_FrameData.pLinearWrap->vkSampler(),
            g_FrameData.pCloudBaseLUT->vkView(),
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        }, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    context.PushDescriptors(
        4,
        {
            g_FrameData.pLinearWrap->vkSampler(),
            g_FrameData.pCloudDetailLUT->vkView(),
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        }, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    context.PushDescriptors(
        5,
        {
            g_FrameData.pLinearClamp->vkSampler(),
            g_FrameData.pVerticalProfileLUT->vkView(),
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        }, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    context.PushDescriptors(
        6,
        {
            g_FrameData.pLinearWrap->vkSampler(),
            m_pWeatherMap->vkView(),
            //g_FrameData.pWeatherMapLUT->vkView(),
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        }, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    context.PushDescriptors(
        7,
        {
            g_FrameData.pLinearWrap->vkSampler(),
            m_pCurlNoiseTexture->vkView(),
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        }, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    context.PushDescriptors(
        8,
        {
            g_FrameData.pLinearClamp->vkSampler(),
            m_pBlueNoiseTexture->vkView(),
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        }, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    context.PushDescriptors(
        9,
        {
            g_FrameData.pPointClamp->vkSampler(),
            g_FrameData.pDepth->vkView(),
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        }, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    context.PushDescriptors(
        10,
        {
            g_FrameData.pLinearClamp->vkSampler(),
            g_FrameData.pTransmittanceLUT->vkView(),
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        }, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    context.PushDescriptors(
        11,
        {
            g_FrameData.pLinearClamp->vkSampler(),
            g_FrameData.pMultiScatteringLUT->vkView(),
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        }, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    context.PushDescriptors(
        12,
        {
            VK_NULL_HANDLE,
            m_pCloudScatteringLUT->vkView(),
            VK_IMAGE_LAYOUT_GENERAL
        }, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);

    // Dispatch with proper group size
    context.Dispatch2D< 8, 8 >(m_pCloudScatteringLUT->Desc().extent.width, m_pCloudScatteringLUT->Desc().extent.height);

    g_FrameData.pCloudScatteringLUT = m_pCloudScatteringLUT;
}

} // namespace vk