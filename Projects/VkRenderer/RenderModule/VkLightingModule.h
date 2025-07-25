#pragma once
#include "VkRenderModule.h"

namespace vk
{

class LightingModule : public RenderModule
{
using Super = RenderModule;
public:
	LightingModule(RenderDevice& device);
	virtual ~LightingModule();

	virtual void Apply(CommandContext& context) override;
	virtual void Resize(u32 width, u32 height, u32 depth = 1) override;

private:
	Arc< Texture >   m_pOutTexture;
	Arc< Sampler >   m_pLinearClampSampler;
	Arc< Sampler >   m_pLinearRepeatSampler;
	ComputePipeline* m_pLightingPSO = nullptr;
};

} // namespace vk