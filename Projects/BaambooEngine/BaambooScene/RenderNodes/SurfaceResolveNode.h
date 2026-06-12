#pragma once
#include "RenderCommon/RenderNode.h"

namespace baamboo
{


class SurfaceResolveNode : public render::RenderNode
{
using Super = render::RenderNode;
public:
	SurfaceResolveNode(render::RenderDevice& rd);
	virtual ~SurfaceResolveNode() = default;

	virtual void Apply(render::CommandContext& context, const SceneRenderView& renderView) override;
	virtual void Resize(u32 width, u32 height, u32 depth = 1) override;

private:
	Arc< render::Texture >         m_pCoreNormal;
	Arc< render::Texture >         m_pCoreMaterial;
	Arc< render::Buffer >          m_pNullPatches; // 1-element fallback so the patch binding stays valid without a terrain
	Box< render::ComputePipeline > m_pResolvePSO;
};


} // namespace baamboo
