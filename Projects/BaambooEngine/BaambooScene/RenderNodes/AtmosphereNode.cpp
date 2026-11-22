#include "BaambooPch.h"
#include "AtmosphereNode.h"
#include "RenderCommon/RenderDevice.h"
#include "RenderCommon/SceneResource.h"
#include "RenderCommon/CommandContext.h"
#include "BaambooScene/Scene.h"
#include "PipelineFactory.h"

namespace baamboo
{

static constexpr uint3 kTransmittanceLutResolution     = { 256, 64, 1 };
static constexpr uint3 kMultiScatteringLutResolution   = { 32, 32, 1 };
static constexpr uint3 kSkyViewLutResolution           = { 192, 104 , 1 };
static constexpr uint3 kAerialPerspectiveLutResolution = { 32, 32, 32 };
static constexpr uint3 kAtmosphereAmbientLutResolution = { 64, 1, 1 };
static constexpr uint3 kSkyboxLutResolution            = { 1024, 1024, 1 };

AtmosphereNode::AtmosphereNode(render::RenderDevice& rd, shader::PipelineFactory& pipelineFactory)
	: Super(rd, "AtmospherePass")
{
	using namespace render;

	m_pTransmittanceLUT =
		Texture::Create(
			m_RenderDevice,
			"AtmospherePass::TransmittanceLUT",
			{
				.resolution = kTransmittanceLutResolution,
				.format     = eFormat::RG11B10_UFLOAT,
				.imageUsage = eTextureUsage_Storage | eTextureUsage_Sample
			});
	m_pMultiScatteringLUT =
		Texture::Create(
			m_RenderDevice,
			"AtmospherePass::MultiScatteringLUT",
			{
				.resolution = kMultiScatteringLutResolution,
				.format     = eFormat::RG11B10_UFLOAT,
				.imageUsage = eTextureUsage_Storage | eTextureUsage_Sample
			});
	m_pSkyViewLUT =
		Texture::Create(
			m_RenderDevice,
			"AtmospherePass::SkyViewLUT",
			{
				.resolution = kSkyViewLutResolution,
				.format     = eFormat::RG11B10_UFLOAT,
				.imageUsage = eTextureUsage_Storage | eTextureUsage_Sample
			});
	m_pAerialPerspectiveLUT =
		Texture::Create(
			m_RenderDevice,
			"AtmospherePass::AerialPerspectiveLUT",
			{
				.imageType  = eImageType::Texture3D,
				.resolution = kAerialPerspectiveLutResolution,
				.format     = eFormat::RGBA16_FLOAT,
				.imageUsage = eTextureUsage_Storage | eTextureUsage_Sample
			});
	m_pAtmosphereAmbientLUT =
		Texture::Create(
			m_RenderDevice,
			"AtmospherePass::AtmosphereAmbientLUT",
			{
				.imageType  = eImageType::Texture1D,
				.resolution = kAtmosphereAmbientLutResolution,
				.format     = eFormat::RG11B10_UFLOAT,
				.imageUsage = eTextureUsage_Storage | eTextureUsage_Sample
			});
	m_pSkyboxLUT =
		Texture::Create(
			m_RenderDevice,
			"AtmospherePass::SkyboxLUT",
			{
				.imageType   = eImageType::TextureCube,
				.resolution  = kSkyboxLutResolution,
				.format      = eFormat::RG11B10_UFLOAT,
				.imageUsage  = eTextureUsage_Storage | eTextureUsage_Sample,
				.arrayLayers = 6
			});

	m_pTransmittancePSO     = pipelineFactory.CreateAndBuildComputePipeline("TransmittancePSO", { .source = "AtmosphereTransmittanceCS.slang" });
	m_pMultiScatteringPSO   = pipelineFactory.CreateAndBuildComputePipeline("MultiScatteringPSO", { .source = "AtmosphereMultiScatteringCS.slang" });
	m_pSkyViewPSO           = pipelineFactory.CreateAndBuildComputePipeline("SkyViewPSO", { .source = "AtmosphereSkyViewCS.slang" });
	m_pAerialPerspectivePSO = pipelineFactory.CreateAndBuildComputePipeline("AerialPerspectivePSO", { .source = "AtmosphereAerialPerspectiveCS.slang" });
	m_pDistantSkyLightPSO   = pipelineFactory.CreateAndBuildComputePipeline("DistantSkyLightPSO", { .source = "AtmosphereDistantSkyLightCS.slang" });
	m_pBakeSkyboxPSO        = pipelineFactory.CreateAndBuildComputePipeline("BakeSkyboxPSO", { .source = "AtmosphereSkyboxCS.slang" });
}

void AtmosphereNode::Apply(render::CommandContext& context, const SceneRenderView& renderView)
{
	using namespace render;
	const Arc< render::Buffer >& pSceneGlobals = m_RenderDevice.GetResourceManager().GetSceneResource().GetSceneGlobals();

	const AtmosphereRaymarchConstants raymarchConstants =
	{
		.msIsoSampleCount = renderView.atmosphere.msIsoSampleCount,
		.msNumRaySteps    = renderView.atmosphere.msNumRaySteps,
		.svMinRaySteps    = renderView.atmosphere.svMinRaySteps,
		.svMaxRaySteps    = renderView.atmosphere.svMaxRaySteps,
	};

	const u64 atmosphereRevision = renderView.componentRevisions[eComponentType::CAtmosphere];
	const bool atmosphereDirty = atmosphereRevision != m_LastAtmosphereRevision;
	const u32 staticLutBuildCount = m_pTransmittancePSO->BuildCount() + m_pMultiScatteringPSO->BuildCount();
	if (m_StaticLutBuildCount != staticLutBuildCount || atmosphereDirty)
	{
		context.SetRenderPipeline(m_pTransmittancePSO.get());

		context.TransitionBarrier(m_pTransmittanceLUT, eTextureLayout::General);

		context.StageDescriptor("ID_Scene", pSceneGlobals);
		context.StageDescriptor("ID_OutTransmittanceLUT", m_pTransmittanceLUT);

		context.Dispatch2D< 8, 8 >(kTransmittanceLutResolution.x, kTransmittanceLutResolution.y);

		//
		context.SetRenderPipeline(m_pMultiScatteringPSO.get());

		context.TransitionBarrier(m_pTransmittanceLUT, eTextureLayout::ShaderReadOnly, 0xFFFFFFFF, true);
		context.TransitionBarrier(m_pMultiScatteringLUT, eTextureLayout::General, 0xFFFFFFFF, true);

		context.SetComputeConstants(sizeof(raymarchConstants), &raymarchConstants);
		context.StageDescriptor("ID_Scene", pSceneGlobals);
		context.StageDescriptor("ID_TransmittanceLUT", m_pTransmittanceLUT, g_FrameData.pLinearClamp);
		context.StageDescriptor("ID_OutMultiScatteringLUT", m_pMultiScatteringLUT);

		context.Dispatch2D< 8, 8 >(kMultiScatteringLutResolution.x, kMultiScatteringLutResolution.y);
		m_LastAtmosphereRevision = atmosphereRevision;
		m_StaticLutBuildCount    = staticLutBuildCount;
	}

	context.SetRenderPipeline(m_pSkyViewPSO.get());

	context.TransitionBarrier(m_pTransmittanceLUT, eTextureLayout::ShaderReadOnly);
	context.TransitionBarrier(m_pMultiScatteringLUT, eTextureLayout::ShaderReadOnly);
	context.TransitionBarrier(m_pSkyViewLUT, eTextureLayout::General);

	context.SetComputeConstants(sizeof(raymarchConstants), &raymarchConstants);
	context.StageDescriptor("ID_Scene", pSceneGlobals);
	context.StageDescriptor("ID_TransmittanceLUT", m_pTransmittanceLUT, g_FrameData.pLinearClamp);
	context.StageDescriptor("ID_MultiScatteringLUT", m_pMultiScatteringLUT, g_FrameData.pLinearClamp);
	context.StageDescriptor("ID_OutSkyViewLUT", m_pSkyViewLUT);

	context.Dispatch2D< 8, 8 >(kSkyViewLutResolution.x, kSkyViewLutResolution.y);

	//
	context.SetRenderPipeline(m_pAerialPerspectivePSO.get());

	context.TransitionBarrier(m_pAerialPerspectiveLUT, eTextureLayout::General);

	context.StageDescriptor("ID_Scene", pSceneGlobals);
	context.StageDescriptor("ID_TransmittanceLUT", m_pTransmittanceLUT, g_FrameData.pLinearClamp);
	context.StageDescriptor("ID_MultiScatteringLUT", m_pMultiScatteringLUT, g_FrameData.pLinearClamp);
	context.StageDescriptor("ID_OutAerialPerspectiveLUT", m_pAerialPerspectiveLUT);

	context.Dispatch3D< 4, 4, 4 >(kAerialPerspectiveLutResolution.x, kAerialPerspectiveLutResolution.y, kAerialPerspectiveLutResolution.z);

	//
	context.SetRenderPipeline(m_pDistantSkyLightPSO.get());

	context.TransitionBarrier(m_pAtmosphereAmbientLUT, eTextureLayout::General);

	context.SetComputeConstants(sizeof(raymarchConstants), &raymarchConstants);
	context.StageDescriptor("ID_Scene", pSceneGlobals);
	context.StageDescriptor("ID_TransmittanceLUT", m_pTransmittanceLUT, g_FrameData.pLinearClamp);
	context.StageDescriptor("ID_MultiScatteringLUT", m_pMultiScatteringLUT, g_FrameData.pLinearClamp);
	context.StageDescriptor("ID_OutAtmosphereAmbientLUT", m_pAtmosphereAmbientLUT);

	context.Dispatch1D< 64 >(kAtmosphereAmbientLutResolution.x);

	//
	context.SetRenderPipeline(m_pBakeSkyboxPSO.get());

	context.TransitionBarrier(m_pSkyViewLUT, eTextureLayout::ShaderReadOnly);
	context.TransitionBarrier(m_pSkyboxLUT, eTextureLayout::General);

	context.StageDescriptor("ID_Scene", pSceneGlobals);
	context.StageDescriptor("ID_TransmittanceLUT", m_pTransmittanceLUT, g_FrameData.pLinearClamp);
	context.StageDescriptor("ID_SkyViewLUT", m_pSkyViewLUT, g_FrameData.pLinearClamp);
	context.StageDescriptor("ID_OutSkyboxLUT", m_pSkyboxLUT);

	context.Dispatch3D< 8, 8, 6 >(kSkyboxLutResolution.x, kSkyboxLutResolution.y, kSkyboxLutResolution.z);

	g_FrameData.pTransmittanceLUT     = m_pTransmittanceLUT;
	g_FrameData.pMultiScatteringLUT   = m_pMultiScatteringLUT;
	g_FrameData.pSkyViewLUT           = m_pSkyViewLUT;
	g_FrameData.pAerialPerspectiveLUT = m_pAerialPerspectiveLUT;
	g_FrameData.pAtmosphereAmbientLUT = m_pAtmosphereAmbientLUT;
	g_FrameData.pSkyboxLUT            = m_pSkyboxLUT;
}

} // namespace baamboo