#include "BaambooPch.h"
#include "SurfaceResolveNode.h"

#include "RenderCommon/RenderDevice.h"
#include "RenderCommon/CommandContext.h"
#include "RenderCommon/CpuProfiler.h"
#include "BaambooScene/Scene.h"
#include "BaambooScene/Terrain/TerrainPatch.h"

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

	m_pNullPatches = Buffer::Create(rd, "SurfaceResolvePass::NullPatches",
		{
			.count              = 1,
			.elementSizeInBytes = sizeof(PatchInstance),
			.bufferUsage        = eBufferUsage_Storage,
		});

	m_pResolvePSO = ComputePipeline::Create(rd, "SurfaceResolvePSO");
	m_pResolvePSO->SetComputeShader(
		Shader::Create(rd, "SurfaceResolveCS", { .stage = eShaderStage::Compute, .filename = "SurfaceResolveCS" })
	).Build();
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

	auto pHeightmap = g_FrameData.pHeightmap.lock();
	if (!pHeightmap)
		pHeightmap = m_RenderDevice.GetResourceManager().GetFlatBlackTexture();

	auto pPatches = g_FrameData.pTerrainPatches.lock();
	if (!pPatches)
		pPatches = m_pNullPatches;

	context.SetRenderPipeline(m_pResolvePSO.get());

	context.TransitionBarrier(pVBuf0, eTextureLayout::ShaderReadOnly);
	context.TransitionBarrier(pVBuf1, eTextureLayout::ShaderReadOnly);
	context.TransitionBarrier(pDepth, eTextureLayout::ShaderReadOnly);
	context.TransitionBarrier(pHeightmap, eTextureLayout::ShaderReadOnly);
	context.TransitionBufferToRead(pPatches, ePipelineStage::ComputeShader);
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

	const auto& tp = g_FrameData.pGetTerrainParams ? g_FrameData.pGetTerrainParams() : TerrainParams{};
	context.SetComputeDynamicUniformBuffer("g_Terrain", sizeof(TerrainParams), &tp);

	context.StageDescriptor("g_VBuf0", pVBuf0, g_FrameData.pPointClampNearest);
	context.StageDescriptor("g_VBuf1", pVBuf1, g_FrameData.pPointClampNearest);
	context.StageDescriptor("g_DepthBuffer", pDepth, g_FrameData.pPointClamp);
	context.StageDescriptor("g_Heightmap", pHeightmap, g_FrameData.pLinearClamp);
	context.StageDescriptor("g_PatchInstances", pPatches);
	context.StageDescriptor("g_CoreNormal", m_pCoreNormal);
	context.StageDescriptor("g_CoreMaterial", m_pCoreMaterial);

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
