#include "BaambooPch.h"
#include "SkyboxNode.h"
#include "RenderCommon/RenderDevice.h"
#include "RenderCommon/CommandContext.h"
#include "BaambooScene/Scene.h"

namespace baamboo
{

static constexpr uint3 SKYBOX_LUT_RESOLUTION = { 1024, 1024, 1 };

//-------------------------------------------------------------------------
// Static Skybox
//-------------------------------------------------------------------------
StaticSkyboxNode::StaticSkyboxNode(render::RenderDevice& rd)
	: Super(rd, "Skybox")
{
	using namespace render;

	m_pSkyboxLUT =
		Texture::Create(
			m_RenderDevice,
			"AtmospherePass::SkyboxLUT",
			{
				.imageType   = eImageType::TextureCube,
				.resolution  = SKYBOX_LUT_RESOLUTION,
				.format      = eFormat::RG11B10_UFLOAT,
				.imageUsage  = eTextureUsage_Storage | eTextureUsage_Sample,
				.arrayLayers = 6
			});

	m_pBakeSkyboxPSO = ComputePipeline::Create(m_RenderDevice, "BakeSkyboxPSO");
	m_pBakeSkyboxPSO->SetComputeShader(
		Shader::Create(m_RenderDevice, "BakeSkyboxCS",
			{
				.stage    = eShaderStage::Compute,
				.filename = "StaticSkyboxCS"
			})).Build();
}

StaticSkyboxNode::~StaticSkyboxNode()
{
}

void StaticSkyboxNode::Apply(render::CommandContext& context, const SceneRenderView& renderView)
{
	using namespace render;

	if (m_SkyboxPathCache != renderView.atmosphere.skybox)
	{
		m_SkyboxPathCache = renderView.atmosphere.skybox;

		auto& rm = m_RenderDevice.GetResourceManager();
		m_pSkyboxTexture = rm.LoadTexture(m_SkyboxPathCache);
		assert(m_pSkyboxTexture);

		context.SetRenderPipeline(m_pBakeSkyboxPSO.get());

		context.TransitionBarrier(m_pSkyboxTexture, eTextureLayout::ShaderReadOnly);
		context.TransitionBarrier(m_pSkyboxLUT, eTextureLayout::General);

		context.StageDescriptor("g_SkyViewLUT", m_pSkyboxTexture, g_FrameData.pLinearClamp);
		context.StageDescriptor("g_OutSkyboxLUT", m_pSkyboxLUT);

		context.Dispatch3D< 8, 8, 6 >(SKYBOX_LUT_RESOLUTION.x, SKYBOX_LUT_RESOLUTION.y, SKYBOX_LUT_RESOLUTION.z);
	}

	g_FrameData.pSkyboxLUT = m_pSkyboxLUT;
}


//-------------------------------------------------------------------------
// Dynamic Skybox
//-------------------------------------------------------------------------
DynamicSkyboxNode::DynamicSkyboxNode(render::RenderDevice& rd)
    : Super(rd, "Skybox")
{
    using namespace render;

	m_pSkyboxLUT =
		Texture::Create(
			m_RenderDevice,
			"AtmospherePass::SkyboxLUT",
			{
				.imageType   = eImageType::TextureCube,
				.resolution  = SKYBOX_LUT_RESOLUTION,
				.format      = eFormat::RG11B10_UFLOAT,
				.imageUsage  = eTextureUsage_Storage | eTextureUsage_Sample,
				.arrayLayers = 6
			});

	m_pBakeSkyboxPSO = ComputePipeline::Create(m_RenderDevice, "BakeSkyboxPSO");
	m_pBakeSkyboxPSO->SetComputeShader(
		Shader::Create(m_RenderDevice, "BakeSkyboxCS",
			{
				.stage    = eShaderStage::Compute,
				.filename = "AtmosphereSkyboxCS"
			})).Build();
}

DynamicSkyboxNode::~DynamicSkyboxNode()
{
}

void DynamicSkyboxNode::Apply(render::CommandContext& context, const SceneRenderView& renderView)
{
    using namespace render;

	context.SetRenderPipeline(m_pBakeSkyboxPSO.get());

	assert(g_FrameData.pSkyViewLUT);
	context.TransitionBarrier(g_FrameData.pSkyViewLUT.lock(), eTextureLayout::ShaderReadOnly);
	context.TransitionBarrier(m_pSkyboxLUT, eTextureLayout::General);

	context.StageDescriptor("g_SkyViewLUT", g_FrameData.pSkyViewLUT.lock(), g_FrameData.pLinearClamp);
	context.StageDescriptor("g_OutSkyboxLUT", m_pSkyboxLUT);

	context.Dispatch3D< 8, 8, 6 >(SKYBOX_LUT_RESOLUTION.x, SKYBOX_LUT_RESOLUTION.y, SKYBOX_LUT_RESOLUTION.z);

	g_FrameData.pSkyboxLUT = m_pSkyboxLUT;
}

} // namespace baamboo
