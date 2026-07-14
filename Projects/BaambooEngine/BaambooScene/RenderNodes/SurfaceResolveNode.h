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
	Box< render::ComputePipeline > m_pResolvePSO;

	Arc< render::Buffer > m_pVoxelVertexFallback;
	Arc< render::Buffer > m_pVoxelMeshletFallback;
	Arc< render::Buffer > m_pVoxelMeshletVertexFallback;
	Arc< render::Buffer > m_pVoxelMeshletTriangleFallback;

	Arc< render::Texture > m_pErosionDetailFallback;

	bool m_bErosionFallbackTransitioned = false;
};


} // namespace baamboo
