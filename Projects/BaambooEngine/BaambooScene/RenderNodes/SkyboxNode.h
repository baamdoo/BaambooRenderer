#pragma once
#include "RenderCommon/RenderNode.h"

namespace baamboo
{

class DynamicSkyboxNode : public render::RenderNode
{
using Super = render::RenderNode;
public:
	DynamicSkyboxNode(render::RenderDevice& rd);
	virtual ~DynamicSkyboxNode();

	virtual void Apply(render::CommandContext& context, const SceneRenderView& renderView) override;

private:
	Arc< render::Texture > m_pSkyboxLUT;

	Box< render::ComputePipeline > m_pBakeSkyboxPSO;
};

} // namespace baamboo