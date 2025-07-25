#include "RendererPch.h"
#include "VkAtmosphereModule.h"
#include "RenderDevice/VkResourceManager.h"
#include "RenderDevice/VkRenderPipeline.h"
#include "RenderDevice/VkCommandContext.h"
#include "RenderResource/VkShader.h"
#include "RenderResource/VkTexture.h"
#include "RenderResource/VkRenderTarget.h"
#include "RenderResource/VkSceneResource.h"

static constexpr VkExtent3D TRANSMITTANCE_LUT_RESOLUTION     = { 256, 64, 1 };
static constexpr VkExtent3D MULTISCATTERING_LUT_RESOLUTION   = { 32, 32, 1 };
static constexpr VkExtent3D SKYVIEW_LUT_RESOLUTION           = { 192, 104 , 1 };
static constexpr VkExtent3D AERIALPERSPECTIVE_LUT_RESOLUTION = { 32, 32 , 32 };

namespace vk
{

AtmosphereModule::AtmosphereModule(RenderDevice& device)
	: Super(device)
{
	m_pTransmittanceLUT =
		Texture::Create(
			m_RenderDevice,
			"AtmospherePass::TransmittanceLUT",
			{
				.resolution = TRANSMITTANCE_LUT_RESOLUTION,
				.format     = VK_FORMAT_B10G11R11_UFLOAT_PACK32,
				.imageUsage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
			});
	m_pMultiScatteringLUT =
		Texture::Create(
			m_RenderDevice,
			"AtmospherePass::MultiScatteringLUT",
			{
				.resolution = MULTISCATTERING_LUT_RESOLUTION,
				.format     = VK_FORMAT_B10G11R11_UFLOAT_PACK32,
				.imageUsage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
			});
	m_pSkyViewLUT =
		Texture::Create(
			m_RenderDevice,
			"AtmospherePass::SkyViewLUT",
			{
				.resolution = SKYVIEW_LUT_RESOLUTION,
				.format     = VK_FORMAT_B10G11R11_UFLOAT_PACK32,
				.imageUsage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
			});
	m_pAerialPerspectiveLUT =
		Texture::Create(
			m_RenderDevice,
			"AtmospherePass::AerialPerspectiveLUT",
			{
				.type       = eTextureType::Texture3D,
				.resolution = AERIALPERSPECTIVE_LUT_RESOLUTION,
				.format     = VK_FORMAT_R16G16B16A16_SFLOAT,
				.imageUsage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
			});
	m_pLinearClampSampler  = Sampler::CreateLinearClamp(m_RenderDevice);

	m_pTransmittancePSO = MakeBox< ComputePipeline >(m_RenderDevice, "TransmittancePSO");
	m_pTransmittancePSO->SetComputeShader(
		Shader::Create(m_RenderDevice, "TransmittanceCS", 
			{
				.filepath = SPIRV_PATH.string() + "TransmittanceLUT.comp.spv"
			})).Build();

	m_pMultiScatteringPSO = MakeBox< ComputePipeline >(m_RenderDevice, "MultiScatteringPSO");
	m_pMultiScatteringPSO->SetComputeShader(
		Shader::Create(m_RenderDevice, "MultiScatteringCS", 
			{
				.filepath = SPIRV_PATH.string() + "MultiScatteringLUT.comp.spv"
			})).Build();

	m_pSkyViewPSO = MakeBox< ComputePipeline >(m_RenderDevice, "SkyViewPSO");
	m_pSkyViewPSO->SetComputeShader(
		Shader::Create(m_RenderDevice, "SkyViewCS",
			{
				.filepath = SPIRV_PATH.string() + "SkyViewLUT.comp.spv"
			})).Build();

	m_pAerialPerspectivePSO = MakeBox< ComputePipeline >(m_RenderDevice, "AerialPerspectivePSO");
	m_pAerialPerspectivePSO->SetComputeShader(
		Shader::Create(m_RenderDevice, "AerialPerspectiveCS",
			{
				.filepath = SPIRV_PATH.string() + "AerialPerspectiveLUT.comp.spv"
			})).Build();
}

AtmosphereModule::~AtmosphereModule()
{
}

void AtmosphereModule::Apply(CommandContext& context)
{
	if (g_FrameData.atmosphere.bMark)
	{
		context.SetRenderPipeline(m_pTransmittancePSO.get());
		context.TransitionImageLayout(m_pTransmittanceLUT, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
		context.SetDynamicUniformBuffer(1, g_FrameData.atmosphere.data);
		context.PushDescriptors(2, { VK_NULL_HANDLE, m_pTransmittanceLUT->vkView(), VK_IMAGE_LAYOUT_GENERAL}, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
		context.Dispatch2D< 8, 8 >(TRANSMITTANCE_LUT_RESOLUTION.width, TRANSMITTANCE_LUT_RESOLUTION.height);

		context.SetRenderPipeline(m_pMultiScatteringPSO.get());
		context.TransitionImageLayout(m_pTransmittanceLUT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
		context.TransitionImageLayout(m_pMultiScatteringLUT, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
		context.SetPushConstants(sizeof(u32), &g_FrameData.atmosphere.msIsoSampleCount, VK_SHADER_STAGE_COMPUTE_BIT, 0);
		context.SetPushConstants(sizeof(u32), &g_FrameData.atmosphere.msNumRaySteps, VK_SHADER_STAGE_COMPUTE_BIT, sizeof(u32));
		context.SetDynamicUniformBuffer(1, g_FrameData.atmosphere.data);
		context.PushDescriptors(2, { m_pLinearClampSampler->vkSampler(), m_pTransmittanceLUT->vkView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
		context.PushDescriptors(3, { VK_NULL_HANDLE, m_pMultiScatteringLUT->vkView(), VK_IMAGE_LAYOUT_GENERAL }, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
		context.Dispatch2D< 8, 8 >(MULTISCATTERING_LUT_RESOLUTION.width, MULTISCATTERING_LUT_RESOLUTION.height);
	}
	context.TransitionImageLayout(m_pTransmittanceLUT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
	context.TransitionImageLayout(m_pMultiScatteringLUT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_IMAGE_ASPECT_COLOR_BIT);

	context.SetRenderPipeline(m_pSkyViewPSO.get());
	context.TransitionImageLayout(m_pSkyViewLUT, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
	context.SetDynamicUniformBuffer(0, g_FrameData.camera);
	context.SetDynamicUniformBuffer(1, g_FrameData.atmosphere.data);
	context.SetPushConstants(sizeof(u32), &g_FrameData.atmosphere.svMinRaySteps, VK_SHADER_STAGE_COMPUTE_BIT, 0);
	context.SetPushConstants(sizeof(u32), &g_FrameData.atmosphere.svMaxRaySteps, VK_SHADER_STAGE_COMPUTE_BIT, sizeof(u32));
	context.PushDescriptors(2, { m_pLinearClampSampler->vkSampler(), m_pTransmittanceLUT->vkView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
	context.PushDescriptors(3, { m_pLinearClampSampler->vkSampler(), m_pMultiScatteringLUT->vkView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
	context.PushDescriptors(4, { VK_NULL_HANDLE, m_pSkyViewLUT->vkView(), VK_IMAGE_LAYOUT_GENERAL }, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
	context.Dispatch2D< 8, 8 >(SKYVIEW_LUT_RESOLUTION.width, SKYVIEW_LUT_RESOLUTION.height);

	context.SetRenderPipeline(m_pAerialPerspectivePSO.get());
	context.TransitionImageLayout(m_pAerialPerspectiveLUT, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
	context.SetDynamicUniformBuffer(0, g_FrameData.camera);
	context.SetDynamicUniformBuffer(1, g_FrameData.atmosphere.data);
	context.PushDescriptors(2, { m_pLinearClampSampler->vkSampler(), m_pTransmittanceLUT->vkView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
	context.PushDescriptors(3, { m_pLinearClampSampler->vkSampler(), m_pMultiScatteringLUT->vkView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
	context.PushDescriptors(4, { VK_NULL_HANDLE, m_pAerialPerspectiveLUT->vkView(), VK_IMAGE_LAYOUT_GENERAL }, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
	context.Dispatch3D< 4, 4, 4 >(AERIALPERSPECTIVE_LUT_RESOLUTION.width, AERIALPERSPECTIVE_LUT_RESOLUTION.height, AERIALPERSPECTIVE_LUT_RESOLUTION.depth);

	g_FrameData.pSkyViewLUT           = m_pSkyViewLUT;
	g_FrameData.pAerialPerspectiveLUT = m_pAerialPerspectiveLUT;
}

} // namespace vk
