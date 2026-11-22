#include "RendererPch.h"
#include "VkShader.h"

#include <span>

namespace vk
{

Arc< VulkanShader > VulkanShader::Create(VkRenderDevice& rd, const char* name, CreationInfo&& info)
{
	return MakeArc< VulkanShader >(rd, name, std::move(info));
}

VulkanShader::VulkanShader(VkRenderDevice& rd, const char* name, CreationInfo&& info)
	: render::Shader(name, std::move(info))
	, VulkanResource(rd, name)
{
	std::span< const u8 > code = m_CreationInfo.bytecode;
	BB_ASSERT(!code.empty(), "Shader '%s' has no bytecode", name);

	VkShaderModuleCreateInfo shaderInfo = {};
	shaderInfo.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	shaderInfo.codeSize = code.size();
	shaderInfo.pCode    = reinterpret_cast<const u32*>(code.data());
	VK_CHECK(vkCreateShaderModule(m_RenderDevice.vkDevice(), &shaderInfo, nullptr, &m_vkModule));

	SetDeviceObjectName((u64)m_vkModule, VK_OBJECT_TYPE_SHADER_MODULE);
}

VulkanShader::~VulkanShader()
{
	vkDestroyShaderModule(m_RenderDevice.vkDevice(), m_vkModule, nullptr);
}

} // namespace vk
