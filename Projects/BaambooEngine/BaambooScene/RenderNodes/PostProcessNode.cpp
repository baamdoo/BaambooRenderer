#include "BaambooPch.h"
#include "PostProcessNode.h"
#include "RenderCommon/RenderDevice.h"
#include "RenderCommon/CommandContext.h"
#include "BaambooScene/Scene.h"

namespace baamboo
{

PostProcessNode::PostProcessNode(render::RenderDevice& rd)
	: Super(rd, "PostProcessPass")
{
	using namespace render;
	{
		m_TAA.ApplyCounter = 0;

		m_TAA.pHistoryTexture =
			Texture::Create(
				m_RenderDevice,
				"PostProcessPass::TemporalAntiAliasingHistory",
				{
					.resolution = { m_RenderDevice.WindowWidth(), m_RenderDevice.WindowHeight(), 1 },
					.format     = eFormat::RGBA16_FLOAT,
					.imageUsage = eTextureUsage_Sample | eTextureUsage_TransferDest
				});
		m_TAA.pAntiAliasedTexture =
			Texture::Create(
				m_RenderDevice,
				"PostProcessPass::AntiAliasing",
				{
					.resolution = { m_RenderDevice.WindowWidth(), m_RenderDevice.WindowHeight(), 1 },
					.format     = eFormat::RGBA16_FLOAT,
					.imageUsage = eTextureUsage_Sample | eTextureUsage_Storage | eTextureUsage_TransferSource
				});

		m_TAA.pTemporalAntiAliasingPSO = ComputePipeline::Create(m_RenderDevice, "TemporalAntiAliasingPSO");
		m_TAA.pTemporalAntiAliasingPSO->SetComputeShader(
			Shader::Create(m_RenderDevice, "TemporalAntiAliasingCS",
			{
				.stage    = eShaderStage::Compute,
				.filename = "TemporalAntiAliasingCS"
			}
		)).Build();

		m_TAA.pSharpenPSO = ComputePipeline::Create(m_RenderDevice, "SharpenPipelinePSO");
		m_TAA.pSharpenPSO->SetComputeShader(
			Shader::Create(m_RenderDevice, "SharpenCS",
			{ 
				.stage    = eShaderStage::Compute,
				.filename = "SharpenCS" 
			}
		)).Build();
	}
	{
		m_ToneMapping.pResolvedTexture =
			Texture::Create(
				m_RenderDevice,
				"PostProcessPass::Out",
				{
					.resolution = { m_RenderDevice.WindowWidth(), m_RenderDevice.WindowHeight(), 1 },
					.format     = eFormat::RGBA8_UNORM,
					.imageUsage = eTextureUsage_Storage | eTextureUsage_TransferSource | eTextureUsage_ColorAttachment
				});

		m_ToneMapping.pToneMappingPSO = ComputePipeline::Create(m_RenderDevice, "ToneMappingPSO");
		m_ToneMapping.pToneMappingPSO->SetComputeShader(
			Shader::Create(m_RenderDevice, "ToneMappingCS",
				{
					.stage    = eShaderStage::Compute,
					.filename = "ToneMappingCS" 
				}
			)).Build();
	}
}

void PostProcessNode::Apply(render::CommandContext& context, const SceneRenderView& renderView)
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

void PostProcessNode::Resize(u32 width, u32 height, u32 depth)
{
	m_TAA.ApplyCounter = 0;
	if (m_TAA.pAntiAliasedTexture)
		m_TAA.pAntiAliasedTexture->Resize(width, height, depth);
	if (m_TAA.pHistoryTexture)
		m_TAA.pHistoryTexture->Resize(width, height, depth);
}

void PostProcessNode::ApplyHeightFog(render::CommandContext& context, const SceneRenderView& renderView)
{
	// TODO
	UNUSED(context);
	UNUSED(renderView);
	using namespace render;
}

void PostProcessNode::ApplyBloom(render::CommandContext& context, const SceneRenderView& renderView)
{
	// TODO
	UNUSED(context);
	UNUSED(renderView);
	using namespace render;
}

void PostProcessNode::ApplyAntiAliasing(render::CommandContext& context, const SceneRenderView& renderView)
{
	using namespace render;

	auto& rm = m_RenderDevice.GetResourceManager();

	auto pColor       = g_FrameData.pColor.lock();
	auto pVelocity    = g_FrameData.pVelocity.lock();
	assert(
		pColor &&
		pVelocity &&
		g_FrameData.pLinearClamp
	);
	{
		const bool bFirstApply = m_TAA.ApplyCounter == 0;

		context.SetRenderPipeline(m_TAA.pTemporalAntiAliasingPSO.get());

		context.TransitionBarrier(pColor, eTextureLayout::ShaderReadOnly);
		context.TransitionBarrier(pVelocity, eTextureLayout::ShaderReadOnly);
		if (!bFirstApply)
			context.TransitionBarrier(m_TAA.pHistoryTexture, eTextureLayout::ShaderReadOnly);
		else
			context.TransitionBarrier(rm.GetFlatBlackTexture(), eTextureLayout::ShaderReadOnly);
		context.TransitionBarrier(m_TAA.pAntiAliasedTexture, eTextureLayout::General);

		struct
		{
			float  blendFactor;
			u32    isFirstFrame;
		} constant = { bFirstApply ? 1.0f : renderView.postProcess.aa.blendFactor, bFirstApply };
		context.SetComputeConstants(sizeof(constant), &constant);
		context.StageDescriptor("g_SceneTexture", pColor, g_FrameData.pLinearClamp);
		context.StageDescriptor("g_VelocityTexture", pVelocity, g_FrameData.pLinearClamp);
		context.StageDescriptor("g_HistoryTexture", bFirstApply ? rm.GetFlatBlackTexture() : m_TAA.pHistoryTexture, g_FrameData.pLinearClamp);
		context.StageDescriptor("g_OutputImage", m_TAA.pAntiAliasedTexture);

		context.Dispatch2D< 16, 16 >(m_TAA.pAntiAliasedTexture->Width(), m_TAA.pAntiAliasedTexture->Height());

		context.CopyTexture(m_TAA.pHistoryTexture, m_TAA.pAntiAliasedTexture);
	}
	{
		context.SetRenderPipeline(m_TAA.pSharpenPSO.get());

		context.TransitionBarrier(m_TAA.pAntiAliasedTexture, eTextureLayout::ShaderReadOnly);
		context.TransitionBarrier(pColor, eTextureLayout::General);

		struct
		{
			float sharpness;
		} constant = { renderView.postProcess.aa.sharpness };
		context.SetComputeConstants(sizeof(constant), &constant);
		context.StageDescriptor("g_AntiAliasedTexture", m_TAA.pAntiAliasedTexture, g_FrameData.pLinearClamp);
		context.StageDescriptor("g_OutputImage", pColor);

		context.Dispatch2D< 16, 16 >(pColor->Width(), pColor->Height());
	}

	m_TAA.ApplyCounter++;
}

void PostProcessNode::ApplyToneMapping(render::CommandContext& context, const SceneRenderView& renderView)
{
	using namespace render;

	auto& rm = m_RenderDevice.GetResourceManager();

	auto pColor = g_FrameData.pColor.lock();
	assert(
		pColor &&
		g_FrameData.pLinearClamp
	);

	context.SetRenderPipeline(m_ToneMapping.pToneMappingPSO.get());

	context.TransitionBarrier(pColor, eTextureLayout::ShaderReadOnly);
	context.TransitionBarrier(m_ToneMapping.pResolvedTexture, eTextureLayout::General);

	// Tone mapping push constants
	struct
	{
		u32   tonemapOp; // 0: None, 1: Reinhard, 2: ACES, 3: Uncharted2
		float ev100;
		float gamma;
	} constant = { (u32)renderView.postProcess.tonemap.op, renderView.postProcess.tonemap.ev100, renderView.postProcess.tonemap.gamma };
	context.SetComputeConstants(sizeof(constant), &constant);
	context.StageDescriptor("g_SceneTexture", g_FrameData.pColor.lock(), g_FrameData.pLinearClamp);
	context.StageDescriptor("g_OutputImage", m_ToneMapping.pResolvedTexture);

	context.Dispatch2D< 16, 16 >(m_ToneMapping.pResolvedTexture->Width(), m_ToneMapping.pResolvedTexture->Height());

	g_FrameData.pColor = m_ToneMapping.pResolvedTexture;
}

} // namespace baamboo
