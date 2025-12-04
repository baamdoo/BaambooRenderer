#pragma once
#include "VkResource.h"

namespace vk
{

class VulkanShader : public render::Shader, public VulkanResource< VulkanShader >
{
public:
	struct DescriptorInfo
	{
		std::string      name;
		u32              binding        = INVALID_INDEX;
		u32              arraySize      = 0;
		VkDescriptorType descriptorType = VK_DESCRIPTOR_TYPE_MAX_ENUM;
	};

	struct ShaderReflection
	{
		std::vector< VkPushConstantRange >                       pushConstants;
		std::unordered_map< u32, std::vector< DescriptorInfo > > descriptors;
	};

	static Arc< VulkanShader > Create(VkRenderDevice& rd, const char* name, CreationInfo&& info);

	VulkanShader(VkRenderDevice& rd, const char* name, CreationInfo&& info);
	virtual ~VulkanShader();

	[[nodiscard]]
	inline VkShaderModule vkModule() const { return m_vkModule; }
	[[nodiscard]]
	inline VkShaderStageFlagBits Stage() const { assert(m_Stage != VK_SHADER_STAGE_FLAG_BITS_MAX_ENUM); return m_Stage; }
	[[nodiscard]]
	inline const ShaderReflection& Reflection() const { return m_Reflection; }

private:
    VkShaderModule        m_vkModule = VK_NULL_HANDLE;
	VkShaderStageFlagBits m_Stage = VK_SHADER_STAGE_FLAG_BITS_MAX_ENUM;

	CreationInfo     m_CreationInfo;
	ShaderReflection m_Reflection;
};

} // namespace vk