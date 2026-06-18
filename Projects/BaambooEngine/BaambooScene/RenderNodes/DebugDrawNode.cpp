#include "BaambooPch.h"
#include "DebugDrawNode.h"

#include "RenderCommon/RenderDevice.h"
#include "RenderCommon/CommandContext.h"
#include "BaambooScene/Scene.h"

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
	u32    typeMask;     // 1u << LT_OFF_*
	float3 color;
	const char* label;
};
constexpr u32 kLtBitSpot   = 0u;
constexpr u32 kLtBitArea   = 1u;
constexpr u32 kLtBitSphere = 2u;
constexpr u32 kLtBitDisk   = 3u;
constexpr u32 kLtBitTube   = 4u;

constexpr u32 kMaxVertsPerLight = 192u;

} // namespace


// =========================================================================
// DebugDrawNode — construction
// =========================================================================
DebugDrawNode::DebugDrawNode(render::RenderDevice& rd)
	: Super(rd, "DebugDrawPass")
{
	using namespace render;

	auto pPS = Shader::Create(rd, "DebugWireframePS", { .stage = eShaderStage::Fragment, .filename = "DebugWireframePS" });

	// --- Frustum PSO ---
	{
		auto pVS = Shader::Create(rd, "DebugFrustumVS", { .stage = eShaderStage::Vertex, .filename = "DebugFrustumVS" });

		m_pFrustumPSO = GraphicsPipeline::Create(rd, "DebugFrustumPSO");
		m_pFrustumPSO->SetShaders(pVS, pPS)
			          .SetTopology(ePrimitiveTopology::Line)
			          .SetCullMode(eCullMode::None);
	}

	// --- Cluster PSO ---
	{
		auto pVS = Shader::Create(rd, "DebugClusterVS", { .stage = eShaderStage::Vertex, .filename = "DebugClusterVS" });

		m_pClusterPSO = GraphicsPipeline::Create(rd, "DebugClusterPSO");
		m_pClusterPSO->SetShaders(pVS, pPS)
			          .SetTopology(ePrimitiveTopology::Line)
			          .SetCullMode(eCullMode::None)
			          .SetBlendEnable(0, true)
			          .SetColorBlending(0, eBlendFactor::SrcAlpha, eBlendFactor::SrcAlphaInv, eBlendOp::Add)
			          .SetAlphaBlending(0, eBlendFactor::One,      eBlendFactor::Zero,        eBlendOp::Add);
	}

	// --- Light PSO ---
	{
		auto pVS = Shader::Create(rd, "DebugLightVS", { .stage = eShaderStage::Vertex, .filename = "DebugLightVS" });

		m_pLightPSO = GraphicsPipeline::Create(rd, "DebugLightPSO");
		m_pLightPSO->SetShaders(pVS, pPS)
			.SetTopology(ePrimitiveTopology::Line)
			.SetCullMode(eCullMode::None);
		// No alpha-blend: light gizmos read more clearly as solid wireframe.
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
}

void DebugDrawNode::Apply(render::CommandContext& context, const SceneRenderView& renderView)
{
	using namespace render;

	const bool bDrawFrustum = renderView.bFrozen;
	const bool bDrawCluster = renderView.debugFlags.bShowClusterWireframe
		&& g_FrameData.pClusterAABBBuffer
		&& g_FrameData.pLightGridBuffer;
	const bool bDrawLights  = renderView.debugFlags.lightTypeMask != 0u
		&& (renderView.light.numSpots + renderView.light.numAreas + renderView.light.numSpheres
			+ renderView.light.numDisks + renderView.light.numTubes) > 0u;

	if (!bDrawFrustum && !bDrawCluster && !bDrawLights)
		return;

	if (!g_FrameData.pColor)
		return;
	auto pColor = g_FrameData.pColor.lock();
	if (!pColor)
		return;

	EnsureRenderTarget(pColor);

	context.TransitionBarrier(pColor, eTextureLayout::ColorAttachment);

	context.BeginRenderPass(m_pRenderTarget);
	{
		if (bDrawFrustum)
			ApplyFrustumWireframe(context);

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

	struct
	{
		float r, g, b, a;
	} push = { 1.0f, 0.85f, 0.15f, 1.0f };
	context.SetGraphicsConstants(sizeof(push), &push);

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
	if (renderView.debugFlags.bClusterHeatmap)    flagsBits |= 0x1u;
	if (renderView.debugFlags.bSkipEmptyClusters) flagsBits |= 0x2u;

	struct
	{
		float r, g, b;
		float a;
		u32   numTilesX;
		u32   numTilesY;
		u32   numSlices;
		u32   flagsBits;
		u32   saturationMax;
		float padX, padY, padZ;
	} push = {
		// uniform color (when heatmap off) — soft white, alpha tuned for readability
		.r = 1.0f, .g = 1.0f, .b = 1.0f, .a = 0.3f,
		.numTilesX     = numTilesX,
		.numTilesY     = numTilesY,
		.numSlices     = numZ,
		.flagsBits     = flagsBits,
		.saturationMax = std::max(renderView.debugFlags.saturationMax, 1u),
		.padX = 0.0f, .padY = 0.0f, .padZ = 0.0f,
	};
	context.SetGraphicsConstants(sizeof(push), &push);

	context.StageDescriptor("g_ClusterBuffer",   g_FrameData.pClusterAABBBuffer.lock());
	context.StageDescriptor("g_LightGridBuffer", g_FrameData.pLightGridBuffer.lock());

	context.Draw(24u, clusterCount);
}

void DebugDrawNode::ApplyLightWireframe(render::CommandContext& context, const SceneRenderView& renderView)
{
	using namespace render;

	context.SetRenderPipeline(m_pLightPSO.get());

	const LightTypeStyle types[] = {
		{ 1u << kLtBitSpot,   { 1.0f, 0.85f, 0.30f }, "Spot"   },
		{ 1u << kLtBitArea,   { 0.30f, 0.85f, 1.0f }, "Area"   },
		{ 1u << kLtBitSphere, { 1.0f, 0.40f, 0.70f }, "Sphere" },
		{ 1u << kLtBitDisk,   { 0.70f, 1.0f, 0.40f }, "Disk"   },
		{ 1u << kLtBitTube,   { 0.40f, 0.70f, 1.0f }, "Tube"   },
	};

	for (const auto& t : types)
	{
		if ((renderView.debugFlags.lightTypeMask & t.typeMask) == 0u)
			continue;

		u32 instanceCount = 0u;
		if      (t.typeMask == (1u << kLtBitSpot))   instanceCount = renderView.light.numSpots;
		else if (t.typeMask == (1u << kLtBitArea))   instanceCount = renderView.light.numAreas;
		else if (t.typeMask == (1u << kLtBitSphere)) instanceCount = renderView.light.numSpheres;
		else if (t.typeMask == (1u << kLtBitDisk))   instanceCount = renderView.light.numDisks;
		else if (t.typeMask == (1u << kLtBitTube))   instanceCount = renderView.light.numTubes;

		if (instanceCount == 0u)
			continue;

		struct
		{
			float r, g, b;
			float a;
			u32   typeMask;
			u32   padX, padY, padZ;
		} push = {
			.r = t.color.r, .g = t.color.g, .b = t.color.b, .a = 1.0f,
			.typeMask = t.typeMask,
			.padX = 0u, .padY = 0u, .padZ = 0u,
		};
		context.SetGraphicsConstants(sizeof(push), &push);

		context.Draw(kMaxVertsPerLight, instanceCount);
	}
}

void DebugDrawNode::Resize(u32 width, u32 height, u32 depth)
{
	UNUSED(width); UNUSED(height); UNUSED(depth);
	
	m_pRenderTarget.reset();
	m_pColorRef.reset();
}

void DebugDrawNode::DrawUI()
{
}

} // namespace baamboo
