#include "RendererPch.h"
#include "VkLightingModule.h"
#include "RenderDevice/VkResourceManager.h"
#include "RenderDevice/VkRenderPipeline.h"
#include "RenderDevice/VkCommandContext.h"
#include "RenderResource/VkShader.h"
#include "RenderResource/VkTexture.h"
#include "RenderResource/VkSceneResource.h"
#include "VkGBufferModule.h"

namespace vk
{

LightingModule::LightingModule(RenderDevice& device)
	: Super(device)
{
	m_pOutTexture =
		Texture::Create(
			m_RenderDevice,
			"LightingPass::Out",
			{
				.resolution = { m_RenderDevice.WindowWidth(), m_RenderDevice.WindowHeight(), 1 },
				.format     = VK_FORMAT_R16G16B16A16_SFLOAT,
				.imageUsage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
			});
	m_pLinearRepeatSampler = Sampler::CreateLinearRepeat(m_RenderDevice, "LightingLinearRepeatSampler");

	auto pCS = Shader::Create(m_RenderDevice, "DeferredPBRLightingCS", { .filepath = SPIRV_PATH.string() + "DeferredPBRLighting.comp.spv" });
	m_pLightingPSO = new ComputePipeline(m_RenderDevice, "LightingPSO");
	m_pLightingPSO->SetComputeShader(pCS).Build();
}

LightingModule::~LightingModule()
{
	RELEASE(m_pLightingPSO);
}

void LightingModule::Apply(CommandContext& context, const SceneRenderView& renderView)
{
	context.SetRenderPipeline(m_pLightingPSO);

	VkImageSubresourceRange subresourceRange = {};
	subresourceRange.aspectMask   = VK_IMAGE_ASPECT_COLOR_BIT;
	subresourceRange.baseMipLevel = 0;
	subresourceRange.levelCount   = m_pOutTexture->Desc().mipLevels;
	subresourceRange.layerCount   = m_pOutTexture->Desc().arrayLayers;

	context.TransitionImageLayout(g_FrameData.pGBuffer0.lock(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, subresourceRange);
	context.TransitionImageLayout(g_FrameData.pGBuffer1.lock(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, subresourceRange);
	context.TransitionImageLayout(g_FrameData.pGBuffer2.lock(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, subresourceRange);
	context.TransitionImageLayout(g_FrameData.pGBuffer3.lock(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, subresourceRange);
	context.TransitionImageLayout(g_FrameData.pDepth.lock(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, subresourceRange);
	context.TransitionImageLayout(g_FrameData.pSkyViewLUT.lock(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
	context.TransitionImageLayout(g_FrameData.pAerialPerspectiveLUT.lock(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
	context.TransitionImageLayout(g_FrameData.pCloudScatteringLUT.lock(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
	context.TransitionImageLayout(m_pOutTexture, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, subresourceRange);

	context.BindSceneDescriptors(*g_FrameData.pSceneResource);
	context.SetPushConstants(sizeof(float), &renderView.atmosphere.data.planetRadius_km, VK_SHADER_STAGE_COMPUTE_BIT);
	context.SetDynamicUniformBuffer(0, g_FrameData.camera);
	context.PushDescriptors(
		1, 
		{ 
			g_FrameData.pLinearClamp->vkSampler(),
			g_FrameData.pGBuffer0->vkView(), 
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
		}, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
	context.PushDescriptors(
		2,
		{
			g_FrameData.pLinearClamp->vkSampler(),
			g_FrameData.pGBuffer1->vkView(),
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
		}, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
	context.PushDescriptors(
		3,
		{
			g_FrameData.pLinearClamp->vkSampler(),
			g_FrameData.pGBuffer2->vkView(),
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
		}, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
	context.PushDescriptors(
		4,
		{
			g_FrameData.pLinearClamp->vkSampler(),
			g_FrameData.pGBuffer3->vkView(),
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
		}, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
	context.PushDescriptors(
		5,
		{
			g_FrameData.pLinearClamp->vkSampler(),
			g_FrameData.pDepth->vkView(),
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
		}, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
	context.PushDescriptors(
		6,
		{
			g_FrameData.pLinearClamp->vkSampler(),
			g_FrameData.pSkyViewLUT->vkView(),
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
		}, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
	context.PushDescriptors(
		7,
		{
			g_FrameData.pLinearClamp->vkSampler(),
			g_FrameData.pAerialPerspectiveLUT->vkView(),
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
		}, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
	if (auto pCloudLUT = g_FrameData.pCloudScatteringLUT.lock())
	{
		context.PushDescriptors(
			8,
			{
				g_FrameData.pLinearClamp->vkSampler(),
				pCloudLUT->vkView(),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			}, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
	}
	context.PushDescriptors(
		9,
		{
			VK_NULL_HANDLE,
			m_pOutTexture->vkView(),
			VK_IMAGE_LAYOUT_GENERAL
		}, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
	context.Dispatch2D< 16, 16 >(m_pOutTexture->Desc().extent.width, m_pOutTexture->Desc().extent.height);

	g_FrameData.pColor = m_pOutTexture;
}

void LightingModule::Resize(u32 width, u32 height, u32 depth)
{
	if (m_pOutTexture)
		m_pOutTexture->Resize(width, height, depth);
}

} // namespace vk