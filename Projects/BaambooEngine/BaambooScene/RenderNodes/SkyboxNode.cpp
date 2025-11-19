#include "BaambooPch.h"
#include "SkyboxNode.h"
#include "RenderCommon/CommandContext.h"
#include "BaambooScene/Scene.h"

namespace baamboo
{

static constexpr uint3 SKYBOX_LUT_RESOLUTION = { 1024, 1024, 1 };

DynamicSkyboxNode::DynamicSkyboxNode(render::RenderDevice& rd)
    : Super(rd, "CloudShapePass")
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
		Shader::Create(m_RenderDevice, "BakeAtmosphereSkyboxCS",
			{
				.stage = eShaderStage::Compute,
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

	struct
	{
		float3 lightDir0;
		//float3 lightDir1;
		float  planetRadius_km;
	} skyboxConstant = { renderView.light.directionals[0].direction, renderView.atmosphere.data.planetRadius_km };
	context.SetComputeConstants(sizeof(skyboxConstant), &skyboxConstant);
	context.SetComputeDynamicUniformBuffer("g_Camera", g_FrameData.camera);
	context.StageDescriptor("g_SkyViewLUT", g_FrameData.pSkyViewLUT.lock(), g_FrameData.pLinearClamp);
	context.StageDescriptor("g_SkyboxLUT", m_pSkyboxLUT);

	context.Dispatch3D< 8, 8, 6 >(SKYBOX_LUT_RESOLUTION.x, SKYBOX_LUT_RESOLUTION.y, SKYBOX_LUT_RESOLUTION.z);

	g_FrameData.pSkyboxLUT = m_pSkyboxLUT;
}

} // namespace baamboo
