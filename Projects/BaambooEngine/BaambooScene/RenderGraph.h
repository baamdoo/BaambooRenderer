#pragma once

namespace render
{
	class RenderNode;
}

namespace baamboo
{

class RenderGraph
{
public:
	void AddRenderNode(const Arc< render::RenderNode >& pNode);
	void RemoveRenderNode(const std::string& nodeName);

	const std::vector< Arc< render::RenderNode > >& GetRenderNodes() const { return m_RenderNodes; }

	const Arc< render::RenderNode >& GetRenderNodeByName(const std::string& nodeName) const
	{
		auto it = m_RenderNodeNameMap.find(nodeName);
		return (it != m_RenderNodeNameMap.end()) ? it->second : nullptr;
	}

private:
	friend class Scene;

	std::vector< Arc< render::RenderNode > >                     m_RenderNodes;
	std::unordered_map< std::string, Arc< render::RenderNode > > m_RenderNodeNameMap;
};

} // namespace baamboo