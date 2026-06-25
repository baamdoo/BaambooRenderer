#include "BaambooPch.h"
#include "SurfaceResolveNode.h"

#include "ShaderTypes.h"
#include "RenderCommon/RenderDevice.h"
#include "RenderCommon/CommandContext.h"
#include "RenderCommon/CpuProfiler.h"
#include "BaambooScene/Scene.h"

namespace baamboo
{


SurfaceResolveNode::SurfaceResolveNode(render::RenderDevice& rd)
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

	m_pResolvePSO = ComputePipeline::Create(rd, "SurfaceResolvePSO");
	m_pResolvePSO->SetComputeShader(
		Shader::Create(rd, "SurfaceResolveCS", { .stage = eShaderStage::Compute, .filename = "SurfaceResolveCS" })
	).Build();

	auto MakeFallback = [&rd](const char* name, u64 elemSize) -> Arc< Buffer >
	{
		return Buffer::Create(rd, name,
			{
				.count              = 1,
				.elementSizeInBytes = elemSize,
				.bufferUsage        = eBufferUsage_Storage,
			});
	};
	m_pVoxelChunksFallback          = MakeFallback("SurfaceResolvePass::VoxelChunksFallback", sizeof(VoxelChunk));
	m_pVoxelVertexFallback          = MakeFallback("SurfaceResolvePass::VoxelVertexFallback", sizeof(::Vertex));
	m_pVoxelMeshletFallback         = MakeFallback("SurfaceResolvePass::VoxelMeshletFallback", sizeof(Meshlet));
	m_pVoxelMeshletVertexFallback   = MakeFallback("SurfaceResolvePass::VoxelMeshletVertexFallback", sizeof(u32));
	m_pVoxelMeshletTriangleFallback = MakeFallback("SurfaceResolvePass::VoxelMeshletTriangleFallback", sizeof(u32));
}

void SurfaceResolveNode::Apply(render::CommandContext& context, const SceneRenderView& renderView)
{
	UNUSED(renderView);
	using namespace render;

	if ((g_FrameData.surfaceRequirements & SURFACE_REQ_NORMAL_ROUGHNESS) == 0u)
		return;

	auto pVBuf0 = g_FrameData.pVBuf0.lock();
	auto pVBuf1 = g_FrameData.pVBuf1.lock();
	auto pDepth = g_FrameData.pDepth.lock();
	if (!pVBuf0 || !pVBuf1 || !pDepth)
		return;

	context.SetRenderPipeline(m_pResolvePSO.get());

	context.TransitionBarrier(pVBuf0, eTextureLayout::ShaderReadOnly);
	context.TransitionBarrier(pVBuf1, eTextureLayout::ShaderReadOnly);
	context.TransitionBarrier(m_pCoreNormal, eTextureLayout::General);
	context.TransitionBarrier(m_pCoreMaterial, eTextureLayout::General);

	struct SurfaceResolvePushConstants
	{
		float viewportWidth;
		float viewportHeight;
	} constants = {
		.viewportWidth  = static_cast<float>(m_RenderDevice.WindowWidth()),
		.viewportHeight = static_cast<float>(m_RenderDevice.WindowHeight()),
	};
	context.SetComputeConstants(sizeof(constants), &constants);

	context.StageDescriptor("g_VBuf0", pVBuf0, g_FrameData.pPointClampNearest);
	context.StageDescriptor("g_VBuf1", pVBuf1, g_FrameData.pPointClampNearest);
	context.StageDescriptor("g_CoreNormal", m_pCoreNormal);
	context.StageDescriptor("g_CoreMaterial", m_pCoreMaterial);

	auto pVoxChunks   = g_FrameData.pVoxelChunks.lock();
	auto pVoxVerts    = g_FrameData.pVoxelVertices.lock();
	auto pVoxMeshlets = g_FrameData.pVoxelMeshlets.lock();
	auto pVoxMv       = g_FrameData.pVoxelMeshletVertices.lock();
	auto pVoxMt       = g_FrameData.pVoxelMeshletTriangles.lock();

	if (pVoxChunks)   context.TransitionBufferToRead(pVoxChunks,   ePipelineStage::ComputeShader);
	if (pVoxVerts)    context.TransitionBufferToRead(pVoxVerts,    ePipelineStage::ComputeShader);
	if (pVoxMeshlets) context.TransitionBufferToRead(pVoxMeshlets, ePipelineStage::ComputeShader);
	if (pVoxMv)       context.TransitionBufferToRead(pVoxMv,       ePipelineStage::ComputeShader);
	if (pVoxMt)       context.TransitionBufferToRead(pVoxMt,       ePipelineStage::ComputeShader);

	context.StageDescriptor("g_VoxelChunks",           pVoxChunks   ? pVoxChunks   : m_pVoxelChunksFallback);
	context.StageDescriptor("g_VoxelVertices",         pVoxVerts    ? pVoxVerts    : m_pVoxelVertexFallback);
	context.StageDescriptor("g_VoxelMeshlets",         pVoxMeshlets ? pVoxMeshlets : m_pVoxelMeshletFallback);
	context.StageDescriptor("g_VoxelMeshletVertices",  pVoxMv       ? pVoxMv       : m_pVoxelMeshletVertexFallback);
	context.StageDescriptor("g_VoxelMeshletTriangles", pVoxMt       ? pVoxMt       : m_pVoxelMeshletTriangleFallback);

	context.Dispatch2D< 16, 16 >(m_pCoreNormal->Width(), m_pCoreNormal->Height());

	context.TransitionBarrier(m_pCoreNormal, eTextureLayout::ShaderReadOnly);
	context.TransitionBarrier(m_pCoreMaterial, eTextureLayout::ShaderReadOnly);

	g_FrameData.pCoreNormal   = m_pCoreNormal;
	g_FrameData.pCoreMaterial = m_pCoreMaterial;
}

void SurfaceResolveNode::Resize(u32 width, u32 height, u32 depth)
{
	if (m_pCoreNormal)
		m_pCoreNormal->Resize(width, height, depth);
	if (m_pCoreMaterial)
		m_pCoreMaterial->Resize(width, height, depth);
}


} // namespace baamboo
