#pragma once
#include "RenderCommon/RenderNode.h"

namespace baamboo
{

class AtmosphereNode : public render::RenderNode
{
using Super = render::RenderNode;
public:
	AtmosphereNode(render::RenderDevice& rd);
	virtual ~AtmosphereNode() = default;

	virtual void Apply(render::CommandContext& context, const SceneRenderView& renderView) override;

private:
	Arc< render::Texture > m_pTransmittanceLUT;
	Arc< render::Texture > m_pMultiScatteringLUT;
	Arc< render::Texture > m_pSkyViewLUT;
	Arc< render::Texture > m_pAerialPerspectiveLUT;
	Arc< render::Texture > m_pAtmosphereAmbientLUT;

	Box< render::ComputePipeline > m_pTransmittancePSO;
	Box< render::ComputePipeline > m_pMultiScatteringPSO;
	Box< render::ComputePipeline > m_pSkyViewPSO;
	Box< render::ComputePipeline > m_pAerialPerspectivePSO;
	Box< render::ComputePipeline > m_pDistantSkyLightPSO;
};

} // namespace baamboo