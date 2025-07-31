#pragma once
#include "VkRenderModule.h"

namespace vk
{

class AtmosphereModule : public RenderModule
{
using Super = RenderModule;
public:
	AtmosphereModule(RenderDevice& device);
	virtual ~AtmosphereModule();

	virtual void Apply(CommandContext& context, const SceneRenderView& renderView) override;

private:
	Arc< Texture > m_pTransmittanceLUT;
	Arc< Texture > m_pMultiScatteringLUT;
	Arc< Texture > m_pSkyViewLUT;
	Arc< Texture > m_pAerialPerspectiveLUT;

	Box< ComputePipeline > m_pTransmittancePSO;
	Box< ComputePipeline > m_pMultiScatteringPSO;
	Box< ComputePipeline > m_pSkyViewPSO;
	Box< ComputePipeline > m_pAerialPerspectivePSO;
};

} // namespace vk