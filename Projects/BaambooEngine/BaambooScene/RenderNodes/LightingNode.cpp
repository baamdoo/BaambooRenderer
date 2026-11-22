#include "BaambooPch.h"
#include "ShaderShared/Light.h"
#include "ShaderShared/VoxelTerrain.h"
#include "LightingNode.h"

#include "RenderCommon/RenderDevice.h"
#include "RenderCommon/SceneResource.h"
#include "RenderCommon/CommandContext.h"
#include "BaambooScene/Scene.h"
#include "PipelineFactory.h"

namespace baamboo
{

namespace
{

u32 ceilDiv(u32 a, u32 b)
{
	return (a + b - 1) / b;
}

}


// =========================================================================
// ClusterBuildNode
// =========================================================================
ClusterBuildNode::ClusterBuildNode(render::RenderDevice& rd, shader::PipelineFactory& pipelineFactory)
	: Super(rd, "ClusterBuildPass")
{
	using namespace render;

	m_pClusterAABBBuffer = Buffer::Create(rd, "ClusterBuildPass::ClusterAABBs",
		{
			.count              = MAX_CLUSTER_COUNT,
			.elementSizeInBytes = sizeof(ClusterAABB),
			.bufferUsage        = eBufferUsage_Storage,
		});

	m_NumTilesX = ceilDiv(rd.WindowWidth(),  CLUSTER_TILE_SIZE_PX);
	m_NumTilesY = ceilDiv(rd.WindowHeight(), CLUSTER_TILE_SIZE_PX);

	m_pClusterBuildPSO = pipelineFactory.CreateAndBuildComputePipeline("ClusterBuildPSO", { .source = "ClusterBuildCS.slang" });
}

void ClusterBuildNode::Apply(render::CommandContext& context, const SceneRenderView& renderView)
{
	using namespace render;

	if (renderView.bFrozen)
	{
		assert(g_FrameData.pClusterAABBBuffer && "ClusterBuild skipped under freeze but no prior buffer; toggle freeze off and on again");
		return;
	}

	context.SetRenderPipeline(m_pClusterBuildPSO.get());

	context.TransitionBufferToWrite(m_pClusterAABBBuffer, ePipelineStage::ComputeShader);

	const ClusterGridConstants constants = {
		.numTilesX = m_NumTilesX,
		.numTilesY = m_NumTilesY,
	};
	context.SetComputeConstants(sizeof(constants), &constants);

	context.StageDescriptor("ID_Scene", m_RenderDevice.GetResourceManager().GetSceneResource().GetSceneGlobals());
	context.StageDescriptor("ID_ClusterBuffer", m_pClusterAABBBuffer);

	context.Dispatch3D< 4, 4, 4 >(m_NumTilesX, m_NumTilesY, CLUSTER_SLICES_Z);

	context.TransitionBufferToRead(m_pClusterAABBBuffer, ePipelineStage::ComputeShader);

	g_FrameData.pClusterAABBBuffer = m_pClusterAABBBuffer;
}

void ClusterBuildNode::Resize(u32 width, u32 height, u32 depth)
{
	UNUSED(depth);
	m_NumTilesX = ceilDiv(width,  CLUSTER_TILE_SIZE_PX);
	m_NumTilesY = ceilDiv(height, CLUSTER_TILE_SIZE_PX);
}


// =========================================================================
// LightCullingNode
// =========================================================================
LightCullingNode::LightCullingNode(render::RenderDevice& rd, shader::PipelineFactory& pipelineFactory)
	: Super(rd, "LightCullingPass")
{
	using namespace render;

	m_pLightGridBuffer = Buffer::Create(rd, "LightCullingPass::LightGrid",
		{
			.count              = MAX_CLUSTER_COUNT,
			.elementSizeInBytes = sizeof(u32) * 2,  // uint2
			.bufferUsage        = eBufferUsage_Storage,
		});

	m_pLightListDataBuffer = Buffer::Create(rd, "LightCullingPass::LightListData",
		{
			.count              = MAX_LIGHTS_PER_CLUSTER * MAX_CLUSTER_COUNT,
			.elementSizeInBytes = sizeof(u32),
			.bufferUsage        = eBufferUsage_Storage,
		});

	m_NumTilesX = ceilDiv(rd.WindowWidth(),  CLUSTER_TILE_SIZE_PX);
	m_NumTilesY = ceilDiv(rd.WindowHeight(), CLUSTER_TILE_SIZE_PX);

	m_pCountPSO = pipelineFactory.CreateAndBuildComputePipeline("LightCullingCountPSO", { .source = "LightCullingCS.slang", .entryPoint = "CountLights" });
	m_pScanPSO  = pipelineFactory.CreateAndBuildComputePipeline("LightCullingScanPSO", { .source = "LightCullingCS.slang", .entryPoint = "ScanLightGrid" });
	m_pWritePSO = pipelineFactory.CreateAndBuildComputePipeline("LightCullingWritePSO", { .source = "LightCullingCS.slang", .entryPoint = "WriteLightList" });
}

void LightCullingNode::Apply(render::CommandContext& context, const SceneRenderView& renderView)
{
	UNUSED(renderView);
	using namespace render;

	assert(g_FrameData.pClusterAABBBuffer && "LightCullingNode requires ClusterBuildNode to run first");
	auto pClusterAABB = g_FrameData.pClusterAABBBuffer.lock();

	const auto& pSceneGlobals = m_RenderDevice.GetResourceManager().GetSceneResource().GetSceneGlobals();

	const ClusterGridConstants constants = {
		.numTilesX = m_NumTilesX,
		.numTilesY = m_NumTilesY,
	};

	// === Pass 1: Count ===
	context.SetRenderPipeline(m_pCountPSO.get());
	context.TransitionBufferToRead (pClusterAABB,         ePipelineStage::ComputeShader);
	context.TransitionBufferToWrite(m_pLightGridBuffer,   ePipelineStage::ComputeShader);

	context.SetComputeConstants(sizeof(constants), &constants);
	context.StageDescriptor("ID_Scene",           pSceneGlobals);
	context.StageDescriptor("ID_ClusterBuffer",   pClusterAABB);
	context.StageDescriptor("ID_LightGridBuffer", m_pLightGridBuffer);
	context.Dispatch3D< 4, 4, 4 >(m_NumTilesX, m_NumTilesY, CLUSTER_SLICES_Z);

	// === Pass 2: Scan (prefix-sum) ===
	context.SetRenderPipeline(m_pScanPSO.get());
	context.UAVBarrier(m_pLightGridBuffer);

	context.SetComputeConstants(sizeof(constants), &constants);
	context.StageDescriptor("ID_LightGridBuffer", m_pLightGridBuffer);
	context.Dispatch1D< 1 >(1);

	// === Pass 3: Write ===
	context.SetRenderPipeline(m_pWritePSO.get());
	context.TransitionBufferToRead (m_pLightGridBuffer,     ePipelineStage::ComputeShader);
	context.TransitionBufferToWrite(m_pLightListDataBuffer, ePipelineStage::ComputeShader);

	context.SetComputeConstants(sizeof(constants), &constants);
	context.StageDescriptor("ID_Scene",               pSceneGlobals);
	context.StageDescriptor("ID_ClusterBuffer",       pClusterAABB);
	context.StageDescriptor("ID_LightGridBuffer",     m_pLightGridBuffer);
	context.StageDescriptor("ID_LightListDataBuffer", m_pLightListDataBuffer);
	context.Dispatch3D< 4, 4, 4 >(m_NumTilesX, m_NumTilesY, CLUSTER_SLICES_Z);

	context.TransitionBufferToRead(m_pLightListDataBuffer, ePipelineStage::ComputeShader);

	g_FrameData.pLightGridBuffer     = m_pLightGridBuffer;
	g_FrameData.pLightListDataBuffer = m_pLightListDataBuffer;
}

void LightCullingNode::Resize(u32 width, u32 height, u32 depth)
{
	UNUSED(depth);
	m_NumTilesX = ceilDiv(width,  CLUSTER_TILE_SIZE_PX);
	m_NumTilesY = ceilDiv(height, CLUSTER_TILE_SIZE_PX);
}


// =========================================================================
// LightingNode
// =========================================================================
LightingNode::LightingNode(render::RenderDevice& rd, shader::PipelineFactory& pipelineFactory)
	: Super(rd, "LightingPass")
{
	using namespace render;
	auto& rm = m_RenderDevice.GetResourceManager();

	m_pSceneTexture =
		Texture::Create(
			m_RenderDevice,
			"LightingPass::Out",
			{
				.resolution = { m_RenderDevice.WindowWidth(), m_RenderDevice.WindowHeight(), 1 },
				.format     = eFormat::RGBA16_FLOAT,
				.imageUsage = eTextureUsage_Sample | eTextureUsage_Storage | eTextureUsage_TransferSource | eTextureUsage_ColorAttachment
			});

	m_pLtcLut1 = rm.LoadTexture(TEXTURE_PATH.string() + "ltc_1.dds");
	m_pLtcLut2 = rm.LoadTexture(TEXTURE_PATH.string() + "ltc_2.dds");

	m_pFallbackLightGridBuffer = Buffer::Create(rd, "LightingPass::FallbackLightGrid",
		{
			.count              = MAX_CLUSTER_COUNT,
			.elementSizeInBytes = sizeof(u32) * 2,
			.bufferUsage        = eBufferUsage_Storage | eBufferUsage_TransferDest,
		});

	m_pFallbackLightListDataBuffer = Buffer::Create(rd, "LightingPass::FallbackLightListData",
		{
			.count              = 1,
			.elementSizeInBytes = sizeof(u32),
			.bufferUsage        = eBufferUsage_Storage | eBufferUsage_TransferDest,
		});

	m_pVoxelChunkDescFallback = Buffer::Create(rd, "LightingPass::VoxelChunkDescFallback",
		{
			.count              = 1,
			.elementSizeInBytes = sizeof(VoxelChunkDesc),
			.bufferUsage        = eBufferUsage_Storage,
		});

	m_pLightingPSO = pipelineFactory.CreateAndBuildComputePipeline("LightingPSO", { .source = "DeferredPBRLightingCS.slang" });
}

void LightingNode::Apply(render::CommandContext& context, const SceneRenderView& renderView)
{
	using namespace render;
	auto& rm = m_RenderDevice.GetResourceManager();

	assert(g_FrameData.pDepth);
	assert(
		g_FrameData.pVBuf0 &&
		g_FrameData.pVBuf1 &&
		g_FrameData.pCoreNormal &&
		g_FrameData.pCoreMaterial &&
		"LightingNode requires the SurfaceResolve pass (VBuf + CoreCache) to run first"
	);

	auto pVoxDescs          = g_FrameData.pVoxelChunkDescs.lock();
	auto pAerialPerspective = g_FrameData.pAerialPerspectiveLUT ? g_FrameData.pAerialPerspectiveLUT.lock() : rm.GetFlatBlackTexture3D();
	auto pCloudScattering   = g_FrameData.pCloudScatteringLUT ? g_FrameData.pCloudScatteringLUT.lock() : rm.GetFlatBlackTexture();
	auto pSkybox            = g_FrameData.pSkyboxLUT ? g_FrameData.pSkyboxLUT.lock() : rm.GetFlatBlackTextureCube();

	auto pLightGridBuffer     = g_FrameData.pLightGridBuffer ? g_FrameData.pLightGridBuffer.lock() : nullptr;
	auto pLightListDataBuffer = g_FrameData.pLightListDataBuffer ? g_FrameData.pLightListDataBuffer.lock() : nullptr;

	context.SetRenderPipeline(m_pLightingPSO.get());

	if (pVoxDescs)
		context.TransitionBufferToRead(pVoxDescs, ePipelineStage::ComputeShader);
	context.TransitionBarrier(g_FrameData.pDepth.lock(), eTextureLayout::ShaderReadOnly);
	context.TransitionBarrier(g_FrameData.pVBuf0.lock(), eTextureLayout::ShaderReadOnly);
	context.TransitionBarrier(g_FrameData.pVBuf1.lock(), eTextureLayout::ShaderReadOnly);
	context.TransitionBarrier(g_FrameData.pCoreNormal.lock(), eTextureLayout::ShaderReadOnly);
	context.TransitionBarrier(g_FrameData.pCoreMaterial.lock(), eTextureLayout::ShaderReadOnly);
	context.TransitionBarrier(pAerialPerspective, eTextureLayout::ShaderReadOnly);
	context.TransitionBarrier(pCloudScattering, eTextureLayout::ShaderReadOnly);
	context.TransitionBarrier(pSkybox, eTextureLayout::ShaderReadOnly);
	context.TransitionBarrier(m_pLtcLut1, eTextureLayout::ShaderReadOnly);
	context.TransitionBarrier(m_pLtcLut2, eTextureLayout::ShaderReadOnly);
	context.TransitionBarrier(m_pSceneTexture, eTextureLayout::General);

	if (!pLightGridBuffer || !pLightListDataBuffer)
	{
		context.ClearBuffer(m_pFallbackLightGridBuffer, 0);
		context.ClearBuffer(m_pFallbackLightListDataBuffer, 0);
		context.TransitionBufferToRead(m_pFallbackLightGridBuffer, ePipelineStage::ComputeShader);
		context.TransitionBufferToRead(m_pFallbackLightListDataBuffer, ePipelineStage::ComputeShader);

		pLightGridBuffer     = m_pFallbackLightGridBuffer;
		pLightListDataBuffer = m_pFallbackLightListDataBuffer;
	}

	const DeferredLightingConstants constants = {
		.debugView = renderView.debugFlags.surfaceDebugView,
	};
	context.SetComputeConstants(sizeof(constants), &constants);

	context.StageDescriptor("ID_Scene", rm.GetSceneResource().GetSceneGlobals());
	context.StageDescriptor("ID_DepthBuffer", g_FrameData.pDepth.lock(), g_FrameData.pPointClamp);
	context.StageDescriptor("ID_VBuf0", g_FrameData.pVBuf0.lock(), g_FrameData.pPointClampNearest);
	context.StageDescriptor("ID_VBuf1", g_FrameData.pVBuf1.lock(), g_FrameData.pPointClampNearest);
	context.StageDescriptor("ID_CoreNormal", g_FrameData.pCoreNormal.lock(), g_FrameData.pPointClamp);
	context.StageDescriptor("ID_CoreMaterial", g_FrameData.pCoreMaterial.lock(), g_FrameData.pPointClamp);
	context.StageDescriptor("ID_AerialPerspectiveLUT", pAerialPerspective, g_FrameData.pLinearClamp);
	context.StageDescriptor("ID_CloudScatteringLUT", pCloudScattering, g_FrameData.pLinearClamp);
	context.StageDescriptor("ID_SkyboxLUT", pSkybox, g_FrameData.pLinearWrap);
	context.StageDescriptor("ID_LtcMatrixLUT",    m_pLtcLut1, g_FrameData.pLinearClamp);
	context.StageDescriptor("ID_LtcAmplitudeLUT", m_pLtcLut2, g_FrameData.pLinearClamp);
	context.StageDescriptor("ID_LightGridBuffer", pLightGridBuffer);
	context.StageDescriptor("ID_LightListDataBuffer", pLightListDataBuffer);
	context.StageDescriptor("ID_VoxelChunkDescs", pVoxDescs ? pVoxDescs : m_pVoxelChunkDescFallback);
	context.StageDescriptor("ID_OutSceneTexture", m_pSceneTexture);

	context.Dispatch2D< 16, 16 >(m_pSceneTexture->Width(), m_pSceneTexture->Height());

	g_FrameData.pColor = m_pSceneTexture;
}

void LightingNode::Resize(u32 width, u32 height, u32 depth)
{
	if (m_pSceneTexture)
		m_pSceneTexture->Resize(width, height, depth);
}


} // namespace baamboo
