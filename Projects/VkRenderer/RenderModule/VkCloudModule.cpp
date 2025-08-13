#include "RendererPch.h"
#include "VkCloudModule.h"
#include "RenderDevice/VkRenderPipeline.h"
#include "RenderDevice/VkCommandContext.h"
#include "RenderResource/VkShader.h"
#include "RenderResource/VkTexture.h"
#include "RenderResource/VkSceneResource.h"

namespace vk
{

static constexpr VkExtent3D BASE_NOISE_TEXTURE_RESOLUTION = { 128, 128, 128 };
static constexpr VkExtent3D DETAIL_NOISE_TEXTURE_RESOLUTION = { 64, 64, 64 };

CloudModule::CloudModule(RenderDevice& device)
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
    m_pCurlNoiseTexture =
        Texture::Create(
            m_RenderDevice,
            "CloudShape::CurlNoise",
            {
                .type       = eTextureType::Texture3D,
                .resolution = DETAIL_NOISE_TEXTURE_RESOLUTION,
                .format     = VK_FORMAT_R16G16B16A16_SFLOAT,
                .imageUsage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
            });

    m_pCloudShapeBasePSO = MakeBox< ComputePipeline >(m_RenderDevice, "CloudShapeBasePSO");
    m_pCloudShapeBasePSO->SetComputeShader(
        Shader::Create(m_RenderDevice, "CloudShapeBaseCS",
            {
                .filepath = SPIRV_PATH.string() + "CloudShapeBase.comp.spv"
            })).Build();
}

CloudModule::~CloudModule()
{
}

void CloudModule::Apply(CommandContext& context, const SceneRenderView& renderView)
{
    context.SetRenderPipeline(m_pCloudShapeBasePSO.get());
    context.TransitionImageLayout(m_pBaseNoiseTexture, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    context.TransitionImageLayout(m_pDetailNoiseTexture, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    context.TransitionImageLayout(m_pCurlNoiseTexture, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    context.SetDynamicUniformBuffer(0, renderView.cloud);
    context.PushDescriptors(1, { VK_NULL_HANDLE, m_pBaseNoiseTexture->vkView(), VK_IMAGE_LAYOUT_GENERAL }, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
    // context.PushDescriptors(2, { VK_NULL_HANDLE, m_pDetailNoiseTexture->vkView(), VK_IMAGE_LAYOUT_GENERAL }, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
    // context.PushDescriptors(3, { VK_NULL_HANDLE, m_pCurlNoiseTexture->vkView(), VK_IMAGE_LAYOUT_GENERAL }, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);

    context.Dispatch3D< 8, 8, 8 >(BASE_NOISE_TEXTURE_RESOLUTION.width, BASE_NOISE_TEXTURE_RESOLUTION.height, BASE_NOISE_TEXTURE_RESOLUTION.depth);

    g_FrameData.pCloudLUT_BaseNoise   = m_pBaseNoiseTexture;
    // g_FrameData.pCloudLUT_DetailNoise = m_pDetailNoiseTexture;
    // g_FrameData.pCloudLUT_CurlNoise   = m_pCurlNoiseTexture;
}

} // namespace vk