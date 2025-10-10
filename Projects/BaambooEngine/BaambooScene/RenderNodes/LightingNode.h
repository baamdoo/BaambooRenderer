#pragma once
#include "RenderCommon/RenderNode.h"

namespace baamboo
{

class LightingNode : public render::RenderNode
{
using Super = render::RenderNode;
public:
	LightingNode(render::RenderDevice& rd);
	virtual ~LightingNode() = default;

	virtual void Apply(render::CommandContext& context, const SceneRenderView& renderView) override;
	virtual void Resize(u32 width, u32 height, u32 depth = 1) override;

private:
	Arc< render::Texture > m_pSceneTexture;

	Box< render::ComputePipeline > m_pLightingPSO;
};

} // namespace baamboo