#pragma once
#include "BaambooCore/ResourceHandle.h"

namespace vk
{

template< typename TResource >
class Resource
{
protected:
	friend class ResourceManager;

	Resource(RenderContext& context, std::wstring_view name) : m_renderContext(context), m_name(name) {}
	virtual ~Resource() {}

protected:
	RenderContext&     m_renderContext;
	std::wstring_view  m_name;

	VmaAllocation     m_vmaAllocation = VK_NULL_HANDLE;
	VmaAllocationInfo m_vmaAllocationInfo = {};
};

}