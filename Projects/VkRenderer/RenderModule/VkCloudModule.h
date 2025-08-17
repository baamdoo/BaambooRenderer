#pragma once
#include "VkRenderModule.h"

namespace vk
{

//-------------------------------------------------------------------------
// Cloud Shape
//-------------------------------------------------------------------------
class CloudShapeModule : public RenderModule
{
using Super = RenderModule;
public:
	CloudShapeModule(RenderDevice& device);
	virtual ~CloudShapeModule();

	virtual void Apply(CommandContext& context, const SceneRenderView& renderView) override;

private:
	Arc< Texture > m_pBaseNoiseTexture;
	Arc< Texture > m_pDetailNoiseTexture;
	Arc< Texture > m_pWeatherMapTexture;

	Box< ComputePipeline > m_pCloudShapeBasePSO;
	Box< ComputePipeline > m_pCloudShapeDetailPSO;
	Box< ComputePipeline > m_pWeatherMapPSO;
};


//-------------------------------------------------------------------------
// Cloud Scattering
//-------------------------------------------------------------------------
class CloudScatteringModule : public RenderModule
{
	using Super = RenderModule;
public:
	CloudScatteringModule(RenderDevice& device);
	virtual ~CloudScatteringModule();

	virtual void Apply(CommandContext& context, const SceneRenderView& renderView) override;

private:
	Arc< Texture > m_pCloudScatteringLUT;

	Box< ComputePipeline > m_pCloudScatteringPSO;
};

} // namespace vk