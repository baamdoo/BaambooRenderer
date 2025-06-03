#pragma once
#include "VkResource.h"

namespace vk
{

class Shader : public Resource
{
using Super = Resource;

public:
    struct CreationInfo
    {
        std::string_view filepath;
    };

	struct DescriptorInfo
	{
		std::string_view name;
		u32              binding        = INVALID_INDEX;
		u32              arraySize      = 0;
		VkDescriptorType descriptorType = VK_DESCRIPTOR_TYPE_MAX_ENUM;
	};

	struct ShaderReflection
	{
		std::vector< VkPushConstantRange >        pushConstants;
		std::unordered_map< u32, DescriptorInfo > descriptors;
	};

	static Arc< Shader > Create(RenderDevice& device, std::string_view name, CreationInfo&& info);

	Shader(RenderDevice& device, std::string_view name, CreationInfo&& info);
	virtual ~Shader();

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