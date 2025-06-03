#pragma once
namespace vk
{

enum class eResourceType : u8 
{
	Buffer,
	Texture,
	Sampler,
	Shader,
	AccelerationStructure
};

class Resource : public ArcBase
{
public:
	Resource(RenderDevice& device, std::string_view name, eResourceType type);
	virtual ~Resource() = default;

protected:
	void SetDeviceObjectName(u64 handle, VkObjectType type);

	RenderDevice&    m_RenderDevice;
	std::string_view m_Name;
	eResourceType    m_Type;

	VmaAllocation     m_vmaAllocation  = VK_NULL_HANDLE;
	VmaAllocationInfo m_AllocationInfo = {};
};

}