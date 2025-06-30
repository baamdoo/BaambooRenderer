#pragma once
#include "Dx12RenderModule.h"

namespace dx12
{

class LightingModule : public RenderModule
{
using Super = RenderModule;
public:
	LightingModule(RenderDevice& device);
	virtual ~LightingModule();

	virtual void Apply(CommandContext& context) override;

private:
	Arc< Texture >   m_pOutTexture;

	RootSignature*   m_pLightingRS  = nullptr;
	ComputePipeline* m_pLightingPSO = nullptr;

	struct
	{
		u32 camera   = INVALID_INDEX;
		u32 light    = INVALID_INDEX;
		u32 textures = INVALID_INDEX;
		u32 material = INVALID_INDEX;
	} m_Indices;
};

}