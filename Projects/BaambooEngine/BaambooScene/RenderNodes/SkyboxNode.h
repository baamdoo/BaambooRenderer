#pragma once
#include "RenderCommon/RenderNode.h"

namespace baamboo::shader { class PipelineFactory; }

namespace baamboo
{

//-------------------------------------------------------------------------
// Static Skybox
//-------------------------------------------------------------------------
class StaticSkyboxNode : public render::RenderNode
{
using Super = render::RenderNode;
public:
	StaticSkyboxNode(render::RenderDevice& rd, shader::PipelineFactory& pipelineFactory);
	virtual ~StaticSkyboxNode();

	virtual void Apply(render::CommandContext& context, const SceneRenderView& renderView) override;

private:
	std::string            m_SkyboxPathCache;
	Arc< render::Texture > m_pSkyboxTexture;
	Arc< render::Texture > m_pSkyboxLUT;

	Box< render::ComputePipeline > m_pBakeSkyboxPSO;
	u32                            m_BakedBuildCount = 0;
};

} // namespace baamboo