#pragma once
#include "RenderCommon/RenderNode.h"

namespace baamboo
{

//-------------------------------------------------------------------------
// Static Skybox
//-------------------------------------------------------------------------
class StaticSkyboxNode : public render::RenderNode
{
using Super = render::RenderNode;
public:
	StaticSkyboxNode(render::RenderDevice& rd);
	virtual ~StaticSkyboxNode();

	virtual void Apply(render::CommandContext& context, const SceneRenderView& renderView) override;

private:
	std::string            m_SkyboxPathCache;
	Arc< render::Texture > m_pSkyboxTexture;
	Arc< render::Texture > m_pSkyboxLUT;

	Box< render::ComputePipeline > m_pBakeSkyboxPSO;
};


//-------------------------------------------------------------------------
// Dynamic Skybox
//-------------------------------------------------------------------------
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