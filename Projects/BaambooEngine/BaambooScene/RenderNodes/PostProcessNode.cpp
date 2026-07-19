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

		m_Bloom.pDownsamplePSO = ComputePipeline::Create(m_RenderDevice, "BloomDownsamplePSO");
		m_Bloom.pDownsamplePSO->SetComputeShader(
			Shader::Create(m_RenderDevice, "BloomDownsampleCS",
			{
				.stage    = eShaderStage::Compute,
				.filename = "BloomDownsampleCS"
			}
		)).Build();

		m_Bloom.pUpsamplePSO = ComputePipeline::Create(m_RenderDevice, "BloomUpsamplePSO");
		m_Bloom.pUpsamplePSO->SetComputeShader(
			Shader::Create(m_RenderDevice, "BloomUpsampleCS",
			{
				.stage    = eShaderStage::Compute,
				.filename = "BloomUpsampleCS"
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

		struct
		{
			u32 bFirstDownsample;
		} constant = { i == 0 ? 1u : 0u };
		context.SetComputeConstants(sizeof(constant), &constant);
		context.StageDescriptor("g_SrcTexture", pSrc, g_FrameData.pLinearClamp);
		context.StageDescriptor("g_OutputImage", pDst);

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

		struct
		{
			float filterRadius;
		} constant = { 1.0f };
		context.SetComputeConstants(sizeof(constant), &constant);
		context.StageDescriptor("g_SrcLowTexture", pSrcLow, g_FrameData.pLinearClamp);
		context.StageDescriptor("g_SrcHighTexture", pSrcHigh, g_FrameData.pLinearClamp);
		context.StageDescriptor("g_OutputImage", pDst);

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

	const bool bBloom =
		(renderView.postProcess.effectBits & (1 << ePostProcess::Bloom)) != 0
		&& renderView.postProcess.bloom.intensity > 0.0f;
	Arc< render::Texture > pBloom = bBloom ? m_Bloom.pUpChain[0] : rm.GetFlatBlackTexture();

	context.SetRenderPipeline(m_ToneMapping.pToneMappingPSO.get());

	context.TransitionBarrier(pColor, eTextureLayout::ShaderReadOnly);
	context.TransitionBarrier(pBloom, eTextureLayout::ShaderReadOnly);
	context.TransitionBarrier(m_ToneMapping.pResolvedTexture, eTextureLayout::General);

	// Tone mapping push constants
	struct
	{
		u32   tonemapOp; // 0: None, 1: Reinhard, 2: ACES, 3: Uncharted2, 4: Uchimura
		float ev100;
		float gamma;
		float bloomStrength;
	} constant = { (u32)renderView.postProcess.tonemap.op, renderView.postProcess.tonemap.ev100, renderView.postProcess.tonemap.gamma,
	               bBloom ? renderView.postProcess.bloom.intensity : 0.0f };
	context.SetComputeConstants(sizeof(constant), &constant);
	context.StageDescriptor("g_SceneTexture", g_FrameData.pColor.lock(), g_FrameData.pLinearClamp);
	context.StageDescriptor("g_OutputImage", m_ToneMapping.pResolvedTexture);
	context.StageDescriptor("g_BloomTexture", pBloom, g_FrameData.pLinearClamp);

	context.Dispatch2D< 16, 16 >(m_ToneMapping.pResolvedTexture->Width(), m_ToneMapping.pResolvedTexture->Height());

	g_FrameData.pColor = m_ToneMapping.pResolvedTexture;
}

} // namespace baamboo
