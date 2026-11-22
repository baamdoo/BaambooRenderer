#include "BaambooPch.h"
#include "SurfaceResolveNode.h"

#include "ShaderShared/VoxelTerrain.h"
#include "RenderCommon/RenderDevice.h"
#include "RenderCommon/SceneResource.h"
#include "RenderCommon/CommandContext.h"
#include "RenderCommon/CpuProfiler.h"
#include "BaambooScene/Scene.h"
#include "PipelineFactory.h"

namespace baamboo
{


SurfaceResolveNode::SurfaceResolveNode(render::RenderDevice& rd, shader::PipelineFactory& pipelineFactory)
	: Super(rd, "SurfaceResolvePass")
{
	using namespace render;

	m_pCoreNormal = Texture::Create(rd, "SurfaceResolvePass::CoreNormal",
		{
			.resolution = { rd.WindowWidth(), rd.WindowHeight(), 1 },
			.format     = eFormat::RG16_SNORM,
			.imageUsage = eTextureUsage_Sample | eTextureUsage_Storage,
		});

	m_pCoreMaterial = Texture::Create(rd, "SurfaceResolvePass::CoreMaterial",
		{
			.resolution = { rd.WindowWidth(), rd.WindowHeight(), 1 },
			.format     = eFormat::RGBA8_UNORM,
			.imageUsage = eTextureUsage_Sample | eTextureUsage_Storage,
		});

	m_pVelocity = Texture::Create(rd, "SurfaceResolvePass::Velocity",
		{
			.resolution = { rd.WindowWidth(), rd.WindowHeight(), 1 },
			.format     = eFormat::RG16_FLOAT,
			.imageUsage = eTextureUsage_Sample | eTextureUsage_Storage,
		});

	m_pResolvePSO = pipelineFactory.CreateAndBuildComputePipeline("SurfaceResolvePSO", { .source = "SurfaceResolveCS.slang" });

	auto MakeFallback = [&rd](const char* name, u64 elemSize) -> Arc< Buffer >
	{
		return Buffer::Create(rd, name,
			{
				.count              = 1,
				.elementSizeInBytes = elemSize,
				.bufferUsage        = eBufferUsage_Storage,
			});
	};
	m_pVoxelVertexFallback          = MakeFallback("SurfaceResolvePass::VoxelVertexFallback", sizeof(VoxelVertex));
	m_pVoxelMeshletFallback         = MakeFallback("SurfaceResolvePass::VoxelMeshletFallback", sizeof(Meshlet));
	m_pVoxelMeshletVertexFallback   = MakeFallback("SurfaceResolvePass::VoxelMeshletVertexFallback", sizeof(u32));
	m_pVoxelMeshletTriangleFallback = MakeFallback("SurfaceResolvePass::VoxelMeshletTriangleFallback", sizeof(u32));
	m_pVoxelChunkDescFallback       = MakeFallback("SurfaceResolvePass::VoxelChunkDescFallback", sizeof(VoxelChunkDesc));

	m_pErosionDetailFallback = Texture::Create(rd, "SurfaceResolvePass::ErosionDetailFallback",
		{
			.imageType  = eImageType::Texture2DArray, // must match the Texture2DArray shader declaration
			.resolution = uint3(1, 1, 1),
			.format     = eFormat::RGBA16_FLOAT,
			.imageUsage = eTextureUsage_Sample,
		});
}

void SurfaceResolveNode::Apply(render::CommandContext& context, const SceneRenderView& renderView)
{
	UNUSED(renderView);
	using namespace render;

	if ((g_FrameData.surfaceRequirements & (SURFACE_REQ_NORMAL_ROUGHNESS | SURFACE_REQ_VELOCITY)) == 0u)
		return;

	auto pVBuf0 = g_FrameData.pVBuf0.lock();
	auto pVBuf1 = g_FrameData.pVBuf1.lock();
	auto pDepth = g_FrameData.pDepth.lock();
	if (!pVBuf0 || !pVBuf1 || !pDepth)
		return;

	auto pVoxVerts    = g_FrameData.pVoxelVertices.lock();
	auto pVoxMeshlets = g_FrameData.pVoxelMeshlets.lock();
	auto pVoxMv       = g_FrameData.pVoxelMeshletVertices.lock();
	auto pVoxMt       = g_FrameData.pVoxelMeshletTriangles.lock();
	auto pVoxDescs    = g_FrameData.pVoxelChunkDescs.lock();
	auto pErosion     = g_FrameData.pVoxelErosionDetail.lock(); // inputs for re-running the mesh-shader micro displacement

	context.SetRenderPipeline(m_pResolvePSO.get());

	context.TransitionBarrier(pVBuf0, eTextureLayout::ShaderReadOnly);
	context.TransitionBarrier(pVBuf1, eTextureLayout::ShaderReadOnly);
	context.TransitionBarrier(m_pCoreNormal, eTextureLayout::General);
	context.TransitionBarrier(m_pCoreMaterial, eTextureLayout::General);
	context.TransitionBarrier(m_pVelocity, eTextureLayout::General);

	if (pVoxVerts)    context.TransitionBufferToRead(pVoxVerts,    ePipelineStage::ComputeShader);
	if (pVoxMeshlets) context.TransitionBufferToRead(pVoxMeshlets, ePipelineStage::ComputeShader);
	if (pVoxMv)       context.TransitionBufferToRead(pVoxMv,       ePipelineStage::ComputeShader);
	if (pVoxMt)       context.TransitionBufferToRead(pVoxMt,       ePipelineStage::ComputeShader);
	if (pVoxDescs)    context.TransitionBufferToRead(pVoxDescs,    ePipelineStage::ComputeShader);

	if (!pErosion && !m_bErosionFallbackTransitioned)
	{
		context.TransitionBarrier(m_pErosionDetailFallback, eTextureLayout::ShaderReadOnly);
		m_bErosionFallbackTransitioned = true;
	}

	context.StageDescriptor("ID_Scene", m_RenderDevice.GetResourceManager().GetSceneResource().GetSceneGlobals());
	context.StageDescriptor("ID_VBuf0", pVBuf0, g_FrameData.pPointClampNearest);
	context.StageDescriptor("ID_VBuf1", pVBuf1, g_FrameData.pPointClampNearest);
	context.StageDescriptor("ID_CoreNormal", m_pCoreNormal);
	context.StageDescriptor("ID_CoreMaterial", m_pCoreMaterial);
	context.StageDescriptor("ID_Velocity", m_pVelocity);

	context.StageDescriptor("ID_VoxelChunkDescs",       pVoxDescs    ? pVoxDescs    : m_pVoxelChunkDescFallback);
	context.StageDescriptor("ID_VoxelVertices",         pVoxVerts    ? pVoxVerts    : m_pVoxelVertexFallback);
	context.StageDescriptor("ID_VoxelMeshlets",         pVoxMeshlets ? pVoxMeshlets : m_pVoxelMeshletFallback);
	context.StageDescriptor("ID_VoxelMeshletVertices",  pVoxMv       ? pVoxMv       : m_pVoxelMeshletVertexFallback);
	context.StageDescriptor("ID_VoxelMeshletTriangles", pVoxMt       ? pVoxMt       : m_pVoxelMeshletTriangleFallback);
	context.StageDescriptor("ID_ErosionDetailMap",      pErosion     ? pErosion     : m_pErosionDetailFallback, g_FrameData.pLinearClamp);

	context.Dispatch2D< 16, 16 >(m_pCoreNormal->Width(), m_pCoreNormal->Height());

	context.TransitionBarrier(m_pCoreNormal, eTextureLayout::ShaderReadOnly);
	context.TransitionBarrier(m_pCoreMaterial, eTextureLayout::ShaderReadOnly);
	context.TransitionBarrier(m_pVelocity, eTextureLayout::ShaderReadOnly);

	g_FrameData.pCoreNormal   = m_pCoreNormal;
	g_FrameData.pCoreMaterial = m_pCoreMaterial;
	g_FrameData.pVelocity     = m_pVelocity;
}

void SurfaceResolveNode::Resize(u32 width, u32 height, u32 depth)
{
	if (m_pCoreNormal)
		m_pCoreNormal->Resize(width, height, depth);
	if (m_pCoreMaterial)
		m_pCoreMaterial->Resize(width, height, depth);
	if (m_pVelocity)
		m_pVelocity->Resize(width, height, depth);
}


} // namespace baamboo
