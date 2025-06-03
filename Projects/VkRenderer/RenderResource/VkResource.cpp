#include "RendererPch.h"
#include "VkResource.h"

namespace vk
{

Resource::Resource(RenderDevice& device, std::string_view name, eResourceType type)
	: m_RenderDevice(device)
	, m_Name(name)
	, m_Type(type) 
{
}

void Resource::SetDeviceObjectName(u64 handle, VkObjectType type)
{
	m_RenderDevice.SetVkObjectName(m_Name, handle, type);
}

} // namespace vk