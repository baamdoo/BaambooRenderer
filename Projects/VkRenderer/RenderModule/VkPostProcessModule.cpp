#include "RendererPch.h"
#include "VkPostProcessModule.h"
#include "RenderDevice/VkRenderPipeline.h"
#include "RenderDevice/VkCommandContext.h"
#include "RenderDevice/VkResourceManager.h"
#include "RenderResource/VkShader.h"
#include "RenderResource/VkTexture.h"
#include "RenderResource/VkRenderTarget.h"
#include "RenderResource/VkSceneResource.h"
#include "ComponentTypes.h"
#include "Utils/Math.hpp"

namespace vk
{

PostProcessModule::PostProcessModule(RenderDevice& device)
	: Super(device)
{
	{
		m_TAA.applyCounter = 0;

		m_TAA.pHistoryTexture =
			Texture::Create(
				m_RenderDevice,
				"PostProcessPass::TemporalAntiAliasingHistory",
				{
					.resolution = { m_RenderDevice.WindowWidth(), m_RenderDevice.WindowHeight(), 1 },
					.format     = VK_FORMAT_R16G16B16A16_SFLOAT,
					.imageUsage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT
				});
		m_TAA.pAntiAliasedTexture =
			Texture::Create(
				m_RenderDevice,
				"PostProcessPass::AntiAliasing",
				{
					.resolution = { m_RenderDevice.WindowWidth(), m_RenderDevice.WindowHeight(), 1 },
					.format     = VK_FORMAT_R16G16B16A16_SFLOAT,
					.imageUsage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
				});

		auto pCS = Shader::Create(
			m_RenderDevice,
			"TemporalAntiAliasingCS",
			{ .filepath = SPIRV_PATH.string() + "TemporalAA.comp.spv" }
		);
		m_TAA.pTemporalAntiAliasingPSO = MakeBox< ComputePipeline >(m_RenderDevice, "TemporalAntiAliasingPSO");
		m_TAA.pTemporalAntiAliasingPSO->SetComputeShader(pCS).Build();

		auto pSharpenCS = Shader::Create(
			m_RenderDevice,
			"SharpenCS",
			{ .filepath = SPIRV_PATH.string() + "Sharpen.comp.spv" }
		);
		m_TAA.pSharpenPSO = MakeBox< ComputePipeline >(m_RenderDevice, "SharpenPipelinePSO");
		m_TAA.pSharpenPSO->SetComputeShader(pSharpenCS).Build();
	}
	{
		m_ToneMapping.pResolvedTexture =
			Texture::Create(
				m_RenderDevice,
				"PostProcessPass::Out",
				{
					.resolution = { m_RenderDevice.WindowWidth(), m_RenderDevice.WindowHeight(), 1 },
					.format     = VK_FORMAT_R8G8B8A8_UNORM,
					.imageUsage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
				});

		auto pToneMappingCS = Shader::Create(
			m_RenderDevice,
			"ToneMappingCS",
			{ .filepath = SPIRV_PATH.string() + "ToneMapping.comp.spv" }
		);
		m_ToneMapping.pToneMappingPSO = MakeBox< ComputePipeline >(m_RenderDevice, "ToneMappingPSO");
		m_ToneMapping.pToneMappingPSO->SetComputeShader(pToneMappingCS).Build();
	}
}

PostProcessModule::~PostProcessModule()
{

}

void PostProcessModule::Apply(CommandContext& context, const SceneRenderView& renderView)
{
	if (renderView.postProcess.effectBits & (1 << ePostProcess::HeightFog))
		ApplyHeightFog(context, renderView);
	if (renderView.postProcess.effectBits & (1 << ePostProcess::Bloom))
		ApplyBloom(context, renderView);
	if (renderView.postProcess.effectBits & (1 << ePostProcess::AntiAliasing))
		ApplyAntiAliasing(context, renderView);

	// always apply tone-mapping
	ApplyToneMapping(context, renderView);
}

void PostProcessModule::Resize(u32 width, u32 height, u32 depth)
{
	m_TAA.applyCounter = 0;
	if (m_TAA.pAntiAliasedTexture)
		m_TAA.pAntiAliasedTexture->Resize(width, height, depth);
	if (m_TAA.pHistoryTexture)
		m_TAA.pHistoryTexture->Resize(width, height, depth);

}

void PostProcessModule::ApplyHeightFog(CommandContext& context, const SceneRenderView& renderView)
{
	// TODO
	UNUSED(context);
	UNUSED(renderView);
}

void PostProcessModule::ApplyBloom(CommandContext& context, const SceneRenderView& renderView)
{
	// TODO
	UNUSED(context);
	UNUSED(renderView);
}

void PostProcessModule::ApplyAntiAliasing(CommandContext& context, const SceneRenderView& renderView)
{
	{
		auto& rm = m_RenderDevice.GetResourceManager();
		const bool bFirstApply = m_TAA.applyCounter == 0;

		context.SetRenderPipeline(m_TAA.pTemporalAntiAliasingPSO.get());

		context.TransitionImageLayout(g_FrameData.pColor.lock(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
		context.TransitionImageLayout(g_FrameData.pGBuffer3.lock(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
		if (!bFirstApply)
			context.TransitionImageLayout(m_TAA.pHistoryTexture, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
		context.TransitionImageLayout(m_TAA.pAntiAliasedTexture, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_IMAGE_ASPECT_COLOR_BIT);

		struct
		{
			float  blendFactor;
			u32    isFirstFrame;
		} constant = { bFirstApply ? 1.0f : renderView.postProcess.aa.blendFactor, bFirstApply };
		context.SetPushConstants(sizeof(constant), &constant, VK_SHADER_STAGE_COMPUTE_BIT);
		context.PushDescriptors(
			0,
			{
				g_FrameData.pLinearClamp->vkSampler(),
				g_FrameData.pColor->vkView(),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			}, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
		context.PushDescriptors(
			1,
			{
				g_FrameData.pLinearClamp->vkSampler(),
				g_FrameData.pGBuffer3->vkView(),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			}, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
		context.PushDescriptors(
			2,
			{
				g_FrameData.pLinearClamp->vkSampler(),
				bFirstApply ? rm.GetFlatBlackTexture()->vkView() : m_TAA.pHistoryTexture->vkView(),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			}, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
		context.PushDescriptors(
			3,
			{
				VK_NULL_HANDLE,
				m_TAA.pAntiAliasedTexture->vkView(),
				VK_IMAGE_LAYOUT_GENERAL
			}, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
		context.Dispatch2D< 16, 16 >(m_TAA.pAntiAliasedTexture->Desc().extent.width, m_TAA.pAntiAliasedTexture->Desc().extent.height);

		context.CopyTexture(m_TAA.pHistoryTexture, m_TAA.pAntiAliasedTexture);
	}
	{
		context.SetRenderPipeline(m_TAA.pSharpenPSO.get());

		context.TransitionImageLayout(m_TAA.pAntiAliasedTexture, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
		context.TransitionImageLayout(g_FrameData.pColor.lock(), VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_IMAGE_ASPECT_COLOR_BIT);

		struct
		{
			float sharpness;
		} constant = { renderView.postProcess.aa.sharpness };
		context.SetPushConstants(sizeof(constant), &constant, VK_SHADER_STAGE_COMPUTE_BIT);
		context.PushDescriptors(
			0,
			{
				g_FrameData.pLinearClamp->vkSampler(),
				m_TAA.pAntiAliasedTexture->vkView(),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			}, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
		context.PushDescriptors(
			1,
			{
				VK_NULL_HANDLE,
				g_FrameData.pColor->vkView(),
				VK_IMAGE_LAYOUT_GENERAL
			}, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
		context.Dispatch2D< 16, 16 >(g_FrameData.pColor->Desc().extent.width, g_FrameData.pColor->Desc().extent.height);
	}

	m_TAA.applyCounter++;
}

void PostProcessModule::ApplyToneMapping(CommandContext& context, const SceneRenderView& renderView)
{
	context.SetRenderPipeline(m_ToneMapping.pToneMappingPSO.get());

	context.TransitionImageLayout(g_FrameData.pColor.lock(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
	context.TransitionImageLayout(m_ToneMapping.pResolvedTexture, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_IMAGE_ASPECT_COLOR_BIT);

	// Tone mapping push constants
	struct
	{
		u32   tonemapOperator; // 0: Reinhard, 1: ACES, 2: Uncharted2
		float gamma;
	} constant = { (u32)renderView.postProcess.tonemap.op, renderView.postProcess.tonemap.gamma };
	context.SetPushConstants(sizeof(constant), &constant, VK_SHADER_STAGE_COMPUTE_BIT);
	context.PushDescriptors(
		0,
		{
			g_FrameData.pLinearClamp->vkSampler(),
			g_FrameData.pColor->vkView(),
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
		}, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
	context.PushDescriptors(
		1,
		{
			VK_NULL_HANDLE,
			m_ToneMapping.pResolvedTexture->vkView(),
			VK_IMAGE_LAYOUT_GENERAL
		}, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
	context.Dispatch2D< 16, 16 >(g_FrameData.pColor->Desc().extent.width, g_FrameData.pColor->Desc().extent.height);

	g_FrameData.pColor = m_ToneMapping.pResolvedTexture;
}

} // namespace vk
