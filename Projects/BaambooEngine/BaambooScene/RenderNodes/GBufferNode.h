#pragma once
#include "RenderCommon/RenderNode.h"

namespace baamboo
{

class GBufferNode : public render::RenderNode
{
using Super = render::RenderNode;
public:
	GBufferNode(render::RenderDevice& rd);
	virtual ~GBufferNode() = default;

	virtual void Apply(render::CommandContext& context, const SceneRenderView& renderView) override;
	virtual void Resize(u32 width, u32 height, u32 depth = 1) override;

private:
	Arc< render::RenderTarget >     m_pRenderTarget;
	Box< render::GraphicsPipeline > m_pGBufferPSO;
};

} // namespace baamboo