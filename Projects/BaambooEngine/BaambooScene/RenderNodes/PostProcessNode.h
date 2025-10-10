#pragma once
#include "RenderCommon/RenderNode.h"

namespace baamboo
{

class PostProcessNode : public render::RenderNode
{
using Super = render::RenderNode;
public:
	PostProcessNode(render::RenderDevice& rd);
	virtual ~PostProcessNode() = default;

	virtual void Apply(render::CommandContext& context, const SceneRenderView& renderView) override;
	virtual void Resize(u32 width, u32 height, u32 depth = 1) override;

private:
	virtual void ApplyHeightFog(render::CommandContext& context, const SceneRenderView& renderView);
	virtual void ApplyBloom(render::CommandContext& context, const SceneRenderView& renderView);
	virtual void ApplyAntiAliasing(render::CommandContext& context, const SceneRenderView& renderView);
	virtual void ApplyToneMapping(render::CommandContext& context, const SceneRenderView& renderView);

private:
	struct
	{
		Arc< render::Texture > pHistoryTexture;
		Arc< render::Texture > pAntiAliasedTexture;

		Box< render::ComputePipeline > pTemporalAntiAliasingPSO;
		Box< render::ComputePipeline > pSharpenPSO;

		u64 ApplyCounter;
	} m_TAA;

	struct
	{
		Arc< render::Texture >         pResolvedTexture;
		Box< render::ComputePipeline > pToneMappingPSO;
	} m_ToneMapping;
};

} // namespace baamboo