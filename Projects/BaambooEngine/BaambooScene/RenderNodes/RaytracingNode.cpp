#include "BaambooPch.h"
#include "RaytracingNode.h"

#include "RenderCommon/RenderDevice.h"
#include "RenderCommon/CommandContext.h"

#include "BaambooScene/Scene.h"

namespace baamboo
{

RaytracingTestNode::RaytracingTestNode(render::RenderDevice& rd)
	: Super(rd, "RaytracingPass_Test")
{
	using namespace render;

	m_pOutputTexture =
		Texture::Create(
			m_RenderDevice,
			"RayTracingTest::Output",
			{
				.resolution = { m_RenderDevice.WindowWidth(), m_RenderDevice.WindowHeight(), 1 },
				.format     = eFormat::RGBA8_UNORM,
				.imageUsage = eTextureUsage_Sample | eTextureUsage_Storage | eTextureUsage_TransferSource
			});

	// 式式 Raytracing Pipeline (RTPSO) 式式
	m_pRaytracingPSO = RaytracingPipeline::Create(m_RenderDevice, "RayTracingTestPSO");
	m_pRaytracingPSO->SetShaderLibrary(
			Shader::Create(m_RenderDevice, "RaytracingTestLib",
				{
					.stage    = eShaderStage::RayGeneration,
					.filename = "RaytracingTestLib"
				}))
		   .SetRayGenerationShader("RayGen")
		   .AddMissShader("Miss")
		   .AddHitGroup(
		       {
		           .hitGroupName           = "HitGroup",
		           .closestHitShaderExport = "ClosestHit"
		       })
		   .SetMaxPayloadSize(sizeof(float) * 5)
		   .SetMaxAttributeSize(sizeof(float) * 2)
		   .SetMaxRecursionDepth(1)
		   .Build();

	// 式式 Shader Binding Table 式式
	m_pSBT = ShaderBindingTable::Create(m_RenderDevice, "RayTracingTestSBT");
	m_pSBT->SetRayGenerationRecord(m_pRaytracingPSO->GetShaderIdentifier("RayGen"), nullptr, 0)
		   .AddMissRecord("Miss", m_pRaytracingPSO->GetShaderIdentifier("Miss"))
		   .AddHitGroupRecord("HitGroup", m_pRaytracingPSO->GetShaderIdentifier("HitGroup"))
		   .Build();
}

void RaytracingTestNode::Apply(render::CommandContext& context, const SceneRenderView& renderView)
{
	UNUSED(renderView);

	using namespace render;

	auto& rm = m_RenderDevice.GetResourceManager();

	auto pTLAS = rm.GetSceneResource().GetTLAS();
	if (!pTLAS || !pTLAS->IsBuilt())
	{
		g_FrameData.pColor = rm.GetFlatBlackTexture();
		return;
	}

	context.SetRenderPipeline(m_pRaytracingPSO.get());

	context.TransitionBarrier(m_pOutputTexture, eTextureLayout::General);

	context.SetAccelerationStructure("g_Scene", *pTLAS);
	context.StageDescriptor("g_Output", m_pOutputTexture);

	context.DispatchRays(*m_pSBT, m_pOutputTexture->Width(), m_pOutputTexture->Height());

	g_FrameData.pColor = m_pOutputTexture;
}

void RaytracingTestNode::Resize(u32 width, u32 height, u32 depth)
{
	UNUSED(depth);

	if (m_pOutputTexture)
		m_pOutputTexture->Resize(width, height, 1);
}

}