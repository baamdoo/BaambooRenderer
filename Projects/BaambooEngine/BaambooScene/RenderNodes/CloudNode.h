#pragma once
#include "RenderCommon/RenderNode.h"

namespace baamboo
{

//-------------------------------------------------------------------------
// Cloud Shape
//-------------------------------------------------------------------------
class CloudShapeNode : public render::RenderNode
{
using Super = render::RenderNode;
public:
	CloudShapeNode(render::RenderDevice& rd);
	virtual ~CloudShapeNode();

	virtual void Apply(render::CommandContext& context, const SceneRenderView& renderView) override;

private:
	Arc< render::Texture > m_pBaseNoiseTexture;
	Arc< render::Texture > m_pDetailNoiseTexture;
	Arc< render::Texture > m_pVerticalProfileTexture;
	Arc< render::Texture > m_pWeatherMapTexture;

	Box< render::ComputePipeline > m_pCloudShapeBasePSO;
	Box< render::ComputePipeline > m_pCloudShapeDetailPSO;
	Box< render::ComputePipeline > m_pVerticalProfilePSO;
	Box< render::ComputePipeline > m_pWeatherMapPSO;
};


//-------------------------------------------------------------------------
// Cloud Scattering
//-------------------------------------------------------------------------
class CloudScatteringNode : public render::RenderNode
{
using Super = render::RenderNode;
public:
	CloudScatteringNode(render::RenderDevice& device);
	virtual ~CloudScatteringNode();

	virtual void Apply(render::CommandContext& context, const SceneRenderView& renderView) override;

private:
	Arc< render::Texture > m_pCloudScatteringLUT;
	Arc< render::Texture > m_pCurlNoiseTexture;
	Arc< render::Texture > m_pWeatherMap;
	Arc< render::Texture > m_pBlueNoiseTexture;

	Box< render::ComputePipeline > m_pCloudScatteringPSO;
};

} // namespace baamboo