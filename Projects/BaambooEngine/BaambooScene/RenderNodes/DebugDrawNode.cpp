#include "BaambooPch.h"
#include "DebugDrawNode.h"

#include "RenderCommon/RenderDevice.h"
#include "RenderCommon/CommandContext.h"
#include "RenderCommon/SceneResource.h"
#include "BaambooScene/Scene.h"
#include "PipelineFactory.h"

#include <imgui.h>

namespace baamboo
{

namespace
{

u32 CeilDiv(u32 a, u32 b)
{
	return (a + b - 1) / b;
}

struct LightTypeStyle
{
	u32    typeMask;     // 1u << kDebugLightBit*
	float3 color;
	const char* label;
};

} // namespace


// =========================================================================
// DebugDrawNode — construction
// =========================================================================
DebugDrawNode::DebugDrawNode(render::RenderDevice& rd, shader::PipelineFactory& pipelineFactory)
	: Super(rd, "DebugDrawPass")
{
	using namespace render;

	const shader::ShaderDesc wireframePS = { .source = "DebugWireframePS.slang" };

	// --- Frustum PSO ---
	{
		m_pFrustumPSO = pipelineFactory.CreateGraphicsPipeline("DebugFrustumPSO",
			{
				.vertex = { .source = "DebugFrustumVS.slang" },
				.pixel  = wireframePS,
			});
		m_pFrustumPSO->SetTopology(ePrimitiveTopology::Line)
			          .SetCullMode(eCullMode::None);
	}

	// --- Cluster PSO ---
	{
		m_pClusterPSO = pipelineFactory.CreateGraphicsPipeline("DebugClusterPSO",
			{
				.vertex = { .source = "DebugClusterVS.slang" },
				.pixel  = wireframePS,
			});
		m_pClusterPSO->SetTopology(ePrimitiveTopology::Line)
			          .SetCullMode(eCullMode::None)
			          .SetBlendEnable(0, true)
			          .SetColorBlending(0, eBlendFactor::SrcAlpha, eBlendFactor::SrcAlphaInv, eBlendOp::Add)
			          .SetAlphaBlending(0, eBlendFactor::One,      eBlendFactor::Zero,        eBlendOp::Add);
	}

	// --- Light PSO ---
	{
		m_pLightPSO = pipelineFactory.CreateGraphicsPipeline("DebugLightPSO",
			{
				.vertex = { .source = "DebugLightVS.slang" },
				.pixel  = wireframePS,
			});
		m_pLightPSO->SetTopology(ePrimitiveTopology::Line)
			.SetCullMode(eCullMode::None);
		// No alpha-blend: light gizmos read more clearly as solid wireframe.
	}

	// --- Generic debug line PSO ---
	{
		m_pDebugLinePSO = pipelineFactory.CreateGraphicsPipeline("DebugLinePSO",
			{
				.vertex = { .source = "DebugLineVS.slang" },
				.pixel  = wireframePS,
			});
		m_pDebugLinePSO->SetTopology(ePrimitiveTopology::Line)
			.SetCullMode(eCullMode::None)
			.SetBlendEnable(0, true)
			.SetColorBlending(0, eBlendFactor::SrcAlpha, eBlendFactor::SrcAlphaInv, eBlendOp::Add)
			.SetAlphaBlending(0, eBlendFactor::One, eBlendFactor::Zero, eBlendOp::Add);
	}
}

void DebugDrawNode::EnsureRenderTarget(Arc< render::Texture > pColor)
{
	using namespace render;

	// Skip if the upstream color texture is the same instance we already wrapped.
	if (m_pRenderTarget && m_pColorRef == pColor)
		return;

	m_pColorRef = pColor;
	m_pRenderTarget = RenderTarget::CreateEmpty(m_RenderDevice, "DebugDrawPass::OverlayRT");
	m_pRenderTarget->AttachTexture(eAttachmentPoint::Color0, m_pColorRef)
		            .SetLoadAttachment(eAttachmentPoint::Color0)
		            .Build();

	m_pFrustumPSO->SetRenderTarget(m_pRenderTarget).Build();
	m_pClusterPSO->SetRenderTarget(m_pRenderTarget).Build();
	m_pLightPSO  ->SetRenderTarget(m_pRenderTarget).Build();
	m_pDebugLinePSO->SetRenderTarget(m_pRenderTarget).Build();
}

void DebugDrawNode::Apply(render::CommandContext& context, const SceneRenderView& renderView)
{
	using namespace render;

	const bool bDrawFrustum = renderView.bFrozen;
	const bool bDrawDebugLines = !renderView.debugLines.empty();
	const bool bDrawCluster = renderView.debugFlags.bShowClusterWireframe
		&& g_FrameData.pClusterAABBBuffer
		&& g_FrameData.pLightGridBuffer;
	const bool bDrawLights  = renderView.debugFlags.lightTypeMask != 0u
		&& (renderView.light.numSpots + renderView.light.numAreas + renderView.light.numSpheres
			+ renderView.light.numDisks + renderView.light.numTubes) > 0u;

	if (!bDrawFrustum && !bDrawDebugLines && !bDrawCluster && !bDrawLights)
		return;

	if (!g_FrameData.pColor)
		return;
	auto pColor = g_FrameData.pColor.lock();
	if (!pColor)
		return;

	EnsureRenderTarget(pColor);

	// Copies and buffer barriers must be recorded outside the render pass (Vulkan)
	if (bDrawDebugLines)
		UploadDebugLines(context, renderView);
	if (bDrawCluster)
	{
		context.TransitionBufferToRead(g_FrameData.pClusterAABBBuffer.lock(), ePipelineStage::VertexShader);
		context.TransitionBufferToRead(g_FrameData.pLightGridBuffer.lock(),   ePipelineStage::VertexShader);
	}

	context.TransitionBarrier(pColor, eTextureLayout::ColorAttachment);

	context.BeginRenderPass(m_pRenderTarget);
	{
		if (bDrawFrustum)
			ApplyFrustumWireframe(context);

		if (bDrawDebugLines)
			ApplyDebugLines(context, renderView);

		if (bDrawCluster)
			ApplyClusterWireframe(context, renderView);

		if (bDrawLights)
			ApplyLightWireframe(context, renderView);
	}
	context.EndRenderPass();

	m_pRenderTarget->InvalidateImageLayout();

	g_FrameData.pColor = pColor;
}

void DebugDrawNode::ApplyFrustumWireframe(render::CommandContext& context)
{
	using namespace render;

	context.SetRenderPipeline(m_pFrustumPSO.get());

	const DebugFrustumConstants constants = { .lineColor = float3(1.0f, 0.85f, 0.15f), .lineAlpha = 1.0f };
	context.SetGraphicsConstants(sizeof(constants), &constants);

	context.StageDescriptor("ID_Scene", m_RenderDevice.GetResourceManager().GetSceneResource().GetSceneGlobals());

	context.Draw(24u, 1u);
}

void DebugDrawNode::ApplyClusterWireframe(render::CommandContext& context, const SceneRenderView& renderView)
{
	using namespace render;

	context.SetRenderPipeline(m_pClusterPSO.get());

	const u32 numTilesX = CeilDiv(m_RenderDevice.WindowWidth(),  CLUSTER_TILE_SIZE_PX);
	const u32 numTilesY = CeilDiv(m_RenderDevice.WindowHeight(), CLUSTER_TILE_SIZE_PX);
	const u32 numZ      = CLUSTER_SLICES_Z;
	const u32 clusterCount = numTilesX * numTilesY * numZ;

	u32 flagsBits = 0u;
	if (renderView.debugFlags.bClusterHeatmap)    flagsBits |= kDebugClusterFlagHeatmap;
	if (renderView.debugFlags.bSkipEmptyClusters) flagsBits |= kDebugClusterFlagSkipEmpty;

	const DebugClusterConstants constants = {
		// uniform color (when heatmap off) — soft white, alpha tuned for readability
		.lineColor     = float3(1.0f),
		.lineAlpha     = 0.3f,
		.numTilesX     = numTilesX,
		.numTilesY     = numTilesY,
		.numSlices     = numZ,
		.flagsBits     = flagsBits,
		.saturationMax = std::max(renderView.debugFlags.saturationMax, 1u),
	};
	context.SetGraphicsConstants(sizeof(constants), &constants);

	context.StageDescriptor("ID_Scene",           m_RenderDevice.GetResourceManager().GetSceneResource().GetSceneGlobals());
	context.StageDescriptor("ID_ClusterBuffer",   g_FrameData.pClusterAABBBuffer.lock());
	context.StageDescriptor("ID_LightGridBuffer", g_FrameData.pLightGridBuffer.lock());

	context.Draw(24u, clusterCount);
}

void DebugDrawNode::ApplyLightWireframe(render::CommandContext& context, const SceneRenderView& renderView)
{
	using namespace render;

	context.SetRenderPipeline(m_pLightPSO.get());

	// Staged once: SetGraphicsConstants below rewrites only the constants at the front of the push block
	context.StageDescriptor("ID_Scene", m_RenderDevice.GetResourceManager().GetSceneResource().GetSceneGlobals());

	const LightTypeStyle types[] = {
		{ 1u << kDebugLightBitSpot,   { 1.0f, 0.85f, 0.30f }, "Spot"   },
		{ 1u << kDebugLightBitArea,   { 0.30f, 0.85f, 1.0f }, "Area"   },
		{ 1u << kDebugLightBitSphere, { 1.0f, 0.40f, 0.70f }, "Sphere" },
		{ 1u << kDebugLightBitDisk,   { 0.70f, 1.0f, 0.40f }, "Disk"   },
		{ 1u << kDebugLightBitTube,   { 0.40f, 0.70f, 1.0f }, "Tube"   },
	};

	for (const auto& t : types)
	{
		if ((renderView.debugFlags.lightTypeMask & t.typeMask) == 0u)
			continue;

		u32 instanceCount = 0u;
		if      (t.typeMask == (1u << kDebugLightBitSpot))   instanceCount = renderView.light.numSpots;
		else if (t.typeMask == (1u << kDebugLightBitArea))   instanceCount = renderView.light.numAreas;
		else if (t.typeMask == (1u << kDebugLightBitSphere)) instanceCount = renderView.light.numSpheres;
		else if (t.typeMask == (1u << kDebugLightBitDisk))   instanceCount = renderView.light.numDisks;
		else if (t.typeMask == (1u << kDebugLightBitTube))   instanceCount = renderView.light.numTubes;

		if (instanceCount == 0u)
			continue;

		const DebugLightConstants constants = {
			.lineColor = t.color,
			.lineAlpha = 1.0f,
			.typeMask  = t.typeMask,
		};
		context.SetGraphicsConstants(sizeof(constants), &constants);

		context.Draw(kDebugLightMaxVertices, instanceCount);
	}
}

void DebugDrawNode::UploadDebugLines(render::CommandContext& context, const SceneRenderView& renderView)
{
	using namespace render;

	const u32 vertexCount = static_cast< u32 >(renderView.debugLines.size());

	const bool bBufferRecreated = !m_pDebugLineBuffer || m_DebugLineVertexCapacity < vertexCount;
	if (bBufferRecreated)
	{
		m_DebugLineVertexCapacity = std::max(vertexCount, m_DebugLineVertexCapacity * 2u);
		m_DebugLineVertexCapacity = std::max(m_DebugLineVertexCapacity, 256u);
		m_pDebugLineBuffer = Buffer::Create(m_RenderDevice, "DebugDrawPass::DebugLines",
			{
				.count = m_DebugLineVertexCapacity,
				.elementSizeInBytes = sizeof(DebugLineVertex),
				.bufferUsage = eBufferUsage_Storage | eBufferUsage_TransferDest,
			});
	}

	if (bBufferRecreated || m_DebugLineVersion != renderView.debugLinesVersion || m_DebugLineVertexCount != vertexCount)
	{
		context.UploadData(m_pDebugLineBuffer, renderView.debugLines.data(), vertexCount, sizeof(DebugLineVertex));
		m_DebugLineVersion = renderView.debugLinesVersion;
		m_DebugLineVertexCount = vertexCount;
	}
	context.TransitionBufferToRead(m_pDebugLineBuffer, ePipelineStage::VertexShader);
}

void DebugDrawNode::ApplyDebugLines(render::CommandContext& context, const SceneRenderView& renderView)
{
	using namespace render;

	context.SetRenderPipeline(m_pDebugLinePSO.get());
	context.StageDescriptor("ID_Scene",      m_RenderDevice.GetResourceManager().GetSceneResource().GetSceneGlobals());
	context.StageDescriptor("ID_DebugLines", m_pDebugLineBuffer);
	context.Draw(static_cast< u32 >(renderView.debugLines.size()), 1u);
}

void DebugDrawNode::Resize(u32 width, u32 height, u32 depth)
{
	if (m_pRenderTarget)
		m_pRenderTarget->Resize(width, height, depth);
}

void DebugDrawNode::DrawUI()
{
}

} // namespace baamboo
