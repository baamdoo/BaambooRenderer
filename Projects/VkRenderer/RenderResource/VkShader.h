#pragma once
#include "VkResource.h"

namespace vk
{

class Shader : public Resource< Shader >
{
using Super = Resource< Shader >;

public:
    struct CreationInfo
    {
        std::string_view filepath;
    };

	struct DescriptorInfo
	{
		std::string_view name;
		u32              binding;
		u32              arraySize;
		VkDescriptorType descriptorType;
	};

	struct ShaderReflection
	{
		std::vector< VkPushConstantRange >        pushConstants;
		std::unordered_map< u32, DescriptorInfo > descriptors;
	};

	[[nodiscard]]
	inline VkShaderModule vkModule() const { return m_vkModule; }
	[[nodiscard]]
	inline VkShaderStageFlagBits Stage() const { assert(m_stage != VK_SHADER_STAGE_FLAG_BITS_MAX_ENUM); return m_stage; }
	[[nodiscard]]
	inline const ShaderReflection& Reflection() const { return m_reflection; }

protected:
	template< typename T >
	friend class ResourcePool;

	Shader(RenderContext& context, std::wstring_view name, CreationInfo&& info);
	virtual ~Shader();

private:
    VkShaderModule        m_vkModule = VK_NULL_HANDLE;
	VkShaderStageFlagBits m_stage = VK_SHADER_STAGE_FLAG_BITS_MAX_ENUM;

	CreationInfo     m_creationInfo;
	ShaderReflection m_reflection;
};

} // namespace vk