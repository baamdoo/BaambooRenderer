#include "BaambooPch.h"
#include "ShaderTypes.h"
#include "LightingNode.h"

#include "RenderCommon/RenderDevice.h"
#include "RenderCommon/CommandContext.h"
#include "BaambooScene/Scene.h"

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
ClusterBuildNode::ClusterBuildNode(render::RenderDevice& rd)
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

	m_pClusterBuildPSO = ComputePipeline::Create(m_RenderDevice, "ClusterBuildPSO");
	m_pClusterBuildPSO->SetComputeShader(
		Shader::Create(m_RenderDevice, "ClusterBuildCS",
			{
				.stage    = eShaderStage::Compute,
				.filename = "ClusterBuildCS"
			})).Build();
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

	struct
	{
		u32 tileSize;
		u32 numTilesX;
		u32 numTilesY;
		u32 numSlices;
	} constants = {
		.tileSize  = CLUSTER_TILE_SIZE_PX,
		.numTilesX = m_NumTilesX,
		.numTilesY = m_NumTilesY,
		.numSlices = CLUSTER_SLICES_Z,
	};
	context.SetComputeConstants(sizeof(constants), &constants);

	context.StageDescriptor("g_ClusterBuffer", m_pClusterAABBBuffer);

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
LightCullingNode::LightCullingNode(render::RenderDevice& rd)
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

	m_pCountPSO = ComputePipeline::Create(m_RenderDevice, "LightCullingCountPSO");
	m_pCountPSO->SetComputeShader(
		Shader::Create(m_RenderDevice, "LightCullingCountCS",
			{
				.stage    = eShaderStage::Compute,
				.filename = "LightCullingCountCS"
			})).Build();

	m_pScanPSO = ComputePipeline::Create(m_RenderDevice, "LightCullingScanPSO");
	m_pScanPSO->SetComputeShader(
		Shader::Create(m_RenderDevice, "LightCullingScanCS",
			{
				.stage    = eShaderStage::Compute,
				.filename = "LightCullingScanCS"
			})).Build();

	m_pWritePSO = ComputePipeline::Create(m_RenderDevice, "LightCullingWritePSO");
	m_pWritePSO->SetComputeShader(
		Shader::Create(m_RenderDevice, "LightCullingWriteCS",
			{
				.stage    = eShaderStage::Compute,
				.filename = "LightCullingWriteCS"
			})).Build();
}

void LightCullingNode::Apply(render::CommandContext& context, const SceneRenderView& renderView)
{
	UNUSED(renderView);
	using namespace render;

	assert(g_FrameData.pClusterAABBBuffer && "LightCullingNode requires ClusterBuildNode to run first");
	auto pClusterAABB = g_FrameData.pClusterAABBBuffer.lock();

	struct
	{
		u32 tileSize;
		u32 numTilesX;
		u32 numTilesY;
		u32 numSlices;
	} cullConstants = {
		.tileSize  = CLUSTER_TILE_SIZE_PX,
		.numTilesX = m_NumTilesX,
		.numTilesY = m_NumTilesY,
		.numSlices = CLUSTER_SLICES_Z,
	};

	// === Pass 1: Count ===
	context.SetRenderPipeline(m_pCountPSO.get());
	context.TransitionBufferToRead (pClusterAABB,         ePipelineStage::ComputeShader);
	context.TransitionBufferToWrite(m_pLightGridBuffer,   ePipelineStage::ComputeShader);

	context.SetComputeConstants(sizeof(cullConstants), &cullConstants);
	context.StageDescriptor("g_ClusterBuffer",   pClusterAABB);
	context.StageDescriptor("g_LightGridBuffer", m_pLightGridBuffer);
	context.Dispatch3D< 4, 4, 4 >(m_NumTilesX, m_NumTilesY, CLUSTER_SLICES_Z);

	// === Pass 2: Scan (prefix-sum) ===
	context.UAVBarrier(m_pLightGridBuffer);

	context.SetRenderPipeline(m_pScanPSO.get());

	struct
	{
		u32 numClusters;
	} scanConstants = {
		.numClusters = m_NumTilesX * m_NumTilesY * CLUSTER_SLICES_Z,
	};
	context.SetComputeConstants(sizeof(scanConstants), &scanConstants);
	context.StageDescriptor("g_LightGridBuffer", m_pLightGridBuffer);
	context.Dispatch1D< 1 >(1);

	// === Pass 3: Write ===
	context.TransitionBufferToRead (m_pLightGridBuffer,     ePipelineStage::ComputeShader);
	context.TransitionBufferToWrite(m_pLightListDataBuffer, ePipelineStage::ComputeShader);

	context.SetRenderPipeline(m_pWritePSO.get());
	context.SetComputeConstants(sizeof(cullConstants), &cullConstants);
	context.StageDescriptor("g_ClusterBuffer",       pClusterAABB);
	context.StageDescriptor("g_LightGridBuffer",     m_pLightGridBuffer);
	context.StageDescriptor("g_LightListDataBuffer", m_pLightListDataBuffer);
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
LightingNode::LightingNode(render::RenderDevice& rd)
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

	m_pLightingPSO = ComputePipeline::Create(m_RenderDevice, "LightingPSO");
	m_pLightingPSO->SetComputeShader(
		Shader::Create(m_RenderDevice, "DeferredPBRLightingCS",
			{
				.stage    = eShaderStage::Compute,
				.filename = "DeferredPBRLightingCS"
			})).Build();
}

void LightingNode::Apply(render::CommandContext& context, const SceneRenderView& renderView)
{
	using namespace render;
	auto& rm = m_RenderDevice.GetResourceManager();

	context.SetRenderPipeline(m_pLightingPSO.get());

	u32 debugView = renderView.debugFlags.surfaceDebugView;
	context.SetComputeConstants(sizeof(u32), &debugView);

	assert(g_FrameData.pDepth);
	assert(
		g_FrameData.pVBuf0 &&
		g_FrameData.pVBuf1 &&
		g_FrameData.pCoreNormal &&
		g_FrameData.pCoreMaterial &&
		"LightingNode requires the SurfaceResolve pass (VBuf + CoreCache) to run first"
	);
	context.TransitionBarrier(g_FrameData.pDepth.lock(), eTextureLayout::ShaderReadOnly);
	context.TransitionBarrier(g_FrameData.pVBuf0.lock(), eTextureLayout::ShaderReadOnly);
	context.TransitionBarrier(g_FrameData.pVBuf1.lock(), eTextureLayout::ShaderReadOnly);
	context.TransitionBarrier(g_FrameData.pCoreNormal.lock(), eTextureLayout::ShaderReadOnly);
	context.TransitionBarrier(g_FrameData.pCoreMaterial.lock(), eTextureLayout::ShaderReadOnly);
	context.TransitionBarrier(g_FrameData.pAerialPerspectiveLUT ?
		g_FrameData.pAerialPerspectiveLUT.lock() : rm.GetFlatBlackTexture3D(), eTextureLayout::ShaderReadOnly);
	context.TransitionBarrier(g_FrameData.pCloudScatteringLUT ?
		g_FrameData.pCloudScatteringLUT.lock() : rm.GetFlatBlackTexture(), eTextureLayout::ShaderReadOnly);
	context.TransitionBarrier(g_FrameData.pSkyboxLUT ?
		g_FrameData.pSkyboxLUT.lock() : rm.GetFlatBlackTextureCube(), eTextureLayout::ShaderReadOnly);
	context.TransitionBarrier(m_pLtcLut1, eTextureLayout::ShaderReadOnly);
	context.TransitionBarrier(m_pLtcLut2, eTextureLayout::ShaderReadOnly);
	context.TransitionBarrier(m_pSceneTexture, eTextureLayout::General);

	context.StageDescriptor("g_DepthBuffer", g_FrameData.pDepth.lock(), g_FrameData.pPointClamp);
	context.StageDescriptor("g_VBuf0", g_FrameData.pVBuf0.lock(), g_FrameData.pPointClampNearest);
	context.StageDescriptor("g_VBuf1", g_FrameData.pVBuf1.lock(), g_FrameData.pPointClampNearest);
	context.StageDescriptor("g_CoreNormal", g_FrameData.pCoreNormal.lock(), g_FrameData.pPointClamp);
	context.StageDescriptor("g_CoreMaterial", g_FrameData.pCoreMaterial.lock(), g_FrameData.pPointClamp);
	context.StageDescriptor("g_AerialPerspectiveLUT", g_FrameData.pAerialPerspectiveLUT ?
		g_FrameData.pAerialPerspectiveLUT.lock() : rm.GetFlatBlackTexture3D(), g_FrameData.pLinearWrap);
	context.StageDescriptor("g_CloudScatteringLUT", g_FrameData.pCloudScatteringLUT ?
		g_FrameData.pCloudScatteringLUT.lock() : rm.GetFlatBlackTexture(), g_FrameData.pLinearClamp);
	context.StageDescriptor("g_SkyboxLUT", g_FrameData.pSkyboxLUT ?
		g_FrameData.pSkyboxLUT.lock() : rm.GetFlatBlackTextureCube(), g_FrameData.pLinearWrap);
	context.StageDescriptor("g_LtcMatrixLUT",    m_pLtcLut1, g_FrameData.pLinearClamp);
	context.StageDescriptor("g_LtcAmplitudeLUT", m_pLtcLut2, g_FrameData.pLinearClamp);
	context.StageDescriptor("g_OutSceneTexture", m_pSceneTexture);

	auto pLightGridBuffer     = g_FrameData.pLightGridBuffer ? g_FrameData.pLightGridBuffer.lock() : nullptr;
	auto pLightListDataBuffer = g_FrameData.pLightListDataBuffer ? g_FrameData.pLightListDataBuffer.lock() : nullptr;
	if (!pLightGridBuffer || !pLightListDataBuffer)
	{
		context.ClearBuffer(m_pFallbackLightGridBuffer, 0);
		context.ClearBuffer(m_pFallbackLightListDataBuffer, 0);
		context.TransitionBufferToRead(m_pFallbackLightGridBuffer, ePipelineStage::ComputeShader);
		context.TransitionBufferToRead(m_pFallbackLightListDataBuffer, ePipelineStage::ComputeShader);

		pLightGridBuffer     = m_pFallbackLightGridBuffer;
		pLightListDataBuffer = m_pFallbackLightListDataBuffer;
	}

	context.StageDescriptor("g_LightGridBuffer", pLightGridBuffer);
	context.StageDescriptor("g_LightListDataBuffer", pLightListDataBuffer);

	context.Dispatch2D< 16, 16 >(m_pSceneTexture->Width(), m_pSceneTexture->Height());

	g_FrameData.pColor = m_pSceneTexture;
}

void LightingNode::Resize(u32 width, u32 height, u32 depth)
{
	if (m_pSceneTexture)
		m_pSceneTexture->Resize(width, height, depth);
}


} // namespace baamboo
