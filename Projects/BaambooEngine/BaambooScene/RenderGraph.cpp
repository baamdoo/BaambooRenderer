#include "BaambooPch.h"
#include "RenderGraph.h"
#include "RenderCommon/RenderNode.h"

namespace baamboo
{

void RenderGraph::AddRenderNode(const Arc< render::RenderNode >& pNode)
{
	if (m_RenderNodeNameMap.contains(pNode->GetName()))
	{
		return;
	}

	m_RenderNodes.push_back(pNode);
	m_RenderNodeNameMap.emplace(pNode->GetName(), pNode);
}

void RenderGraph::RemoveRenderNode(const std::string& nodeName)
{
	const auto& pNode = m_RenderNodeNameMap.find(nodeName);
	if (pNode == m_RenderNodeNameMap.end())
	{
		printf("No %s render node in RenderGraph!", nodeName.c_str());
		return;
	}

	std::erase(m_RenderNodes, pNode->second);
}

}
