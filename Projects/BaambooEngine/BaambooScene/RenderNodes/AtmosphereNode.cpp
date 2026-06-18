#include "BaambooPch.h"
#include "AtmosphereNode.h"
#include "RenderCommon/RenderDevice.h"
#include "RenderCommon/CommandContext.h"
#include "BaambooScene/Scene.h"

namespace baamboo
{

static constexpr uint3 kTransmittanceLutResolution     = { 256, 64, 1 };
static constexpr uint3 kMultiScatteringLutResolution   = { 32, 32, 1 };
static constexpr uint3 kSkyViewLutResolution           = { 192, 104 , 1 };
static constexpr uint3 kAerialPerspectiveLutResolution = { 32, 32, 32 };
static constexpr uint3 kAtmosphereAmbientLutResolution = { 64, 1, 1 };
static constexpr uint3 kSkyboxLutResolution            = { 1024, 1024, 1 };

AtmosphereNode::AtmosphereNode(render::RenderDevice& rd)
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

	m_pTransmittancePSO = ComputePipeline::Create(m_RenderDevice, "TransmittancePSO");
	m_pTransmittancePSO->SetComputeShader(
		Shader::Create(m_RenderDevice, "TransmittanceCS",
			{
				.stage    = eShaderStage::Compute,
				.filename = "AtmosphereTransmittanceCS"
			})).Build();

	m_pMultiScatteringPSO = ComputePipeline::Create(m_RenderDevice, "MultiScatteringPSO");
	m_pMultiScatteringPSO->SetComputeShader(
		Shader::Create(m_RenderDevice, "MultiScatteringCS",
			{
				.stage    = eShaderStage::Compute,
				.filename = "AtmosphereMultiScatteringCS"
			})).Build();

	m_pSkyViewPSO = ComputePipeline::Create(m_RenderDevice, "SkyViewPSO");
	m_pSkyViewPSO->SetComputeShader(
		Shader::Create(m_RenderDevice, "SkyViewCS",
			{
				.stage    = eShaderStage::Compute,
				.filename = "AtmosphereSkyViewCS"
			})).Build();

	m_pAerialPerspectivePSO = ComputePipeline::Create(m_RenderDevice, "AerialPerspectivePSO");
	m_pAerialPerspectivePSO->SetComputeShader(
		Shader::Create(m_RenderDevice, "AerialPerspectiveCS",
			{
				.stage    = eShaderStage::Compute,
				.filename = "AtmosphereAerialPerspectiveCS"
			})).Build();

	m_pDistantSkyLightPSO = ComputePipeline::Create(m_RenderDevice, "DistantSkyLightPSO");
	m_pDistantSkyLightPSO->SetComputeShader(
		Shader::Create(m_RenderDevice, "AtmosphereDistantSkyLightCS",
			{
				.stage    = eShaderStage::Compute,
				.filename = "AtmosphereDistantSkyLightCS"
			})).Build();

	m_pBakeSkyboxPSO = ComputePipeline::Create(m_RenderDevice, "BakeSkyboxPSO");
	m_pBakeSkyboxPSO->SetComputeShader(
		Shader::Create(m_RenderDevice, "BakeAtmosphereSkyboxCS",
			{
				.stage    = eShaderStage::Compute,
				.filename = "AtmosphereSkyboxCS"
			})).Build();
}

void AtmosphereNode::Apply(render::CommandContext& context, const SceneRenderView& renderView)
{
	using namespace render;
	auto& rm = m_RenderDevice.GetResourceManager();

	if (g_FrameData.componentMarker & (1 << eComponentType::CAtmosphere))
	{
		context.SetRenderPipeline(m_pTransmittancePSO.get());

		context.TransitionBarrier(m_pTransmittanceLUT, eTextureLayout::General);

		context.StageDescriptor("g_OutTransmittanceLUT", m_pTransmittanceLUT);
		
		context.Dispatch2D< 8, 8 >(kTransmittanceLutResolution.x, kTransmittanceLutResolution.y);

		//
		context.SetRenderPipeline(m_pMultiScatteringPSO.get());

		context.TransitionBarrier(m_pTransmittanceLUT, eTextureLayout::ShaderReadOnly, 0xFFFFFFFF, true);
		context.TransitionBarrier(m_pMultiScatteringLUT, eTextureLayout::General, 0xFFFFFFFF, true);

		context.SetComputeConstants(sizeof(u32), &renderView.atmosphere.msIsoSampleCount, 0);
		context.SetComputeConstants(sizeof(u32), &renderView.atmosphere.msNumRaySteps, sizeof(u32));
		context.StageDescriptor("g_TransmittanceLUT", m_pTransmittanceLUT, g_FrameData.pLinearClamp);
		context.StageDescriptor("g_OutMultiScatteringLUT", m_pMultiScatteringLUT);
		
		context.Dispatch2D< 8, 8 >(kMultiScatteringLutResolution.x, kMultiScatteringLutResolution.y);
	}

	context.SetRenderPipeline(m_pSkyViewPSO.get());

	context.TransitionBarrier(m_pTransmittanceLUT, eTextureLayout::ShaderReadOnly);
	context.TransitionBarrier(m_pMultiScatteringLUT, eTextureLayout::ShaderReadOnly);
	context.TransitionBarrier(m_pSkyViewLUT, eTextureLayout::General);

	context.SetComputeConstants(sizeof(u32), &renderView.atmosphere.svMinRaySteps, 0);
	context.SetComputeConstants(sizeof(u32), &renderView.atmosphere.svMaxRaySteps, sizeof(u32));
	context.StageDescriptor("g_TransmittanceLUT", m_pTransmittanceLUT, g_FrameData.pLinearClamp);
	context.StageDescriptor("g_MultiScatteringLUT", m_pMultiScatteringLUT, g_FrameData.pLinearClamp);
	context.StageDescriptor("g_OutSkyViewLUT", m_pSkyViewLUT);
	
	context.Dispatch2D< 8, 8 >(kSkyViewLutResolution.x, kSkyViewLutResolution.y);

	//
	context.SetRenderPipeline(m_pAerialPerspectivePSO.get());

	context.TransitionBarrier(m_pAerialPerspectiveLUT, eTextureLayout::General);

	context.StageDescriptor("g_TransmittanceLUT", m_pTransmittanceLUT, g_FrameData.pLinearClamp);
	context.StageDescriptor("g_MultiScatteringLUT", m_pMultiScatteringLUT, g_FrameData.pLinearClamp);
	context.StageDescriptor("g_OutAerialPerspectiveLUT", m_pAerialPerspectiveLUT);

	context.Dispatch3D< 4, 4, 4 >(kAerialPerspectiveLutResolution.x, kAerialPerspectiveLutResolution.y, kAerialPerspectiveLutResolution.z);

	//
	context.SetRenderPipeline(m_pDistantSkyLightPSO.get());

	context.TransitionBarrier(m_pAtmosphereAmbientLUT, eTextureLayout::General);

	struct
	{
		u32 minRaySteps;
		u32 maxRaySteps;
		u32 sampleCount;
	} constant = { renderView.atmosphere.svMinRaySteps, renderView.atmosphere.svMaxRaySteps, renderView.atmosphere.msIsoSampleCount };
	context.SetComputeConstants(sizeof(constant), &constant);
	context.StageDescriptor("g_TransmittanceLUT", m_pTransmittanceLUT, g_FrameData.pLinearClamp);
	context.StageDescriptor("g_MultiScatteringLUT", m_pMultiScatteringLUT, g_FrameData.pLinearClamp);
	context.StageDescriptor("g_OutAtmosphereAmbientLUT", m_pAtmosphereAmbientLUT);

	context.Dispatch1D< 64 >(kAtmosphereAmbientLutResolution.x);

	//
	context.SetRenderPipeline(m_pBakeSkyboxPSO.get());

	context.TransitionBarrier(m_pSkyViewLUT, eTextureLayout::ShaderReadOnly);
	context.TransitionBarrier(m_pSkyboxLUT, eTextureLayout::General);

	context.StageDescriptor("g_SkyViewLUT", m_pSkyViewLUT, g_FrameData.pLinearClamp);
	context.StageDescriptor("g_OutSkyboxLUT", m_pSkyboxLUT);

	context.Dispatch3D< 8, 8, 6 >(kSkyboxLutResolution.x, kSkyboxLutResolution.y, kSkyboxLutResolution.z);

	g_FrameData.pTransmittanceLUT     = m_pTransmittanceLUT;
	g_FrameData.pMultiScatteringLUT   = m_pMultiScatteringLUT;
	g_FrameData.pSkyViewLUT           = m_pSkyViewLUT;
	g_FrameData.pAerialPerspectiveLUT = m_pAerialPerspectiveLUT;
	g_FrameData.pAtmosphereAmbientLUT = m_pAtmosphereAmbientLUT;
	g_FrameData.pSkyboxLUT            = m_pSkyboxLUT;
}

} // namespace baamboo