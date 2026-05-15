#pragma once
#include "RenderCommon/RenderNode.h"

namespace baamboo
{


class DebugDrawNode : public render::RenderNode
{
using Super = render::RenderNode;
public:
	DebugDrawNode(render::RenderDevice& rd);
	virtual ~DebugDrawNode() = default;

	virtual void Apply(render::CommandContext& context, const SceneRenderView& renderView) override;
	virtual void Resize(u32 width, u32 height, u32 depth = 1) override;
	virtual void DrawUI() override;

private:
	void EnsureRenderTarget(Arc< render::Texture > pColor);

	void ApplyFrustumWireframe(render::CommandContext& context);
	void ApplyClusterWireframe(render::CommandContext& context, const SceneRenderView& renderView);
	void ApplyLightWireframe  (render::CommandContext& context, const SceneRenderView& renderView);

private:
	Arc< render::Texture >          m_pColorRef;
	Arc< render::RenderTarget >     m_pRenderTarget;

	Box< render::GraphicsPipeline > m_pFrustumPSO;
	Box< render::GraphicsPipeline > m_pClusterPSO;
	Box< render::GraphicsPipeline > m_pLightPSO;
};

} // namespace baamboo
