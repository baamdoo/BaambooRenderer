#pragma once
#include "RenderCommon/RenderNode.h"

namespace baamboo
{

// =========================================================================
// Cluster AABB Build
// =========================================================================
class ClusterBuildNode : public render::RenderNode
{
using Super = render::RenderNode;
public:
	ClusterBuildNode(render::RenderDevice& rd);
	virtual ~ClusterBuildNode() = default;

	virtual void Apply(render::CommandContext& context, const SceneRenderView& renderView) override;
	virtual void Resize(u32 width, u32 height, u32 depth = 1) override;

private:
	Arc< render::Buffer > m_pClusterAABBBuffer;
	Box< render::ComputePipeline > m_pClusterBuildPSO;

	u32 m_NumTilesX = 0;
	u32 m_NumTilesY = 0;
};


// =========================================================================
// Light Culling
// =========================================================================
class LightCullingNode : public render::RenderNode
{
	using Super = render::RenderNode;
public:
	LightCullingNode(render::RenderDevice& rd);
	virtual ~LightCullingNode() = default;

	virtual void Apply(render::CommandContext& context, const SceneRenderView& renderView) override;
	virtual void Resize(u32 width, u32 height, u32 depth = 1) override;

private:
	Arc< render::Buffer > m_pLightGridBuffer;
	Arc< render::Buffer > m_pLightListDataBuffer;

	Box< render::ComputePipeline > m_pCountPSO;
	Box< render::ComputePipeline > m_pScanPSO;
	Box< render::ComputePipeline > m_pWritePSO;

	u32 m_NumTilesX = 0;
	u32 m_NumTilesY = 0;
};


// =========================================================================
// Lighting
// =========================================================================
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
	Arc< render::Texture > m_pLtcLut1;
	Arc< render::Texture > m_pLtcLut2;
	Arc< render::Buffer >  m_pFallbackLightGridBuffer;
	Arc< render::Buffer >  m_pFallbackLightListDataBuffer;

	Box< render::ComputePipeline > m_pLightingPSO;
};


} // namespace baamboo