#pragma once
#include "Dx12RenderModule.h"

namespace dx12
{

class PostProcessModule : public RenderModule
{
using Super = RenderModule;
public:
	PostProcessModule(RenderDevice& device);
	virtual ~PostProcessModule();

	virtual void Apply(CommandContext& context, const SceneRenderView& renderView) override;
	virtual void Resize(u32 width, u32 height, u32 depth = 1) override;

private:
	virtual void ApplyHeightFog(CommandContext& context, const SceneRenderView& renderView);
	virtual void ApplyBloom(CommandContext& context, const SceneRenderView& renderView);
	virtual void ApplyAntiAliasing(CommandContext& context, const SceneRenderView& renderView);
	virtual void ApplyToneMapping(CommandContext& context, const SceneRenderView& renderView);

private:
	struct
	{
		Arc< Texture > pHistoryTexture;
		Arc< Texture > pAntiAliasedTexture;

		Box< RootSignature >   pTemporalAntiAliasingRS;
		Box< RootSignature >   pSharpenRS;

		Box< ComputePipeline > pTemporalAntiAliasingPSO;
		Box< ComputePipeline > pSharpenPSO;

		u64 applyCounter;
	} m_TAA;

	struct
	{
		Arc< Texture >         pResolvedTexture;
		Box< RootSignature >   pToneMappingRS;
		Box< ComputePipeline > pToneMappingPSO;
	} m_ToneMapping;
};

} // namespace dx12