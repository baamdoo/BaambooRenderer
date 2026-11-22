#include "BaambooPch.h"
#include "PostProcessNode.h"
#include "RenderCommon/RenderDevice.h"
#include "RenderCommon/CommandContext.h"
#include "BaambooScene/Scene.h"
#include "PipelineFactory.h"

namespace baamboo
{

PostProcessNode::PostProcessNode(render::RenderDevice& rd, shader::PipelineFactory& pipelineFactory)
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

		m_TAA.pTemporalAntiAliasingPSO = pipelineFactory.CreateAndBuildComputePipeline("TemporalAntiAliasingPSO", { .source = "TemporalAntiAliasingCS.slang" });
		m_TAA.pSharpenPSO              = pipelineFactory.CreateAndBuildComputePipeline("SharpenPipelinePSO", { .source = "SharpenCS.slang" });
	}
	{
		for (u32 i = 0; i < kBloomMipCount; ++i)
		{
			const u32 w = std::max(1u, m_RenderDevice.WindowWidth() >> (i + 1));
			const u32 h = std::max(1u, m_RenderDevice.WindowHeight() >> (i + 1));

			m_Bloom.pDownChain[i] =
				Texture::Create(
					m_RenderDevice,
					("PostProcessPass::BloomDown" + std::to_string(i)).c_str(),
					{
						.resolution = { w, h, 1 },
						.format     = eFormat::RGBA16_FLOAT,
						.imageUsage = eTextureUsage_Sample | eTextureUsage_Storage | eTextureUsage_TransferSource
					});
			m_Bloom.pUpChain[i] =
				Texture::Create(
					m_RenderDevice,
					("PostProcessPass::BloomUp" + std::to_string(i)).c_str(),
					{
						.resolution = { w, h, 1 },
						.format     = eFormat::RGBA16_FLOAT,
						.imageUsage = eTextureUsage_Sample | eTextureUsage_Storage | eTextureUsage_TransferDest
					});
		}

		m_Bloom.pDownsamplePSO = pipelineFactory.CreateAndBuildComputePipeline("BloomDownsamplePSO", { .source = "BloomDownsampleCS.slang" });
		m_Bloom.pUpsamplePSO   = pipelineFactory.CreateAndBuildComputePipeline("BloomUpsamplePSO", { .source = "BloomUpsampleCS.slang" });
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

		m_ToneMapping.pToneMappingPSO = pipelineFactory.CreateAndBuildComputePipeline("ToneMappingPSO", { .source = "ToneMappingCS.slang" });
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
	if (m_ToneMapping.pResolvedTexture)
		m_ToneMapping.pResolvedTexture->Resize(width, height, depth);
	for (u32 i = 0; i < kBloomMipCount; ++i)
	{
		const u32 w = std::max(1u, width >> (i + 1));
		const u32 h = std::max(1u, height >> (i + 1));
		if (m_Bloom.pDownChain[i])
			m_Bloom.pDownChain[i]->Resize(w, h, depth);
		if (m_Bloom.pUpChain[i])
			m_Bloom.pUpChain[i]->Resize(w, h, depth);
	}
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
	UNUSED(renderView);
	using namespace render;

	auto pColor = g_FrameData.pColor.lock();
	assert(pColor && g_FrameData.pLinearClamp);

	context.SetRenderPipeline(m_Bloom.pDownsamplePSO.get());
	for (u32 i = 0; i < kBloomMipCount; ++i)
	{
		Arc< Texture > pSrc = (i == 0) ? pColor : m_Bloom.pDownChain[i - 1];
		Arc< Texture > pDst = m_Bloom.pDownChain[i];

		context.TransitionBarrier(pSrc, eTextureLayout::ShaderReadOnly);
		context.TransitionBarrier(pDst, eTextureLayout::General);

		const BloomDownsampleConstants constants = { .bFirstDownsample = i == 0 ? 1u : 0u };
		context.SetComputeConstants(sizeof(constants), &constants);
		context.StageDescriptor("ID_SrcTexture", pSrc, g_FrameData.pLinearClamp);
		context.StageDescriptor("ID_OutputImage", pDst);

		context.Dispatch2D< 16, 16 >(pDst->Width(), pDst->Height());
	}

	context.TransitionBarrier(m_Bloom.pDownChain[kBloomMipCount - 1], eTextureLayout::TransferSource);
	context.TransitionBarrier(m_Bloom.pUpChain[kBloomMipCount - 1], eTextureLayout::TransferDest);
	context.CopyTexture(m_Bloom.pUpChain[kBloomMipCount - 1], m_Bloom.pDownChain[kBloomMipCount - 1]);

	context.SetRenderPipeline(m_Bloom.pUpsamplePSO.get());
	for (i32 i = kBloomMipCount - 2; i >= 0; --i)
	{
		Arc< Texture > pSrcLow  = m_Bloom.pUpChain[i + 1];
		Arc< Texture > pSrcHigh = m_Bloom.pDownChain[i];
		Arc< Texture > pDst     = m_Bloom.pUpChain[i];

		context.TransitionBarrier(pSrcLow, eTextureLayout::ShaderReadOnly);
		context.TransitionBarrier(pSrcHigh, eTextureLayout::ShaderReadOnly);
		context.TransitionBarrier(pDst, eTextureLayout::General);

		const BloomUpsampleConstants constants = { .filterRadius = 1.0f };
		context.SetComputeConstants(sizeof(constants), &constants);
		context.StageDescriptor("ID_SrcLowTexture", pSrcLow, g_FrameData.pLinearClamp);
		context.StageDescriptor("ID_SrcHighTexture", pSrcHigh, g_FrameData.pLinearClamp);
		context.StageDescriptor("ID_OutputImage", pDst);

		context.Dispatch2D< 16, 16 >(pDst->Width(), pDst->Height());
	}
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

		const TemporalAntiAliasingConstants constants =
		{
			.blendFactor = bFirstApply ? 1.0f : renderView.postProcess.aa.blendFactor,
			.bFirstFrame = bFirstApply ? 1u : 0u
		};
		context.SetComputeConstants(sizeof(constants), &constants);
		context.StageDescriptor("ID_SceneTexture", pColor, g_FrameData.pLinearClamp);
		context.StageDescriptor("ID_VelocityTexture", pVelocity, g_FrameData.pLinearClamp);
		context.StageDescriptor("ID_HistoryTexture", bFirstApply ? rm.GetFlatBlackTexture() : m_TAA.pHistoryTexture, g_FrameData.pLinearClamp);
		context.StageDescriptor("ID_OutputImage", m_TAA.pAntiAliasedTexture);

		context.Dispatch2D< 16, 16 >(m_TAA.pAntiAliasedTexture->Width(), m_TAA.pAntiAliasedTexture->Height());

		context.CopyTexture(m_TAA.pHistoryTexture, m_TAA.pAntiAliasedTexture);
	}
	{
		context.SetRenderPipeline(m_TAA.pSharpenPSO.get());

		context.TransitionBarrier(m_TAA.pAntiAliasedTexture, eTextureLayout::ShaderReadOnly);
		context.TransitionBarrier(pColor, eTextureLayout::General);

		const SharpenConstants constants = { .sharpness = renderView.postProcess.aa.sharpness };
		context.SetComputeConstants(sizeof(constants), &constants);
		context.StageDescriptor("ID_AntiAliasedTexture", m_TAA.pAntiAliasedTexture, g_FrameData.pLinearClamp);
		context.StageDescriptor("ID_OutputImage", pColor);

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

	const bool bBloom =
		(renderView.postProcess.effectBits & (1 << ePostProcess::Bloom)) != 0
		&& renderView.postProcess.bloom.intensity > 0.0f;
	Arc< render::Texture > pBloom = bBloom ? m_Bloom.pUpChain[0] : rm.GetFlatBlackTexture();

	context.SetRenderPipeline(m_ToneMapping.pToneMappingPSO.get());

	context.TransitionBarrier(pColor, eTextureLayout::ShaderReadOnly);
	context.TransitionBarrier(pBloom, eTextureLayout::ShaderReadOnly);
	context.TransitionBarrier(m_ToneMapping.pResolvedTexture, eTextureLayout::General);

	const ToneMappingConstants constants =
	{
		.tonemapOp     = (u32)renderView.postProcess.tonemap.op,
		.ev100         = renderView.postProcess.tonemap.ev100,
		.gamma         = renderView.postProcess.tonemap.gamma,
		.bloomStrength = bBloom ? renderView.postProcess.bloom.intensity : 0.0f
	};
	context.SetComputeConstants(sizeof(constants), &constants);
	context.StageDescriptor("ID_SceneTexture", g_FrameData.pColor.lock(), g_FrameData.pLinearClamp);
	context.StageDescriptor("ID_OutputImage", m_ToneMapping.pResolvedTexture);
	context.StageDescriptor("ID_BloomTexture", pBloom, g_FrameData.pLinearClamp);

	context.Dispatch2D< 16, 16 >(m_ToneMapping.pResolvedTexture->Width(), m_ToneMapping.pResolvedTexture->Height());

	g_FrameData.pColor = m_ToneMapping.pResolvedTexture;
}

} // namespace baamboo
