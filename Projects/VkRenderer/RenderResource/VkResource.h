#pragma once
#include "BaambooCore/ResourceHandle.h"

namespace vk
{

template< typename TResource >
class Resource
{
protected:
	friend class ResourceManager;

	Resource(RenderContext& context, std::wstring_view name) : m_RenderContext(context), m_Name(name) {}
	virtual ~Resource() {}

protected:
	RenderContext&     m_RenderContext;
	std::wstring_view  m_Name;

	VmaAllocation     m_vmaAllocation = VK_NULL_HANDLE;
	VmaAllocationInfo m_vmaAllocationInfo = {};
};

}