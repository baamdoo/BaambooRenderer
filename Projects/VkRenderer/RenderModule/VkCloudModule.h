#pragma once
#include "VkRenderModule.h"

namespace vk
{

class CloudModule : public RenderModule
{
using Super = RenderModule;
public:
	CloudModule(RenderDevice& device);
	virtual ~CloudModule();

	virtual void Apply(CommandContext& context, const SceneRenderView& renderView) override;

private:
	Arc< Texture > m_pBaseNoiseTexture;
	Arc< Texture > m_pDetailNoiseTexture;
	Arc< Texture > m_pCurlNoiseTexture;

	Box< ComputePipeline > m_pCloudShapeBasePSO;
	Box< ComputePipeline > m_pCloudShapeDetailPSO;
};

} // namespace vk