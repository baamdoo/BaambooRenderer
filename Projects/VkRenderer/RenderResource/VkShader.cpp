#include "RendererPch.h"
#include "VkShader.h"

#include <fstream>
#include <spirv_cross/spirv_cross.hpp>

static constexpr u32 MAX_DYNAMIC_ARRAY_SIZE = 1024u;

namespace vk
{

std::vector< char > ReadSpirv(std::string_view filepath)
{
	std::ifstream file(filepath.data(), std::ios::ate | std::ios::binary);

	if (!file.is_open()) {
		throw std::runtime_error("failed to open file!");
	}

	std::streamsize fileSize = (std::streamsize)file.tellg();
	std::vector< char > buffer(fileSize);
	file.seekg(0);
	file.read(buffer.data(), fileSize);
	file.close();

	return buffer;
}

VkShaderStageFlagBits ParseSpirv(const u32* code, u64 codeSize, Shader::ShaderReflection& reflection)
{
	reflection = {};

	auto stageConverter([](spv::ExecutionModel executionModel)
		{
			switch (executionModel)
			{
			case spv::ExecutionModelVertex:
				return VK_SHADER_STAGE_VERTEX_BIT;
			case spv::ExecutionModelFragment:
				return VK_SHADER_STAGE_FRAGMENT_BIT;
			case spv::ExecutionModelGeometry:
				return VK_SHADER_STAGE_GEOMETRY_BIT;
			case spv::ExecutionModelTessellationControl:
				return VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
			case spv::ExecutionModelTessellationEvaluation:
				return VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
			case spv::ExecutionModelTaskNV:
				return VK_SHADER_STAGE_TASK_BIT_NV;
			case spv::ExecutionModelMeshNV:
				return VK_SHADER_STAGE_MESH_BIT_NV;
			case spv::ExecutionModelGLCompute:
				return VK_SHADER_STAGE_COMPUTE_BIT;
			default:
				assert(!"No Entry!");
				return VkShaderStageFlagBits(0);
			}
		});

	spirv_cross::Compiler compiler(code, codeSize);
	spv::ExecutionModel executionModel = compiler.get_execution_model();

	VkShaderStageFlagBits stage = stageConverter(executionModel);
	spirv_cross::ShaderResources resources = compiler.get_shader_resources();


	// **
	// Parse descriptors
	// **
	for (const auto& resource : resources.uniform_buffers)
	{
		auto buffers = compiler.get_active_buffer_ranges(resource.id);
		if (!buffers.empty())
		{
			const std::string& name = resource.name;
			const spirv_cross::SPIRType& type = compiler.get_type(resource.type_id);
			u32 set = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
			u32 binding = compiler.get_decoration(resource.id, spv::DecorationBinding);
			// u32 size = (u32)compiler.get_declared_struct_size(type);
			u32 arraySize = type.array.empty() ? 1u : type.array[0] == 0u ? MAX_DYNAMIC_ARRAY_SIZE : type.array[0]; // assume utilize only 1d-array for now

			Shader::DescriptorInfo& descriptorInfo = reflection.descriptors[set][binding];
			descriptorInfo.set = set;
			descriptorInfo.binding = binding;
			descriptorInfo.name = name;
			descriptorInfo.arraySize = arraySize;
			descriptorInfo.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		}
	}

	for (const auto& resource : resources.storage_buffers)
	{
		auto buffers = compiler.get_active_buffer_ranges(resource.id);
		if (!buffers.empty())
		{
			const std::string& name = resource.name;
			const spirv_cross::SPIRType& type = compiler.get_type(resource.type_id);
			u32 set = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
			u32 binding = compiler.get_decoration(resource.id, spv::DecorationBinding);
			u32 arraySize = type.array.empty() ? 1u : type.array[0] == 0u ? MAX_DYNAMIC_ARRAY_SIZE : type.array[0]; // assume utilize only 1d-array for now

			Shader::DescriptorInfo& descriptorInfo = reflection.descriptors[set][binding];
			descriptorInfo.set = set;
			descriptorInfo.binding = binding;
			descriptorInfo.name = name;
			descriptorInfo.arraySize = arraySize;
			descriptorInfo.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		}
	}

	for (const auto& resource : resources.sampled_images)
	{
		const std::string& name = resource.name;
		const spirv_cross::SPIRType& type = compiler.get_type(resource.type_id);
		u32 set = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
		u32 binding = compiler.get_decoration(resource.id, spv::DecorationBinding);
		u32 arraySize = type.array.empty() ? 1u : type.array[0] == 0u ? MAX_DYNAMIC_ARRAY_SIZE : type.array[0]; // assume utilize only 1d-array for now

		Shader::DescriptorInfo& descriptorInfo = reflection.descriptors[set][binding];
		descriptorInfo.set = set;
		descriptorInfo.binding = binding;
		descriptorInfo.name = name;
		descriptorInfo.arraySize = arraySize;
		descriptorInfo.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
	}

	for (const auto& resource : resources.storage_images)
	{
		const std::string& name = resource.name;
		const spirv_cross::SPIRType& type = compiler.get_type(resource.type_id);
		u32 set = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
		u32 binding = compiler.get_decoration(resource.id, spv::DecorationBinding);
		u32 arraySize = type.array.empty() ? 1u : type.array[0] == 0u ? MAX_DYNAMIC_ARRAY_SIZE : type.array[0]; // assume utilize only 1d-array for now

		Shader::DescriptorInfo& descriptorInfo = reflection.descriptors[set][binding];
		descriptorInfo.set = set;
		descriptorInfo.binding = binding;
		descriptorInfo.name = name;
		descriptorInfo.arraySize = arraySize;
		descriptorInfo.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	}

	for (const auto& resource : resources.push_constant_buffers)
	{
		const std::string& name = resource.name;
		const spirv_cross::SPIRType& type = compiler.get_type(resource.type_id);
		// u32 memberCount = u32(type.member_types.size());
		u32 size = (u32)compiler.get_declared_struct_size(type);
		u32 offset = 0;
		if (!reflection.pushConstants.empty())
			offset = reflection.pushConstants.back().offset + reflection.pushConstants.back().size;

		auto& pushConstant = reflection.pushConstants.emplace_back();
		pushConstant.size = size - offset;
		pushConstant.offset = offset;
	}

	return stage;
}

Shader::Shader(RenderContext& context, std::string_view name, CreationInfo&& info)
	: Super(context, name)
	, m_creationInfo(info)
{
	auto code = ReadSpirv(info.filepath);

	VkShaderModuleCreateInfo shaderInfo = {};
	shaderInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	shaderInfo.codeSize = code.size();
	shaderInfo.pCode = reinterpret_cast<const u32*>(code.data());
	VK_CHECK(vkCreateShaderModule(m_renderContext.vkDevice(), &shaderInfo, nullptr, &m_vkModule));

	m_stage = ParseSpirv(reinterpret_cast<const u32*>(code.data()), code.size() / 4, m_reflection);
}

Shader::~Shader()
{
	vkDestroyShaderModule(m_renderContext.vkDevice(), m_vkModule, nullptr);
}

} // namespace vk
