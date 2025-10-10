#pragma once

namespace vk
{

template< typename TResource >
class VulkanResource
{
public:
	VulkanResource(VkRenderDevice& rd, const std::string& name)
		: m_RenderDevice(rd), m_Name(name) {}
	virtual ~VulkanResource() = default;

protected:
	void SetDeviceObjectName(u64 handle, VkObjectType type)
	{
		m_RenderDevice.SetVkObjectName(m_Name, handle, type);
	}

	VkRenderDevice& m_RenderDevice;
	std::string     m_Name;

	VmaAllocation     m_vmaAllocation  = VK_NULL_HANDLE;
	VmaAllocationInfo m_AllocationInfo = {};
};

}