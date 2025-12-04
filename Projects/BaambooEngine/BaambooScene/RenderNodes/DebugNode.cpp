#include "BaambooPch.h"
#include "DebugNode.h"
#include "RenderCommon/RenderDevice.h"
#include "RenderCommon/CommandContext.h"
#include "BaambooScene/Scene.h"

namespace baamboo
{

DebugNode::DebugNode(render::RenderDevice& rd)
	: Super(rd, "DebugPass")
{
	using namespace render;
	{
		auto pAttachment0 =
			Texture::Create(
				m_RenderDevice,
				"DebugPass::BoundingLineTexture",
				{
					.resolution = { m_RenderDevice.WindowWidth(), m_RenderDevice.WindowHeight(), 1 },
					.format     = eFormat::RGBA16_FLOAT,
					.imageUsage = eTextureUsage_Sample | eTextureUsage_ColorAttachment
				});

		m_Bounding.pRenderTarget = RenderTarget::CreateEmpty(m_RenderDevice, "DebugPass::BoundingLineRT");
		m_Bounding.pRenderTarget->AttachTexture(eAttachmentPoint::Color0, pAttachment0).Build();

		auto vs = Shader::Create(m_RenderDevice, "DebugBoundingVS",
			{
				.stage    = eShaderStage::Vertex,
				.filename = "DebugBoundingVS"
			});
		auto gs = Shader::Create(m_RenderDevice, "DebugBoundingGS",
			{
				.stage    = eShaderStage::Geometry,
				.filename = "DebugBoundingGS"
			});
		auto ps = Shader::Create(m_RenderDevice, "DebugBoundingPS",
			{
				.stage    = eShaderStage::Fragment,
				.filename = "DebugBoundingPS"
			});
		m_Bounding.pBoundingLinePSO = GraphicsPipeline::Create(m_RenderDevice, "BoundingLinePSO");
		m_Bounding.pBoundingLinePSO->SetShaders(vs, ps, gs)
			.SetRenderTarget(m_Bounding.pRenderTarget)
			.SetDepthTestEnable(true, eCompareOp::LessEqual)
			.SetTopology(ePrimitiveTopology::Point).Build();
	}
	{
		
	}
}

void DebugNode::Apply(render::CommandContext& context, const SceneRenderView& renderView)
{
	if (renderView.debug.effectBits & (1 << eDebugDraw::BoundingLine))
		ApplyBoundingLines(context, renderView);
}

void DebugNode::Resize(u32 width, u32 height, u32 depth)
{
	if (m_Bounding.pRenderTarget)
		m_Bounding.pRenderTarget->Resize(width, height, depth);
}

void DebugNode::ApplyBoundingLines(render::CommandContext& context, const SceneRenderView& renderView)
{
	using namespace render;

	UNUSED(renderView);
	using namespace render;

	auto& rm = m_RenderDevice.GetResourceManager();

	context.BeginRenderPass(m_Bounding.pRenderTarget);
	{
		context.SetRenderPipeline(m_Bounding.pBoundingLinePSO.get());

		context.DrawScene(rm.GetSceneResource());
	}
	context.EndRenderPass();

	m_Bounding.pRenderTarget->InvalidateImageLayout();

	g_FrameData.pColor = m_Bounding.pRenderTarget->Attachment(eAttachmentPoint::Color0);
}


} // namespace baamboo