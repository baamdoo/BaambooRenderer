#pragma once
#include "RenderCommon/RenderNode.h"

namespace baamboo
{

class DebugNode : public render::RenderNode
{
using Super = render::RenderNode;
public:
	DebugNode(render::RenderDevice& rd);
	virtual ~DebugNode() = default;

	virtual void Apply(render::CommandContext& context, const SceneRenderView& renderView) override;
	virtual void Resize(u32 width, u32 height, u32 depth = 1) override;

private:
	virtual void ApplyBoundingLines(render::CommandContext& context, const SceneRenderView& renderView);

private:
	struct
	{
		Arc< render::RenderTarget > pRenderTarget;

		Box< render::GraphicsPipeline > pBoundingLinePSO;
	} m_Bounding;
};

} // namespace baamboo