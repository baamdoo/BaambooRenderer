#pragma once
#include "VkResource.h"

namespace vk
{

class VulkanShader : public render::Shader, public VulkanResource< VulkanShader >
{
public:
	static Arc< VulkanShader > Create(VkRenderDevice& rd, const char* name, CreationInfo&& info);

	VulkanShader(VkRenderDevice& rd, const char* name, CreationInfo&& info);
	virtual ~VulkanShader();

	[[nodiscard]]
	inline VkShaderModule vkModule() const { return m_vkModule; }

private:
	VkShaderModule m_vkModule = VK_NULL_HANDLE;
};

} // namespace vk