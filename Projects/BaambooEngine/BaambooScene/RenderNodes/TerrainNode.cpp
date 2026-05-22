#include "BaambooPch.h"
#include "TerrainNode.h"

#include "RenderCommon/RenderDevice.h"
#include "RenderCommon/CommandContext.h"

#include "BaambooScene/Scene.h"

namespace baamboo
{

TerrainNode::TerrainNode(render::RenderDevice& rd)
	: Super(rd, "TerrainPass")
{
	using namespace render;

	auto pColor = Texture::Create(rd, "TerrainPass::Color",
		{
			.resolution = { rd.WindowWidth(), rd.WindowHeight(), 1 },
			.format     = eFormat::RG11B10_UFLOAT,
			.imageUsage = eTextureUsage_ColorAttachment | eTextureUsage_Sample,
		});
	auto pDepth = Texture::Create(rd, "TerrainPass::Depth",
		{
			.resolution      = { rd.WindowWidth(), rd.WindowHeight(), 1 },
			.format          = eFormat::D32_FLOAT,
			.imageUsage      = eTextureUsage_DepthStencilAttachment | eTextureUsage_Sample,
			.depthClearValue = 0.0f, // reversed-Z
		});

	m_pRenderTarget = RenderTarget::CreateEmpty(rd, "TerrainPass::RenderPass");
	m_pRenderTarget->AttachTexture(eAttachmentPoint::Color0, pColor)
	                .AttachTexture(eAttachmentPoint::DepthStencil, pDepth).Build();

	{
		auto pMS = Shader::Create(rd, "TerrainMS", { .stage = eShaderStage::Mesh,     .filename = "TerrainMS" });
		auto pPS = Shader::Create(rd, "TerrainPS", { .stage = eShaderStage::Fragment, .filename = "TerrainPS" });

		m_pTerrainPSO = GraphicsPipeline::Create(rd, "TerrainPSO");
		m_pTerrainPSO->SetMeshShaders(pMS, pPS)
		             .SetRenderTarget(m_pRenderTarget)
		             .SetCullMode(eCullMode::None)
		             .SetDepthWriteEnable(true, eCompareOp::Greater).Build();
	}

	m_pPatchInstances = Buffer::Create(rd, "TerrainPass::PatchInstances",
		{
			.count              = MAX_PATCHES_PER_FRAME,
			.elementSizeInBytes = sizeof(PatchInstance),
			.mapDirection       = 1, // host-write (UPLOAD heap)
			.bufferUsage        = eBufferUsage_Storage, // SRV in shader (bindless index)
		});

	m_pDispatchArgs = Buffer::Create(rd, "TerrainPass::DispatchArgs",
		{
			.count              = MAX_PATCHES_PER_FRAME,
			.elementSizeInBytes = sizeof(IndirectCommandData),
			.mapDirection       = 1, // host-write (UPLOAD heap)
			.bufferUsage        = eBufferUsage_Indirect,
		});
}

void TerrainNode::SetQuadtreeConfig(const QuadtreeConfig& cfg)
{
	m_Config = cfg;
	m_bConfigDirty = true;
}

void TerrainNode::BuildQuadtree()
{
	TerrainQuadtree::Config qc{};
	qc.rootOriginXZ  = float2(m_Config.rootOriginX, m_Config.rootOriginZ);
	qc.rootSizeMeter = m_Config.rootSizeMeter;
	qc.terrainMinY   = m_Config.heightMinMeter;
	qc.terrainMaxY   = m_Config.heightMinMeter + m_Config.heightRangeMeter;
	qc.lodRangeBase  = m_Config.lodRangeBaseMeter;
	qc.lodMorphK     = m_Config.lodMorphK;
	qc.maxDepth      = std::min(m_Config.maxDepth, TERRAIN_MAX_LOD_DEPTHS - 1u);
	qc.gridN         = m_Config.gridN;
	m_Quadtree.Build(qc);
}

void TerrainNode::FillTerrainParams(TerrainParams& params, u32 numPatches) const
{
	std::memset(&params, 0, sizeof(TerrainParams));

	const u32   rho        = m_pHeightmap ? m_pHeightmap->Width() : 1u;
	const float invRes     = 1.0f / static_cast< float >(rho);

	params.terrainOriginX   = m_Config.rootOriginX;
	params.terrainOriginZ   = m_Config.rootOriginZ;
	params.terrainSizeMeter = m_Config.terrainSizeMeter;
	params.gridN            = m_Config.gridN;

	params.heightMinMeter   = m_Config.heightMinMeter;
	params.heightRangeMeter = m_Config.heightRangeMeter;
	params.heightmapTexel   = invRes;
	params.worldPerTexel    = m_Config.terrainSizeMeter * invRes;

	params.heightmapRes = rho;
	params.maxDepth     = m_Quadtree.GetConfig().maxDepth;
	params.numPatches   = numPatches;

	const auto& rs = m_Quadtree.RangeStarts();
	const auto& re = m_Quadtree.RangeEnds();
	for (u32 d = 0u; d < TERRAIN_MAX_LOD_DEPTHS; ++d)
	{
		params.lodRangeStart[d] = (d < rs.size()) ? rs[d] : 0.0f;
		params.lodRangeEnd  [d] = (d < re.size()) ? re[d] : 0.0f;
	}
}

void TerrainNode::UploadPatches(const std::vector< PatchInstance >& patches)
{
	const u32 count = std::min< u32 >(static_cast< u32 >(patches.size()), MAX_PATCHES_PER_FRAME);

	if (auto* p = static_cast< PatchInstance* >(m_pPatchInstances->MappedMemory()))
	{
		if (count > 0u)
			std::memcpy(p, patches.data(), count * sizeof(PatchInstance));
	}

	if (auto* p = static_cast< IndirectCommandData* >(m_pDispatchArgs->MappedMemory()))
	{
		for (u32 i = 0u; i < count; ++i)
		{
			p[i].drawID      = i;
			p[i].groupCountX = 1u;
			p[i].groupCountY = 1u;
			p[i].groupCountZ = 1u;
		}
	}
}

void TerrainNode::Apply(render::CommandContext& context, const SceneRenderView& renderView)
{
	using namespace render;

	// --- Import the Gaea heightmap once (lazy; reload on path change) ---
	if (!m_pHeightmap || m_Config.heightmapPath != m_HeightmapPathCache)
	{
		m_HeightmapPathCache = m_Config.heightmapPath;

		auto& rm = m_RenderDevice.GetResourceManager();
		if (!m_Config.heightmapPath.empty() && fs::exists(m_Config.heightmapPath))
		{
			m_pHeightmap = rm.LoadTexture(m_Config.heightmapPath);
		}
		else
		{
			fprintf(stderr,
				"[TerrainNode] heightmap not found: \"%s\" — flat placeholder. "
				"Export a Gaea R16_UNORM .dds there (Docs/Terrain/01 Step 7).\n",
				m_Config.heightmapPath.c_str());
			m_pHeightmap = rm.GetFlatBlackTexture();
		}
		BB_ASSERT(m_pHeightmap != nullptr, "TerrainNode: heightmap is null");
	}

	context.TransitionBarrier(m_pHeightmap, eTextureLayout::ShaderReadOnly);

	// --- (Re)build the quadtree on config change ---
	if (m_bConfigDirty)
	{
		BuildQuadtree();
		m_bConfigDirty = false;
	}

	// --- Per-frame LOD selection ---
	const float3 cameraPos = renderView.camera.pos;
	const mat4   viewProj  = renderView.camera.mProj * renderView.camera.mView;

	const TerrainQuadtree::Frustum frustum = TerrainQuadtree::ExtractFrustum(viewProj);
	m_Quadtree.SelectLOD(cameraPos, frustum);

	const auto& emit = m_Quadtree.EmitList();
	const u32   numPatches = std::min< u32 >(static_cast< u32 >(emit.size()), MAX_PATCHES_PER_FRAME);
	if (numPatches == 0u)
	{
		context.BeginRenderPass(m_pRenderTarget);
		context.EndRenderPass();

		m_pRenderTarget->InvalidateImageLayout();
		g_FrameData.pColor = m_pRenderTarget->Attachment(eAttachmentPoint::Color0);
		g_FrameData.pDepth = m_pRenderTarget->Attachment(eAttachmentPoint::DepthStencil);
		return;
	}

	UploadPatches(emit);

	TerrainParams params{};
	FillTerrainParams(params, numPatches);

	context.BeginRenderPass(m_pRenderTarget);
	{
		context.SetRenderPipeline(m_pTerrainPSO.get());

		context.SetGraphicsDynamicUniformBuffer("g_Terrain", sizeof(TerrainParams), &params);

		context.StageDescriptor("g_Heightmap", m_pHeightmap, g_FrameData.pLinearClamp);
		context.StageDescriptor("g_PatchInstances", m_pPatchInstances);

		context.DrawMeshTasksIndirect(m_pDispatchArgs, offsetof(IndirectCommandData, groupCountX), numPatches, sizeof(IndirectCommandData));
	}
	context.EndRenderPass();

	m_pRenderTarget->InvalidateImageLayout();

	g_FrameData.pColor = m_pRenderTarget->Attachment(eAttachmentPoint::Color0);
	g_FrameData.pDepth = m_pRenderTarget->Attachment(eAttachmentPoint::DepthStencil);
}

void TerrainNode::Resize(u32 width, u32 height, u32 depth)
{
	if (m_pRenderTarget)
		m_pRenderTarget->Resize(width, height, depth);
}


} // namespace baamboo
