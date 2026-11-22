#include "BaambooPch.h"
#include "SkyboxNode.h"
#include "RenderCommon/RenderDevice.h"
#include "RenderCommon/CommandContext.h"
#include "BaambooScene/Scene.h"
#include "PipelineFactory.h"

namespace baamboo
{

static constexpr uint3 kSkyboxLutResolution = { 1024, 1024, 1 };

//-------------------------------------------------------------------------
// Static Skybox
//-------------------------------------------------------------------------
StaticSkyboxNode::StaticSkyboxNode(render::RenderDevice& rd, shader::PipelineFactory& pipelineFactory)
	: Super(rd, "Skybox")
{
	using namespace render;

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

	m_pBakeSkyboxPSO = pipelineFactory.CreateAndBuildComputePipeline("BakeSkyboxPSO", { .source = "StaticSkyboxCS.slang" });
}

StaticSkyboxNode::~StaticSkyboxNode()
{
}

void StaticSkyboxNode::Apply(render::CommandContext& context, const SceneRenderView& renderView)
{
	using namespace render;

	const bool bPathChanged = m_SkyboxPathCache != renderView.atmosphere.skybox && !renderView.atmosphere.skybox.empty();
	if (bPathChanged)
	{
		m_SkyboxPathCache = renderView.atmosphere.skybox;

		auto& rm = m_RenderDevice.GetResourceManager();
		m_pSkyboxTexture = rm.LoadTexture(m_SkyboxPathCache, false, eTextureColorSpace::SRGB);
		assert(m_pSkyboxTexture);
	}

	if (m_pSkyboxTexture && (bPathChanged || m_BakedBuildCount != m_pBakeSkyboxPSO->BuildCount()))
	{
		context.SetRenderPipeline(m_pBakeSkyboxPSO.get());

		context.TransitionBarrier(m_pSkyboxTexture, eTextureLayout::ShaderReadOnly);
		context.TransitionBarrier(m_pSkyboxLUT, eTextureLayout::General);

		context.StageDescriptor("ID_SkyboxTexture", m_pSkyboxTexture, g_FrameData.pLinearClamp);
		context.StageDescriptor("ID_OutSkyboxLUT", m_pSkyboxLUT);

		context.Dispatch3D< 8, 8, 6 >(kSkyboxLutResolution.x, kSkyboxLutResolution.y, kSkyboxLutResolution.z);
		m_BakedBuildCount = m_pBakeSkyboxPSO->BuildCount();
	}

	g_FrameData.pSkyboxLUT = m_pSkyboxLUT;
}

} // namespace baamboo
