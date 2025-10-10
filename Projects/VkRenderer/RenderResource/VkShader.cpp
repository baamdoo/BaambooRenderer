#include "RendererPch.h"
#include "VkShader.h"

#include <fstream>
#include <spirv_cross/spirv_cross.hpp>

static constexpr u32 MAX_DYNAMIC_ARRAY_SIZE = 1024u;

namespace vk
{

#define VK_SHADER_PATH(filename, stage) GetCompiledShaderPath(filename, stage)
std::string GetCompiledShaderPath(const std::string& filename, render::eShaderStage stage)
{
	using namespace render;

	std::string stageStr;
	switch (VK_SHADER_STAGE(stage))
	{
	case VK_SHADER_STAGE_VERTEX_BIT:
		stageStr = ".vert";
		break;
	case VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT:
		stageStr = ".hull";
		break;
	case VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT:
		stageStr = ".domain";
		break;
	case VK_SHADER_STAGE_GEOMETRY_BIT:
		stageStr = ".geom";
		break;
	case VK_SHADER_STAGE_FRAGMENT_BIT:
		stageStr = ".frag";
		break;
	case VK_SHADER_STAGE_COMPUTE_BIT:
		stageStr = ".comp";
		break;
	case VK_SHADER_STAGE_ALL_GRAPHICS:
	case VK_SHADER_STAGE_ALL:
		assert(false && "Invalid shader bit for parsing spirv path!");
		break;

	case VK_SHADER_STAGE_RAYGEN_BIT_KHR:
	case VK_SHADER_STAGE_ANY_HIT_BIT_KHR:
	case VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR:
	case VK_SHADER_STAGE_MISS_BIT_KHR:
	case VK_SHADER_STAGE_INTERSECTION_BIT_KHR:
	case VK_SHADER_STAGE_CALLABLE_BIT_KHR:
		// TODO
		assert(false && "Invalid shader bit for parsing spirv path!");
		break;

	case VK_SHADER_STAGE_TASK_BIT_EXT:
		stageStr = ".task";
		break;
	case VK_SHADER_STAGE_MESH_BIT_EXT:
		stageStr = ".mesh";
		break;
	}

	return SPIRV_PATH.string() + filename + stageStr + ".spv";
}

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

VkShaderStageFlagBits ParseSpirv(const u32* code, u64 codeSize, VulkanShader::ShaderReflection& reflection)
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
			std::string instanceName = compiler.get_name(resource.id);

			const spirv_cross::SPIRType& type = compiler.get_type(resource.type_id);
			u32 set       = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
			u32 binding   = compiler.get_decoration(resource.id, spv::DecorationBinding);
			// u32 size = (u32)compiler.get_declared_struct_size(type);
			u32 arraySize = type.array.empty() ? 1u : type.array[0] == 0u ? MAX_DYNAMIC_ARRAY_SIZE : type.array[0]; // assume utilize only 1d-array for now

			VulkanShader::DescriptorInfo& descriptorInfo = reflection.descriptors[set].emplace_back();
			descriptorInfo.binding        = binding;
			descriptorInfo.name           = instanceName;
			descriptorInfo.arraySize      = arraySize;
			descriptorInfo.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		}
	}

	for (const auto& resource : resources.storage_buffers)
	{
		auto buffers = compiler.get_active_buffer_ranges(resource.id);
		if (!buffers.empty())
		{
			std::string instanceName = compiler.get_name(resource.id);

			const spirv_cross::SPIRType& type = compiler.get_type(resource.type_id);
			u32 set       = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
			u32 binding   = compiler.get_decoration(resource.id, spv::DecorationBinding);
			u32 arraySize = type.array.empty() ? 1u : type.array[0] == 0u ? MAX_DYNAMIC_ARRAY_SIZE : type.array[0]; // assume utilize only 1d-array for now

			VulkanShader::DescriptorInfo& descriptorInfo = reflection.descriptors[set].emplace_back();
			descriptorInfo.binding        = binding;
			descriptorInfo.name           = instanceName;
			descriptorInfo.arraySize      = arraySize;
			descriptorInfo.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		}
	}

	for (const auto& resource : resources.sampled_images)
	{
		std::string instanceName = compiler.get_name(resource.id);

		const spirv_cross::SPIRType& type = compiler.get_type(resource.type_id);
		u32 set       = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
		u32 binding   = compiler.get_decoration(resource.id, spv::DecorationBinding);
		u32 arraySize = type.array.empty() ? 1u : type.array[0] == 0u ? MAX_DYNAMIC_ARRAY_SIZE : type.array[0]; // assume utilize only 1d-array for now

		VulkanShader::DescriptorInfo& descriptorInfo = reflection.descriptors[set].emplace_back();
		descriptorInfo.binding        = binding;
		descriptorInfo.name           = instanceName;
		descriptorInfo.arraySize      = arraySize;
		descriptorInfo.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	}

	for (const auto& resource : resources.storage_images)
	{
		std::string instanceName = compiler.get_name(resource.id);

		const spirv_cross::SPIRType& type = compiler.get_type(resource.type_id);
		u32 set = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
		u32 binding = compiler.get_decoration(resource.id, spv::DecorationBinding);
		u32 arraySize = type.array.empty() ? 1u : type.array[0] == 0u ? MAX_DYNAMIC_ARRAY_SIZE : type.array[0]; // assume utilize only 1d-array for now

		VulkanShader::DescriptorInfo& descriptorInfo = reflection.descriptors[set].emplace_back();
		descriptorInfo.binding        = binding;
		descriptorInfo.name           = instanceName;
		descriptorInfo.arraySize      = arraySize;
		descriptorInfo.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	}

	for (const auto& resource : resources.push_constant_buffers)
	{
		//const std::string& name = resource.name;
		const spirv_cross::SPIRType& type = compiler.get_type(resource.type_id);
		// u32 memberCount = u32(type.member_types.size());
		u32 size = (u32)compiler.get_declared_struct_size(type);
		u32 offset = 0;
		if (!reflection.pushConstants.empty())
			offset = reflection.pushConstants.back().offset + reflection.pushConstants.back().size;

		auto& pushConstant      = reflection.pushConstants.emplace_back();
		pushConstant.stageFlags = stage;
		pushConstant.size       = size - offset;
		pushConstant.offset     = offset;
	}

	return stage;
}

Arc< VulkanShader > VulkanShader::Create(VkRenderDevice& rd, const std::string& name, CreationInfo&& info)
{
	return MakeArc< VulkanShader >(rd, name, std::move(info));
}

VulkanShader::VulkanShader(VkRenderDevice& rd, const std::string& name, CreationInfo&& info)
	: render::Shader(name, std::move(info))
	, VulkanResource(rd, name)
{
	auto code = ReadSpirv(VK_SHADER_PATH(info.filename, info.stage));

	VkShaderModuleCreateInfo shaderInfo = {};
	shaderInfo.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	shaderInfo.codeSize = code.size();
	shaderInfo.pCode    = reinterpret_cast<const u32*>(code.data());
	VK_CHECK(vkCreateShaderModule(m_RenderDevice.vkDevice(), &shaderInfo, nullptr, &m_vkModule));

	m_Stage = ParseSpirv(reinterpret_cast<const u32*>(code.data()), code.size() / 4, m_Reflection);

	SetDeviceObjectName((u64)m_vkModule, VK_OBJECT_TYPE_SHADER_MODULE);
}

VulkanShader::~VulkanShader()
{
	vkDestroyShaderModule(m_RenderDevice.vkDevice(), m_vkModule, nullptr);
}

} // namespace vk
