#include "BaambooPch.h"
#include "LightingNode.h"
#include "RenderCommon/RenderDevice.h"
#include "RenderCommon/CommandContext.h"
#include "BaambooScene/Scene.h"

namespace baamboo
{

LightingNode::LightingNode(render::RenderDevice& rd)
	: Super(rd, "LightingPass")
{
	using namespace render;

	m_pSceneTexture =
		Texture::Create(
			m_RenderDevice,
			"LightingPass::Out",
			{
				.resolution = { m_RenderDevice.WindowWidth(), m_RenderDevice.WindowHeight(), 1 },
				.format     = eFormat::RGBA16_FLOAT,
				.imageUsage = eTextureUsage_Sample | eTextureUsage_Storage | eTextureUsage_TransferSource | eTextureUsage_ColorAttachment
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

	assert(
		g_FrameData.pGBuffer0 &&
		g_FrameData.pGBuffer1 &&
		g_FrameData.pGBuffer2 &&
		g_FrameData.pGBuffer3 &&
		g_FrameData.pDepth
	);
	context.TransitionBarrier(g_FrameData.pGBuffer0.lock(), eTextureLayout::ShaderReadOnly);
	context.TransitionBarrier(g_FrameData.pGBuffer1.lock(), eTextureLayout::ShaderReadOnly);
	context.TransitionBarrier(g_FrameData.pGBuffer2.lock(), eTextureLayout::ShaderReadOnly);
	context.TransitionBarrier(g_FrameData.pGBuffer3.lock(), eTextureLayout::ShaderReadOnly);
	context.TransitionBarrier(g_FrameData.pDepth.lock(), eTextureLayout::ShaderReadOnly);
	context.TransitionBarrier(g_FrameData.pAerialPerspectiveLUT ? 
		g_FrameData.pAerialPerspectiveLUT.lock() : rm.GetFlatBlackTexture3D(), eTextureLayout::ShaderReadOnly);
	context.TransitionBarrier(g_FrameData.pCloudScatteringLUT ? 
		g_FrameData.pCloudScatteringLUT.lock() : rm.GetFlatBlackTexture(), eTextureLayout::ShaderReadOnly);
	context.TransitionBarrier(g_FrameData.pSkyboxLUT ? 
		g_FrameData.pSkyboxLUT.lock() : rm.GetFlatBlackTextureCube(), eTextureLayout::ShaderReadOnly);
	context.TransitionBarrier(m_pSceneTexture, eTextureLayout::General);

	context.StageDescriptor("g_GBuffer0", g_FrameData.pGBuffer0.lock(), g_FrameData.pLinearClamp);
	context.StageDescriptor("g_GBuffer1", g_FrameData.pGBuffer1.lock(), g_FrameData.pLinearClamp);
	context.StageDescriptor("g_GBuffer2", g_FrameData.pGBuffer2.lock(), g_FrameData.pLinearClamp);
	context.StageDescriptor("g_GBuffer3", g_FrameData.pGBuffer3.lock(), g_FrameData.pLinearClamp);
	context.StageDescriptor("g_DepthBuffer", g_FrameData.pDepth.lock(), g_FrameData.pPointClamp);
	context.StageDescriptor("g_AerialPerspectiveLUT", g_FrameData.pAerialPerspectiveLUT ? 
		g_FrameData.pAerialPerspectiveLUT.lock() : rm.GetFlatBlackTexture3D(), g_FrameData.pLinearWrap);
	context.StageDescriptor("g_CloudScatteringLUT", g_FrameData.pCloudScatteringLUT ? 
		g_FrameData.pCloudScatteringLUT.lock() : rm.GetFlatBlackTexture(), g_FrameData.pLinearClamp);
	context.StageDescriptor("g_SkyboxLUT", g_FrameData.pSkyboxLUT ?
		g_FrameData.pSkyboxLUT.lock() : rm.GetFlatBlackTextureCube(), g_FrameData.pLinearWrap);
	context.StageDescriptor("g_OutSceneTexture", m_pSceneTexture);

	context.Dispatch2D< 16, 16 >(m_pSceneTexture->Width(), m_pSceneTexture->Height());

	g_FrameData.pColor = m_pSceneTexture;
}

void LightingNode::Resize(u32 width, u32 height, u32 depth)
{
	if (m_pSceneTexture)
		m_pSceneTexture->Resize(width, height, depth);
}

} // namespace baamboo