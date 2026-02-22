#pragma once
#include "RenderCommon/RenderNode.h"

namespace baamboo
{

class RaytracingTestNode : public render::RenderNode
{
using Super = render::RenderNode;
public:
	RaytracingTestNode(render::RenderDevice& rd);
	virtual ~RaytracingTestNode() = default;

	virtual void Apply(render::CommandContext& context, const SceneRenderView& renderView) override;
	virtual void Resize(u32 width, u32 height, u32 depth = 1) override;

private:
	Arc< render::Texture > m_pOutputTexture;

	Arc< render::ShaderBindingTable > m_pSBT;
	Box< render::RaytracingPipeline > m_pRaytracingPSO;

	// Test Geometry & AS (나중에는 Dx12SceneResource로 이동)
	Arc< render::Buffer > m_VertexBuffer;
	Arc< render::Buffer > m_BLAS;
	Arc< render::Buffer > m_TLAS;
	Arc< render::Buffer > m_ScratchBuffer;
	Arc< render::Buffer > m_InstanceDescs;
};

}