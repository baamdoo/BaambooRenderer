#include "BaambooPch.h"
#include "AtmosphereNode.h"
#include "RenderCommon/RenderDevice.h"
#include "RenderCommon/CommandContext.h"
#include "BaambooScene/Scene.h"

namespace baamboo
{

static constexpr uint3 TRANSMITTANCE_LUT_RESOLUTION     = { 256, 64, 1 };
static constexpr uint3 MULTISCATTERING_LUT_RESOLUTION   = { 32, 32, 1 };
static constexpr uint3 SKYVIEW_LUT_RESOLUTION           = { 192, 104 , 1 };
static constexpr uint3 AERIALPERSPECTIVE_LUT_RESOLUTION = { 32, 32 , 32 };

AtmosphereNode::AtmosphereNode(render::RenderDevice& rd)
	: Super(rd, "AtmospherePass")
{
	using namespace render;

	m_pTransmittanceLUT =
		Texture::Create(
			m_RenderDevice,
			"AtmospherePass::TransmittanceLUT",
			{
				.resolution = TRANSMITTANCE_LUT_RESOLUTION,
				.format     = eFormat::RG11B10_UFLOAT,
				.imageUsage = eTextureUsage_Storage | eTextureUsage_Sample
			});
	m_pMultiScatteringLUT =
		Texture::Create(
			m_RenderDevice,
			"AtmospherePass::MultiScatteringLUT",
			{
				.resolution = MULTISCATTERING_LUT_RESOLUTION,
				.format     = eFormat::RG11B10_UFLOAT,
				.imageUsage = eTextureUsage_Storage | eTextureUsage_Sample
			});
	m_pSkyViewLUT =
		Texture::Create(
			m_RenderDevice,
			"AtmospherePass::SkyViewLUT",
			{
				.resolution = SKYVIEW_LUT_RESOLUTION,
				.format     = eFormat::RG11B10_UFLOAT,
				.imageUsage = eTextureUsage_Storage | eTextureUsage_Sample
			});
	m_pAerialPerspectiveLUT =
		Texture::Create(
			m_RenderDevice,
			"AtmospherePass::AerialPerspectiveLUT",
			{
				.type       = eTextureType::Texture3D,
				.resolution = AERIALPERSPECTIVE_LUT_RESOLUTION,
				.format     = eFormat::RGBA16_FLOAT,
				.imageUsage = eTextureUsage_Storage | eTextureUsage_Sample
			});

	m_pTransmittancePSO = ComputePipeline::Create(m_RenderDevice, "TransmittancePSO");
	m_pTransmittancePSO->SetComputeShader(
		Shader::Create(m_RenderDevice, "TransmittanceCS",
			{
				.stage    = eShaderStage::Compute,
				.filename = "AtmosphereTransmittance"
			})).Build();

	m_pMultiScatteringPSO = ComputePipeline::Create(m_RenderDevice, "MultiScatteringPSO");
	m_pMultiScatteringPSO->SetComputeShader(
		Shader::Create(m_RenderDevice, "MultiScatteringCS",
			{
				.stage    = eShaderStage::Compute,
				.filename = "AtmosphereMultiScattering"
			})).Build();

	m_pSkyViewPSO = ComputePipeline::Create(m_RenderDevice, "SkyViewPSO");
	m_pSkyViewPSO->SetComputeShader(
		Shader::Create(m_RenderDevice, "SkyViewCS",
			{
				.stage    = eShaderStage::Compute,
				.filename = "AtmosphereSkyView"
			})).Build();

	m_pAerialPerspectivePSO = ComputePipeline::Create(m_RenderDevice, "AerialPerspectivePSO");
	m_pAerialPerspectivePSO->SetComputeShader(
		Shader::Create(m_RenderDevice, "AerialPerspectiveCS",
			{
				.stage    = eShaderStage::Compute,
				.filename = "AerialPerspective"
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

		context.SetComputeDynamicUniformBuffer("g_Atmosphere", renderView.atmosphere.data);
		context.StageDescriptor("g_TransmittanceLUT", m_pTransmittanceLUT);

		context.Dispatch2D< 8, 8 >(TRANSMITTANCE_LUT_RESOLUTION.x, TRANSMITTANCE_LUT_RESOLUTION.y);

		//
		context.SetRenderPipeline(m_pMultiScatteringPSO.get());

		context.TransitionBarrier(m_pTransmittanceLUT, eTextureLayout::ShaderReadOnly, 0xFFFFFFFF, true);
		context.TransitionBarrier(m_pMultiScatteringLUT, eTextureLayout::General, 0xFFFFFFFF, true);

		context.SetComputeConstants(sizeof(u32), &renderView.atmosphere.msIsoSampleCount, 0);
		context.SetComputeConstants(sizeof(u32), &renderView.atmosphere.msNumRaySteps, sizeof(u32));
		context.SetComputeDynamicUniformBuffer("g_Atmosphere", renderView.atmosphere.data);
		context.StageDescriptor("g_TransmittanceLUT", m_pTransmittanceLUT, g_FrameData.pLinearClamp);
		context.StageDescriptor("g_MultiScatteringLUT", m_pMultiScatteringLUT);

		context.Dispatch2D< 8, 8 >(MULTISCATTERING_LUT_RESOLUTION.x, MULTISCATTERING_LUT_RESOLUTION.y);
	}
	context.TransitionBarrier(m_pTransmittanceLUT, eTextureLayout::ShaderReadOnly, 0xFFFFFFFF, true);
	context.TransitionBarrier(m_pMultiScatteringLUT, eTextureLayout::ShaderReadOnly, 0xFFFFFFFF, true);

	//
	context.SetRenderPipeline(m_pSkyViewPSO.get());

	context.TransitionBarrier(m_pSkyViewLUT, eTextureLayout::General, 0xFFFFFFFF, true);

	context.SetComputeDynamicUniformBuffer("g_Camera", g_FrameData.camera);
	context.SetComputeDynamicUniformBuffer("g_Atmosphere", renderView.atmosphere.data);
	context.SetComputeConstants(sizeof(u32), &renderView.atmosphere.svMinRaySteps, 0);
	context.SetComputeConstants(sizeof(u32), &renderView.atmosphere.svMaxRaySteps, sizeof(u32));
	context.StageDescriptor("g_TransmittanceLUT", m_pTransmittanceLUT, g_FrameData.pLinearClamp);
	context.StageDescriptor("g_MultiScatteringLUT", m_pMultiScatteringLUT, g_FrameData.pLinearClamp);
	context.StageDescriptor("g_SkyViewLUT", m_pSkyViewLUT);

	context.Dispatch2D< 8, 8 >(SKYVIEW_LUT_RESOLUTION.x, SKYVIEW_LUT_RESOLUTION.y);

	//
	context.SetRenderPipeline(m_pAerialPerspectivePSO.get());

	context.TransitionBarrier(m_pAerialPerspectiveLUT, eTextureLayout::General);

	context.SetComputeDynamicUniformBuffer("g_Camera", g_FrameData.camera);
	context.SetComputeDynamicUniformBuffer("g_Atmosphere", renderView.atmosphere.data);
	context.StageDescriptor("g_TransmittanceLUT", m_pTransmittanceLUT, g_FrameData.pLinearClamp);
	context.StageDescriptor("g_MultiScatteringLUT", m_pMultiScatteringLUT, g_FrameData.pLinearClamp);
	context.StageDescriptor("g_AerialPerspectiveLUT", m_pAerialPerspectiveLUT);

	context.Dispatch3D< 4, 4, 4 >(AERIALPERSPECTIVE_LUT_RESOLUTION.x, AERIALPERSPECTIVE_LUT_RESOLUTION.y, AERIALPERSPECTIVE_LUT_RESOLUTION.z);

	g_FrameData.pTransmittanceLUT     = m_pTransmittanceLUT;
	g_FrameData.pMultiScatteringLUT   = m_pMultiScatteringLUT;
	g_FrameData.pSkyViewLUT           = m_pSkyViewLUT;
	g_FrameData.pAerialPerspectiveLUT = m_pAerialPerspectiveLUT;
}

} // namespace baamboo