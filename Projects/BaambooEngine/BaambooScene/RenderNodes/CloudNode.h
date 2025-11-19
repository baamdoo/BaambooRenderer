#pragma once
#include "BaambooScene/Systems/CloudSystem.h"
#include "RenderCommon/RenderNode.h"

enum class eCloudUprezRatio;

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
	Arc< render::Texture > m_pErosionNoiseTexture;
	Arc< render::Texture > m_pDensityTopGradientTexture;
	Arc< render::Texture > m_pDensityBottomGradientTexture;

	Box< render::ComputePipeline > m_pCloudShapeBasePSO;
	Box< render::ComputePipeline > m_pCloudShapeDetailPSO;
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
	virtual void Resize(u32 width, u32 height, u32 depth) override;

private:
	Arc< render::Texture > m_pBaseNoiseTexture;
	Arc< render::Texture > m_pErosionNoiseTexture;
	Arc< render::Texture > m_pDensityTopGradientTexture;
	Arc< render::Texture > m_pDensityBottomGradientTexture;

	Arc< render::Texture > m_pCloudShadowMap;
	Arc< render::Texture > m_pCloudScatteringLUT;
	Arc< render::Texture > m_pUprezzedCloudScatteringLUT;
	Arc< render::Texture > m_pPrevUprezzedCloudScatteringLUT;

	Arc< render::Texture > m_pBlueNoiseTexture;

	Box< render::ComputePipeline > m_pCloudShadowPSO;
	Box< render::ComputePipeline > m_pCloudRaymarchPSO;
	Box< render::ComputePipeline > m_pCloudTemporalUprezPSO;

	eCloudUprezRatio m_CurrentUprezRatio = eCloudUprezRatio::X2;
};

} // namespace baamboo