#include "RendererPch.h"
#include "VkRenderPipeline.h"
#include "VkResourceManager.h"
#include "RenderResource/VkRenderTarget.h"
#include "RenderResource/VkSceneResource.h"
#include "Utils/FileIO.hpp"

#include <unordered_set>

namespace vk
{

using namespace render;

#pragma region ConvertToVk
#define VK_PIPELINE_PRIMITIVETOPOLOGY(topology) ConvertToVkPrimitiveTopology(topology)
VkPrimitiveTopology ConvertToVkPrimitiveTopology(render::ePrimitiveTopology topology)
{
	switch (topology)
	{
	case ePrimitiveTopology::Point     : return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
	case ePrimitiveTopology::Line      : return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
	case ePrimitiveTopology::Triangle  : return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	case ePrimitiveTopology::Patch     : return VK_PRIMITIVE_TOPOLOGY_PATCH_LIST;

	default:
		assert(false && "Invalid primitive topology!"); break;
	}

	return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
}

#define VK_PIPELINE_CULLMODE(mode) ConvertToVkCullMode(mode)
VkCullModeFlagBits ConvertToVkCullMode(render::eCullMode mode)
{
	switch (mode)
	{
	case eCullMode::None  : return VK_CULL_MODE_NONE;
	case eCullMode::Front : return VK_CULL_MODE_FRONT_BIT;
	case eCullMode::Back  : return VK_CULL_MODE_BACK_BIT;

	default:
		assert(false && "Invalid cull mode!"); break;
	}

	return VK_CULL_MODE_NONE;
}

#define VK_PIPELINE_FRONTFACE(face) ConvertToVkFrontFace(face)
VkFrontFace ConvertToVkFrontFace(render::eFrontFace face)
{
	switch (face)
	{
	case eFrontFace::Clockwise        : return VK_FRONT_FACE_CLOCKWISE;
	case eFrontFace::CounterClockwise : return VK_FRONT_FACE_COUNTER_CLOCKWISE;

	default:
		assert(false && "Invalid front face!"); break;
	}

	return VK_FRONT_FACE_COUNTER_CLOCKWISE;
}

#define VK_PIPELINE_LOGICOP(op) ConvertToVkLogicOp(op)
VkLogicOp ConvertToVkLogicOp(render::eLogicOp op)
{
	switch (op)
	{
	case eLogicOp::None   : return VK_LOGIC_OP_NO_OP;
	case eLogicOp::Clear  : return VK_LOGIC_OP_CLEAR;
	case eLogicOp::Set    : return VK_LOGIC_OP_SET;
	case eLogicOp::Copy   : return VK_LOGIC_OP_COPY;
	case eLogicOp::CopyInv: return VK_LOGIC_OP_COPY_INVERTED;

	default:
		assert(false && "Invalid logic op!"); break;
	}

	return VK_LOGIC_OP_NO_OP;
}

#define VK_PIPELINE_BLENDOP(op) ConvertToVkBlendOp(op)
VkBlendOp ConvertToVkBlendOp(render::eBlendOp op)
{
	switch (op)
	{
	case eBlendOp::Add        : return VK_BLEND_OP_ADD;
	case eBlendOp::Subtract   : return VK_BLEND_OP_SUBTRACT;
	case eBlendOp::SubtractInv: return VK_BLEND_OP_REVERSE_SUBTRACT;
	case eBlendOp::Min        : return VK_BLEND_OP_MIN;
	case eBlendOp::Max        : return VK_BLEND_OP_MAX;

	default:
		assert(false && "Invalid blend op!"); break;
	}

	return VK_BLEND_OP_ADD;
}

#define VK_PIPELINE_BLENDFACTOR(factor) ConvertToVkBlendFactor(factor)
VkBlendFactor ConvertToVkBlendFactor(render::eBlendFactor factor)
{
	switch (factor)
	{
	case eBlendFactor::Zero             : return VK_BLEND_FACTOR_ZERO;
	case eBlendFactor::One              : return VK_BLEND_FACTOR_ONE;
	case eBlendFactor::SrcColor         : return VK_BLEND_FACTOR_SRC_COLOR;
	case eBlendFactor::SrcColorInv      : return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
	case eBlendFactor::SrcAlpha         : return VK_BLEND_FACTOR_SRC_ALPHA;
	case eBlendFactor::SrcAlphaInv      : return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	case eBlendFactor::DstColor         : return VK_BLEND_FACTOR_DST_COLOR;
	case eBlendFactor::DstColorInv      : return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
	case eBlendFactor::DstAlpha         : return VK_BLEND_FACTOR_DST_ALPHA;
	case eBlendFactor::DstAlphaInv      : return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
	case eBlendFactor::SrcAlphaSaturate : return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;

	default: 
		assert(false && "Invalid blend factor!"); break;
	}
	
	return VK_BLEND_FACTOR_ZERO;
}
#pragma endregion


//-------------------------------------------------------------------------
// Graphics Pipeline
//-------------------------------------------------------------------------
VulkanGraphicsPipeline::VulkanGraphicsPipeline(VkRenderDevice& rd, const std::string& name)
	: render::GraphicsPipeline(name)
	, m_RenderDevice(rd)
{
	// **
	// Set default values
	// **

	// input
	m_PipelineDesc.vertexInputInfo            = {};
	m_PipelineDesc.vertexInputInfo.sType      = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	m_PipelineDesc.inputAssemblyInfo          = {};
	m_PipelineDesc.inputAssemblyInfo.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	m_PipelineDesc.inputAssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

	// viewport
	m_PipelineDesc.viewportStateInfo               = {};
	m_PipelineDesc.viewportStateInfo.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	m_PipelineDesc.viewportStateInfo.viewportCount = 1;
	m_PipelineDesc.viewportStateInfo.scissorCount  = 1;

	// rasterizer
	m_PipelineDesc.rasterizerInfo             = {};
	m_PipelineDesc.rasterizerInfo.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	m_PipelineDesc.rasterizerInfo.polygonMode = VK_POLYGON_MODE_FILL;
	m_PipelineDesc.rasterizerInfo.cullMode    = VK_CULL_MODE_BACK_BIT;
	m_PipelineDesc.rasterizerInfo.frontFace   = VK_FRONT_FACE_CLOCKWISE;
	m_PipelineDesc.rasterizerInfo.lineWidth   = 1.f;

	// depth-stencil
	m_PipelineDesc.depthStencilInfo                  = {};
	m_PipelineDesc.depthStencilInfo.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	m_PipelineDesc.depthStencilInfo.depthTestEnable  = VK_TRUE;
	m_PipelineDesc.depthStencilInfo.depthWriteEnable = VK_FALSE; // False as default. Since depth is mainly writable by depth pre-pass only.
	m_PipelineDesc.depthStencilInfo.depthCompareOp   = VK_COMPARE_OP_LESS_OR_EQUAL;
	m_PipelineDesc.depthStencilInfo.minDepthBounds   = 0.f;
	m_PipelineDesc.depthStencilInfo.maxDepthBounds   = 1.f;

	// multi-sampling
	m_PipelineDesc.multisamplingInfo                      = {};
	m_PipelineDesc.multisamplingInfo.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	m_PipelineDesc.multisamplingInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
	m_PipelineDesc.multisamplingInfo.minSampleShading     = 1.f;
}

VulkanGraphicsPipeline::~VulkanGraphicsPipeline()
{
	vkDestroyPipeline(m_RenderDevice.vkDevice(), m_vkPipeline, nullptr);
	vkDestroyPipelineLayout(m_RenderDevice.vkDevice(), m_vkPipelineLayout, nullptr);
	for (u32 i = 1; i < m_vkSetLayouts.size(); ++i)
	{
		if (m_vkSetLayouts[i] == m_RenderDevice.GetEmptyDescriptorSetLayout())
			continue;

		vkDestroyDescriptorSetLayout(m_RenderDevice.vkDevice(), m_vkSetLayouts[i], nullptr);
	}
}

GraphicsPipeline& VulkanGraphicsPipeline::SetVertexInputs(std::vector< VkVertexInputBindingDescription >&& streams, std::vector< VkVertexInputAttributeDescription >&& attributes)
{
	m_PipelineDesc.vertexInputInfo.vertexBindingDescriptionCount   = static_cast<u32>(streams.size());
	m_PipelineDesc.vertexInputInfo.pVertexBindingDescriptions      = streams.data();
	m_PipelineDesc.vertexInputInfo.vertexAttributeDescriptionCount = static_cast<u32>(attributes.size());
	m_PipelineDesc.vertexInputInfo.pVertexAttributeDescriptions    = attributes.data();

	return *this;
}

GraphicsPipeline& VulkanGraphicsPipeline::SetRenderTarget(Arc< render::RenderTarget > pRenderTarget)
{
	auto rhiRenderTarget = StaticCast<VulkanRenderTarget>(pRenderTarget);
	assert(rhiRenderTarget);

	m_PipelineDesc.renderPass = rhiRenderTarget->vkRenderPass();
	m_PipelineDesc.blendStates.resize(rhiRenderTarget->GetNumColors());
	for (auto& blendStates : m_PipelineDesc.blendStates)
	{
		// set default values
		blendStates.blendEnable         = VK_FALSE;
		blendStates.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
		blendStates.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
		blendStates.colorBlendOp        = VK_BLEND_OP_ADD;
		blendStates.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
		blendStates.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
		blendStates.alphaBlendOp        = VK_BLEND_OP_ADD;
		blendStates.colorWriteMask      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	}

	return *this;
}

GraphicsPipeline& VulkanGraphicsPipeline::SetFillMode(bool bWireframe)
{
	m_PipelineDesc.rasterizerInfo.polygonMode = bWireframe ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
	return *this;
}

GraphicsPipeline& VulkanGraphicsPipeline::SetCullMode(render::eCullMode cullMode)
{
	m_PipelineDesc.rasterizerInfo.cullMode = VK_PIPELINE_CULLMODE(cullMode);
	return *this;
}

GraphicsPipeline& VulkanGraphicsPipeline::SetTopology(ePrimitiveTopology topology)
{
	m_PipelineDesc.inputAssemblyInfo.topology = VK_PIPELINE_PRIMITIVETOPOLOGY(topology);
	return *this;
}

GraphicsPipeline& VulkanGraphicsPipeline::SetDepthTestEnable(bool bEnable, eCompareOp compareOp)
{
	m_PipelineDesc.depthStencilInfo.depthTestEnable = bEnable;
	m_PipelineDesc.depthStencilInfo.depthCompareOp  = VK_COMPAREOP(compareOp);
	return *this;
}

GraphicsPipeline& VulkanGraphicsPipeline::SetDepthWriteEnable(bool bEnable, eCompareOp compareOp)
{
	m_PipelineDesc.depthStencilInfo.depthWriteEnable = bEnable;
	m_PipelineDesc.depthStencilInfo.depthCompareOp   = VK_COMPAREOP(compareOp);
	return *this;
}

GraphicsPipeline& VulkanGraphicsPipeline::SetBlendEnable(u32 renderTargetIndex, bool bEnable)
{
	assert(m_PipelineDesc.blendStates.size() > renderTargetIndex);
	m_PipelineDesc.blendStates[renderTargetIndex].blendEnable = bEnable ? VK_TRUE : VK_FALSE;

	return *this;
}

GraphicsPipeline& VulkanGraphicsPipeline::SetColorBlending(u32 renderTargetIndex, eBlendFactor srcBlend, eBlendFactor dstBlend, eBlendOp blendOp)
{
	assert(m_PipelineDesc.blendStates.size() > renderTargetIndex);
	m_PipelineDesc.blendStates[renderTargetIndex].blendEnable = VK_TRUE;
	m_PipelineDesc.blendStates[renderTargetIndex].srcColorBlendFactor = VK_PIPELINE_BLENDFACTOR(srcBlend);
	m_PipelineDesc.blendStates[renderTargetIndex].srcColorBlendFactor = VK_PIPELINE_BLENDFACTOR(dstBlend);
	m_PipelineDesc.blendStates[renderTargetIndex].colorBlendOp        = VK_PIPELINE_BLENDOP(blendOp);

	return *this;
}

GraphicsPipeline& VulkanGraphicsPipeline::SetAlphaBlending(u32 renderTargetIndex, eBlendFactor srcBlend, eBlendFactor dstBlend, eBlendOp blendOp)
{
	assert(m_PipelineDesc.blendStates.size() > renderTargetIndex);
	m_PipelineDesc.blendStates[renderTargetIndex].blendEnable         = VK_TRUE;
	m_PipelineDesc.blendStates[renderTargetIndex].srcAlphaBlendFactor = VK_PIPELINE_BLENDFACTOR(srcBlend);
	m_PipelineDesc.blendStates[renderTargetIndex].srcAlphaBlendFactor = VK_PIPELINE_BLENDFACTOR(dstBlend);
	m_PipelineDesc.blendStates[renderTargetIndex].alphaBlendOp        = VK_PIPELINE_BLENDOP(blendOp);

	return *this;
}

GraphicsPipeline& VulkanGraphicsPipeline::SetLogicOp(eLogicOp logicOp)
{
	m_PipelineDesc.blendLogicOp = VK_PIPELINE_LOGICOP(logicOp);
	return *this;
}

void VulkanGraphicsPipeline::Build()
{
	auto& rm = m_RenderDevice.GetResourceManager();
	auto& rhiSceneResource = static_cast<VkSceneResource&>(rm.GetSceneResource());

	// **
	// Create shader stages
	// **
	std::vector< VkPipelineShaderStageCreateInfo > shaderStages;
	std::vector< VkPushConstantRange >             pushConstants;
	if (m_bMeshShader)
	{
		auto ms = StaticCast<VulkanShader>(m_pMS);
		auto ts = StaticCast<VulkanShader>(m_pTS);
		assert(ms);


		VkPipelineShaderStageCreateInfo msStageCreateInfo = {};
		msStageCreateInfo.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		msStageCreateInfo.stage  = VK_SHADER_STAGE_MESH_BIT_EXT;
		msStageCreateInfo.module = ms->vkModule();
		msStageCreateInfo.pName  = "main";
		shaderStages.push_back(msStageCreateInfo);

		if (ts)
		{
			VkPipelineShaderStageCreateInfo tsStageCreateInfo = {};
			tsStageCreateInfo.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			tsStageCreateInfo.stage  = VK_SHADER_STAGE_TASK_BIT_EXT;
			tsStageCreateInfo.module = ts->vkModule();
			tsStageCreateInfo.pName  = "main";
			shaderStages.push_back(tsStageCreateInfo);
		}

		// **
		// Construct descriptor-set layout
		// **
		i32 maxSet = -1;
		std::unordered_map< u32, std::unordered_map< u32, VkDescriptorSetLayoutBinding > > descriptorSetLayoutBindingMap;

		// vs
		const auto& msReflection = ms->Reflection();
		for (const auto& [set, infos] : msReflection.descriptors)
		{
			for (const auto& info : infos)
			{
				VkDescriptorSetLayoutBinding layoutBinding = {};
				layoutBinding.binding         = info.binding;
				layoutBinding.descriptorCount = 1;
				layoutBinding.descriptorType  = info.descriptorType;
				layoutBinding.stageFlags      = VK_SHADER_STAGE_MESH_BIT_EXT;
				descriptorSetLayoutBindingMap[set].emplace(info.binding, layoutBinding);

				m_ResourceBindingMap.emplace(info.name, (static_cast<u64>(set) << 32) | info.binding);
			}

			maxSet = maxSet < (i32)set ? (i32)set : maxSet;
		}

		if (ts)
		{
			const auto& tsReflection = ts->Reflection();
			for (const auto& [set, infos] : tsReflection.descriptors)
			{
				for (const auto& info : infos)
				{
					if (descriptorSetLayoutBindingMap[set].contains(info.binding))
					{
						descriptorSetLayoutBindingMap[set][info.binding].stageFlags |= VK_SHADER_STAGE_TASK_BIT_NV;
					}
					else
					{
						VkDescriptorSetLayoutBinding layoutBinding = {};
						layoutBinding.binding         = info.binding;
						layoutBinding.descriptorCount = 1;
						layoutBinding.descriptorType  = info.descriptorType;
						layoutBinding.stageFlags      = VK_SHADER_STAGE_TASK_BIT_NV;
						descriptorSetLayoutBindingMap[set].emplace(info.binding, layoutBinding);

						m_ResourceBindingMap.emplace(info.name, (static_cast<u64>(set) << 32) | info.binding);
					}
				}

				maxSet = maxSet < (i32)set ? (i32)set : maxSet;
			}
		}

		m_vkSetLayouts.clear();
		m_vkSetLayouts.resize(maxSet + 1);
		for (const auto& [set, binding] : descriptorSetLayoutBindingMap)
		{
			if (set == eDescriptorSet_Static)
			{
				m_vkSetLayouts[eDescriptorSet_Static] = rhiSceneResource.GetSceneDescriptorSetLayout();
				continue;
			}

			std::vector< VkDescriptorSetLayoutBinding > bindings;
			for (auto& [_, info] : descriptorSetLayoutBindingMap[set])
			{
				assert((info.descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER && set == eDescriptorSet_Push)
					&& "All uniform-buffers should be placed in push descriptor set!");
				bindings.push_back(info);
			}

			/** Descriptor set layout **/
			VkDescriptorSetLayoutCreateInfo layoutInfo = {};
			layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			layoutInfo.bindingCount = static_cast<u32>(bindings.size());
			layoutInfo.pBindings = bindings.data();
			if (set == eDescriptorSet_Push)
			{
				layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
			}
			VK_CHECK(vkCreateDescriptorSetLayout(m_RenderDevice.vkDevice(), &layoutInfo, nullptr, &m_vkSetLayouts[set]));

			if (set > eDescriptorSet_Push)
			{
				auto& descriptorSet = m_RenderDevice.AllocateDescriptorSet(m_vkSetLayouts[set]);
				m_DescriptorTable.emplace(set, descriptorSet);
			}
		}

		for (auto& vkSetLayout : m_vkSetLayouts)
		{
			if (vkSetLayout == VK_NULL_HANDLE)
				vkSetLayout = m_RenderDevice.GetEmptyDescriptorSetLayout();
		}

		// **
		// Push constants
		// **
		const auto& msResourceInfo = ms->Reflection();
		pushConstants.append_range(msResourceInfo.pushConstants);
		if (ts)
		{
			const auto& reflection = ts->Reflection();
			pushConstants.append_range(reflection.pushConstants);
		}
	}
	else
	{
		auto vs = StaticCast<VulkanShader>(m_pVS);
		auto ps = StaticCast<VulkanShader>(m_pPS);
		auto gs = StaticCast<VulkanShader>(m_pGS);
		auto hs = StaticCast<VulkanShader>(m_pHS);
		auto ds = StaticCast<VulkanShader>(m_pDS);
		assert(vs && ps);

		VkPipelineShaderStageCreateInfo vsStageCreateInfo = {};
		vsStageCreateInfo.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		vsStageCreateInfo.stage  = VK_SHADER_STAGE_VERTEX_BIT;
		vsStageCreateInfo.module = vs->vkModule();
		vsStageCreateInfo.pName  = "main";
		shaderStages.push_back(vsStageCreateInfo);

		VkPipelineShaderStageCreateInfo fsStageCreateInfo = {};
		fsStageCreateInfo.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		fsStageCreateInfo.stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
		fsStageCreateInfo.module = ps->vkModule();
		fsStageCreateInfo.pName  = "main";
		shaderStages.push_back(fsStageCreateInfo);

		if (gs)
		{
			VkPipelineShaderStageCreateInfo gsStageCreateInfo = {};
			gsStageCreateInfo.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			gsStageCreateInfo.stage  = VK_SHADER_STAGE_GEOMETRY_BIT;
			gsStageCreateInfo.module = gs->vkModule();
			gsStageCreateInfo.pName  = "main";
			shaderStages.push_back(gsStageCreateInfo);
		}
		if (hs)
		{
			VkPipelineShaderStageCreateInfo hsStageCreateInfo = {};
			hsStageCreateInfo.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			hsStageCreateInfo.stage  = VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
			hsStageCreateInfo.module = hs->vkModule();
			hsStageCreateInfo.pName  = "main";
			shaderStages.push_back(hsStageCreateInfo);
		}
		if (ds)
		{
			VkPipelineShaderStageCreateInfo dsStageCreateInfo = {};
			dsStageCreateInfo.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			dsStageCreateInfo.stage  = VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
			dsStageCreateInfo.module = ds->vkModule();
			dsStageCreateInfo.pName  = "main";
			shaderStages.push_back(dsStageCreateInfo);
		}


		// **
		// Construct descriptor-set layout
		// **
		i32 maxSet = -1;
		std::unordered_map< u32, std::unordered_map< u32, VkDescriptorSetLayoutBinding > > descriptorSetLayoutBindingMap;

		// vs
		const auto& vsReflection = vs->Reflection();
		for (const auto& [set, infos] : vsReflection.descriptors)
		{
			for (const auto& info : infos)
			{
				VkDescriptorSetLayoutBinding layoutBinding = {};
				layoutBinding.binding         = info.binding;
				layoutBinding.descriptorCount = 1;
				layoutBinding.descriptorType  = info.descriptorType;
				layoutBinding.stageFlags      = VK_SHADER_STAGE_VERTEX_BIT;
				descriptorSetLayoutBindingMap[set].emplace(info.binding, layoutBinding);

				m_ResourceBindingMap.emplace(info.name, (static_cast<u64>(set) << 32) | info.binding);
			}

			maxSet = maxSet < (i32)set ? (i32)set : maxSet;
		}

		// ps
		{
			const auto& fsReflection = ps->Reflection();
			for (const auto& [set, infos] : fsReflection.descriptors)
			{
				for (const auto& info : infos)
				{
					if (descriptorSetLayoutBindingMap[set].contains(info.binding))
					{
						descriptorSetLayoutBindingMap[set][info.binding].stageFlags |= VK_SHADER_STAGE_FRAGMENT_BIT;
					}
					else
					{
						VkDescriptorSetLayoutBinding layoutBinding = {};
						layoutBinding.binding         = info.binding;
						layoutBinding.descriptorCount = 1;
						layoutBinding.descriptorType  = info.descriptorType;
						layoutBinding.stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
						descriptorSetLayoutBindingMap[set].emplace(info.binding, layoutBinding);

						m_ResourceBindingMap.emplace(info.name, (static_cast<u64>(set) << 32) | info.binding);
					}
				}

				maxSet = maxSet < (i32)set ? (i32)set : maxSet;
			}
		}

		// gs
		if (gs)
		{
			const auto& gsReflection = gs->Reflection();
			for (const auto& [set, infos] : gsReflection.descriptors)
			{
				for (const auto& info : infos)
				{
					if (descriptorSetLayoutBindingMap[set].contains(info.binding))
					{
						descriptorSetLayoutBindingMap[set][info.binding].stageFlags |= VK_SHADER_STAGE_GEOMETRY_BIT;
					}
					else
					{
						VkDescriptorSetLayoutBinding layoutBinding = {};
						layoutBinding.binding         = info.binding;
						layoutBinding.descriptorCount = 1;
						layoutBinding.descriptorType  = info.descriptorType;
						layoutBinding.stageFlags      = VK_SHADER_STAGE_GEOMETRY_BIT;
						descriptorSetLayoutBindingMap[set].emplace(info.binding, layoutBinding);

						m_ResourceBindingMap.emplace(info.name, (static_cast<u64>(set) << 32) | info.binding);
					}
				}

				maxSet = maxSet < (i32)set ? (i32)set : maxSet;
			}
		}

		// hs
		if (hs)
		{
			const auto& hsReflection = hs->Reflection();
			for (const auto& [set, infos] : hsReflection.descriptors)
			{
				for (const auto& info : infos)
				{
					if (descriptorSetLayoutBindingMap[set].contains(info.binding))
					{
						descriptorSetLayoutBindingMap[set][info.binding].stageFlags |= VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
					}
					else
					{
						VkDescriptorSetLayoutBinding layoutBinding = {};
						layoutBinding.binding         = info.binding;
						layoutBinding.descriptorCount = 1;
						layoutBinding.descriptorType  = info.descriptorType;
						layoutBinding.stageFlags      = VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
						descriptorSetLayoutBindingMap[set].emplace(info.binding, layoutBinding);

						m_ResourceBindingMap.emplace(info.name, (static_cast<u64>(set) << 32) | info.binding);
					}
				}

				maxSet = maxSet < (i32)set ? (i32)set : maxSet;
			}
		}

		// ds
		if (ds)
		{
			const auto& dsReflection = ds->Reflection();
			for (const auto& [set, infos] : dsReflection.descriptors)
			{
				for (const auto& info : infos)
				{
					if (descriptorSetLayoutBindingMap[set].contains(info.binding))
					{
						descriptorSetLayoutBindingMap[set][info.binding].stageFlags |= VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
					}
					else
					{
						VkDescriptorSetLayoutBinding layoutBinding = {};
						layoutBinding.binding         = info.binding;
						layoutBinding.descriptorCount = 1;
						layoutBinding.descriptorType  = info.descriptorType;
						layoutBinding.stageFlags      = VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
						descriptorSetLayoutBindingMap[set].emplace(info.binding, layoutBinding);

						m_ResourceBindingMap.emplace(info.name, (static_cast<u64>(set) << 32) | info.binding);
					}
				}

				maxSet = maxSet < (i32)set ? (i32)set : maxSet;
			}
		}

		m_vkSetLayouts.clear();
		m_vkSetLayouts.resize(maxSet + 1);
		for (const auto& [set, binding] : descriptorSetLayoutBindingMap)
		{
			if (set == eDescriptorSet_Static)
			{
				m_vkSetLayouts[eDescriptorSet_Static] = rhiSceneResource.GetSceneDescriptorSetLayout();
				continue;
			}

			std::vector< VkDescriptorSetLayoutBinding > bindings;
			for (auto& [_, info] : descriptorSetLayoutBindingMap[set])
			{
				assert((info.descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER && set == eDescriptorSet_Push)
					&& "All uniform-buffers should be placed in push descriptor set!");
				bindings.push_back(info);
			}

			/** Descriptor set layout **/
			VkDescriptorSetLayoutCreateInfo layoutInfo = {};
			layoutInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			layoutInfo.bindingCount = static_cast<u32>(bindings.size());
			layoutInfo.pBindings    = bindings.data();
			if (set == eDescriptorSet_Push)
			{
				layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
			}
			VK_CHECK(vkCreateDescriptorSetLayout(m_RenderDevice.vkDevice(), &layoutInfo, nullptr, &m_vkSetLayouts[set]));

			if (set > eDescriptorSet_Push)
			{
				auto& descriptorSet = m_RenderDevice.AllocateDescriptorSet(m_vkSetLayouts[set]);
				m_DescriptorTable.emplace(set, descriptorSet);
			}
		}

		for (auto& vkSetLayout : m_vkSetLayouts)
		{
			if (vkSetLayout == VK_NULL_HANDLE)
				vkSetLayout = m_RenderDevice.GetEmptyDescriptorSetLayout();
		}

		// **
		// Push constants
		// **
		const auto& vsResourceInfo = vs->Reflection();
		pushConstants.append_range(vsResourceInfo.pushConstants);
		if (ps)
		{
			const auto& reflection = ps->Reflection();
			pushConstants.append_range(reflection.pushConstants);
		}
		if (gs)
		{
			const auto& reflection = gs->Reflection();
			pushConstants.append_range(reflection.pushConstants);
		}
		if (hs)
		{
			const auto& reflection = hs->Reflection();
			pushConstants.append_range(reflection.pushConstants);
		}
		if (ds)
		{
			const auto& reflection = ds->Reflection();
			pushConstants.append_range(reflection.pushConstants);
		}
	}


	// **
	// Create pipeline layout
	// **
	VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
	pipelineLayoutInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutInfo.setLayoutCount         = static_cast<u32>(m_vkSetLayouts.size());
	pipelineLayoutInfo.pSetLayouts            = m_vkSetLayouts.data();
	pipelineLayoutInfo.pushConstantRangeCount = static_cast<u32>(pushConstants.size());
	pipelineLayoutInfo.pPushConstantRanges    = pushConstants.data();
	VK_CHECK(vkCreatePipelineLayout(m_RenderDevice.vkDevice(), &pipelineLayoutInfo, nullptr, &m_vkPipelineLayout));


	// **
	// Descriptor template
	// **
	//for (u32 set = 0; set < eNumDescriptorSet; ++set)
	//{
	//	std::vector< VkDescriptorSetLayoutBinding > bindings;
	//	for (auto& [binding, info] : descriptorSetLayoutBindingMap[set])
	//		bindings.push_back(info);

	//	/** Descriptor Template Entries **/
	//	u32 offset = 0;
	//	std::vector< VkDescriptorUpdateTemplateEntry > entries(bindings.size());
	//	for (u32 i = 0; i < bindings.size(); ++i)
	//	{
	//		VkDescriptorUpdateTemplateEntry& entry = entries[i];
	//		entry.dstBinding = bindings[i].binding;
	//		entry.dstArrayElement = 0;
	//		entry.descriptorCount = bindings[i].descriptorCount;
	//		entry.descriptorType = bindings[i].descriptorType;
	//		entry.offset = offset;
	//		entry.stride = sizeof(DescriptorInfo);

	//		offset += sizeof(DescriptorInfo) * bindings[i].descriptorCount;
	//	}

	//	VkDescriptorUpdateTemplateCreateInfo templateCreateInfo = {};
	//	templateCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_UPDATE_TEMPLATE_CREATE_INFO;
	//	templateCreateInfo.descriptorUpdateEntryCount = static_cast<u32>(entries.size());
	//	templateCreateInfo.pDescriptorUpdateEntries = entries.data();
	//	templateCreateInfo.templateType = set > eDescriptorSet_PerFrame ? 
	//		VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_PUSH_DESCRIPTORS_KHR : VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_DESCRIPTOR_SET;
	//	templateCreateInfo.descriptorSetLayout = m_vkSetLayouts[set];
	//	templateCreateInfo.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	//	templateCreateInfo.pipelineLayout = m_vkPipelineLayout;
	//	templateCreateInfo.set = set;
	//	VK_CHECK(vkCreateDescriptorUpdateTemplate(m_RenderDevice.vkDevice(), &templateCreateInfo, nullptr, &m_vkDescriptorTemplate));
	//}


	// **
	// Pipeline
	// **

	// Pipeline cache
	std::string filename   = std::string(m_Name) + ".cache";
	const bool bCacheExist = FileIO::FileExist(PIPELINE_PATH.string() + filename);

	VkPipelineCache vkPipelineCache     = VK_NULL_HANDLE;
	VkPipelineCacheCreateInfo cacheInfo = {};
	cacheInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
	if (bCacheExist) 
	{
		FileIO::Data cacheData = FileIO::ReadBinary(PIPELINE_PATH.string() + filename);

		const auto cacheHeader = (VkPipelineCacheHeaderVersionOne*)cacheData.data;
		if (cacheHeader->deviceID == m_RenderDevice.DeviceProps().deviceID &&
			cacheHeader->vendorID == m_RenderDevice.DeviceProps().vendorID &&
			memcmp(cacheHeader->pipelineCacheUUID, m_RenderDevice.DeviceProps().pipelineCacheUUID, VK_UUID_SIZE) == 0)
		{
			cacheInfo.initialDataSize = cacheData.size;
			cacheInfo.pInitialData    = cacheData.data;
		}
		
		delete cacheHeader;
	}
	VK_CHECK(vkCreatePipelineCache(m_RenderDevice.vkDevice(), &cacheInfo, nullptr, &vkPipelineCache));

	// dynamic state
	std::vector< VkDynamicState > dynamicStates = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	VkPipelineDynamicStateCreateInfo dynamicStateInfo = {};
	dynamicStateInfo.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamicStateInfo.dynamicStateCount = static_cast<u32>(dynamicStates.size());
	dynamicStateInfo.pDynamicStates    = dynamicStates.data();

	VkPipelineColorBlendStateCreateInfo colorBlendingInfo = 
	{
		.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
		.logicOp         = m_PipelineDesc.blendLogicOp,
		.attachmentCount = static_cast<u32>(m_PipelineDesc.blendStates.size()),
		.pAttachments    = m_PipelineDesc.blendStates.data(),
		.blendConstants  = { 0.f, 0.f, 0.f, 0.f }
	};

	// Pipeline
	VkGraphicsPipelineCreateInfo pipelineInfo = {};
	pipelineInfo.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipelineInfo.stageCount          = static_cast<u32>(shaderStages.size());
	pipelineInfo.pStages             = shaderStages.data();
	pipelineInfo.pVertexInputState   = &m_PipelineDesc.vertexInputInfo;
	pipelineInfo.pInputAssemblyState = &m_PipelineDesc.inputAssemblyInfo;
	pipelineInfo.pViewportState      = &m_PipelineDesc.viewportStateInfo;
	pipelineInfo.pRasterizationState = &m_PipelineDesc.rasterizerInfo;
	pipelineInfo.pMultisampleState   = &m_PipelineDesc.multisamplingInfo;
	pipelineInfo.pDepthStencilState  = &m_PipelineDesc.depthStencilInfo;
	pipelineInfo.pColorBlendState    = &colorBlendingInfo;
	pipelineInfo.pDynamicState       = &dynamicStateInfo;
	pipelineInfo.layout              = m_vkPipelineLayout;
	pipelineInfo.renderPass          = m_PipelineDesc.renderPass;
	pipelineInfo.subpass             = 0;
	pipelineInfo.basePipelineIndex   = -1;
	VK_CHECK(vkCreateGraphicsPipelines(m_RenderDevice.vkDevice(), vkPipelineCache, 1, &pipelineInfo, nullptr, &m_vkPipeline));


	// **
	// Clean up
	// **
	if (!bCacheExist && vkPipelineCache)
	{
		FileIO::Data writeData = {};
		VK_CHECK(vkGetPipelineCacheData(m_RenderDevice.vkDevice(), vkPipelineCache, &writeData.size, nullptr));

		writeData.data = _aligned_malloc(writeData.size, 64);
		VK_CHECK(vkGetPipelineCacheData(m_RenderDevice.vkDevice(), vkPipelineCache, &writeData.size, writeData.data));

		FileIO::WriteBinary(PIPELINE_PATH.string(), filename, writeData);
		writeData.Deallocate();
	}
	vkDestroyPipelineCache(m_RenderDevice.vkDevice(), vkPipelineCache, nullptr);
}


//-------------------------------------------------------------------------
// Compute Pipeline
//-------------------------------------------------------------------------
VulkanComputePipeline::VulkanComputePipeline(VkRenderDevice& rd, const std::string& name)
	: render::ComputePipeline(name)
	, m_RenderDevice(rd)
{
}

VulkanComputePipeline::~VulkanComputePipeline()
{
	vkDestroyPipeline(m_RenderDevice.vkDevice(), m_vkPipeline, nullptr);
	vkDestroyPipelineLayout(m_RenderDevice.vkDevice(), m_vkPipelineLayout, nullptr);
	for (u32 i = 1; i < m_vkSetLayouts.size(); ++i)
	{
		if (m_vkSetLayouts[i] == m_RenderDevice.GetEmptyDescriptorSetLayout())
			continue;

		vkDestroyDescriptorSetLayout(m_RenderDevice.vkDevice(), m_vkSetLayouts[i], nullptr);
	}
}

void VulkanComputePipeline::Build()
{
	auto& rm = m_RenderDevice.GetResourceManager();
	auto& rhiSceneResource = static_cast<VkSceneResource&>(rm.GetSceneResource());

	// **
	// Construct descriptor-set layout
	// **
	auto vkCS = StaticCast<VulkanShader>(m_pCS);
	assert(vkCS);

	i32 maxSet = -1;
	std::unordered_map< u32, std::unordered_map< u32, VkDescriptorSetLayoutBinding > > descriptorSetLayoutBindingMap;

	// vs
	const auto& csReflection = vkCS->Reflection();
	for (const auto& [set, infos] : csReflection.descriptors)
	{
		for (const auto& info : infos)
		{
			VkDescriptorSetLayoutBinding layoutBinding = {};
			layoutBinding.binding         = info.binding;
			layoutBinding.descriptorCount = 1;
			layoutBinding.descriptorType  = info.descriptorType;
			layoutBinding.stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
			descriptorSetLayoutBindingMap[set].emplace(info.binding, layoutBinding);

			m_ResourceBindingMap.emplace(info.name, (static_cast<u64>(set) << 32) | info.binding);
		}

		maxSet = maxSet < (i32)set ? (i32)set : maxSet;
	}

	m_vkSetLayouts.clear();
	m_vkSetLayouts.resize(maxSet + 1);
	for (const auto& [set, binding] : descriptorSetLayoutBindingMap)
	{
		if (set == eDescriptorSet_Static)
		{
			m_vkSetLayouts[eDescriptorSet_Static] = rhiSceneResource.GetSceneDescriptorSetLayout();
			continue;
		}

		std::vector< VkDescriptorSetLayoutBinding > bindings;
		for (auto& [_, info] : descriptorSetLayoutBindingMap[set])
		{
			assert((info.descriptorType != VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER || set == eDescriptorSet_Push)
				&& "All uniform-buffers should be placed in push descriptor set!");
			bindings.push_back(info);
		}

		/** Descriptor set layout **/
		VkDescriptorSetLayoutCreateInfo layoutInfo = {};
		layoutInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		layoutInfo.bindingCount = static_cast<u32>(bindings.size());
		layoutInfo.pBindings    = bindings.data();
		if (set == eDescriptorSet_Push)
		{
			layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
		}
		VK_CHECK(vkCreateDescriptorSetLayout(m_RenderDevice.vkDevice(), &layoutInfo, nullptr, &m_vkSetLayouts[set]));

		if (set > eDescriptorSet_Push)
		{
			auto& descriptorSet = m_RenderDevice.AllocateDescriptorSet(m_vkSetLayouts[set]);
			m_DescriptorTable.emplace(set, descriptorSet);
		}
	}

	for (auto& vkSetLayout : m_vkSetLayouts)
	{
		if (vkSetLayout == VK_NULL_HANDLE)
			vkSetLayout = m_RenderDevice.GetEmptyDescriptorSetLayout();
	}

	// **
	// Push constants
	// **
	std::vector< VkPushConstantRange > pushConstants;
	const auto& resourceInfo = vkCS->Reflection();
	pushConstants.append_range(resourceInfo.pushConstants);


	// **
	// Create pipeline layout
	// **
	VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
	pipelineLayoutInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutInfo.setLayoutCount         = static_cast<u32>(m_vkSetLayouts.size());
	pipelineLayoutInfo.pSetLayouts            = m_vkSetLayouts.data();
	pipelineLayoutInfo.pushConstantRangeCount = static_cast<u32>(pushConstants.size());
	pipelineLayoutInfo.pPushConstantRanges    = pushConstants.data();
	VK_CHECK(vkCreatePipelineLayout(m_RenderDevice.vkDevice(), &pipelineLayoutInfo, nullptr, &m_vkPipelineLayout));

	// **
	// Pipeline
	// **

	// Pipeline cache
	std::string filename   = std::string(m_Name) + ".cache";
	const bool bCacheExist = FileIO::FileExist(PIPELINE_PATH.string() + filename);

	VkPipelineCache vkPipelineCache     = VK_NULL_HANDLE;
	VkPipelineCacheCreateInfo cacheInfo = {};
	cacheInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
	if (bCacheExist)
	{
		FileIO::Data cacheData = FileIO::ReadBinary(PIPELINE_PATH.string() + filename);

		const auto cacheHeader = (VkPipelineCacheHeaderVersionOne*)cacheData.data;
		if (cacheHeader->deviceID == m_RenderDevice.DeviceProps().deviceID &&
			cacheHeader->vendorID == m_RenderDevice.DeviceProps().vendorID &&
			memcmp(cacheHeader->pipelineCacheUUID, m_RenderDevice.DeviceProps().pipelineCacheUUID, VK_UUID_SIZE) == 0)
		{
			cacheInfo.initialDataSize = cacheData.size;
			cacheInfo.pInitialData = cacheData.data;
		}

		delete cacheHeader;
	}
	VK_CHECK(vkCreatePipelineCache(m_RenderDevice.vkDevice(), &cacheInfo, nullptr, &vkPipelineCache));

	VkComputePipelineCreateInfo pipelineInfo = {};
	pipelineInfo.sType              = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
	pipelineInfo.stage.sType        = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	pipelineInfo.stage.stage        = VK_SHADER_STAGE_COMPUTE_BIT;
	pipelineInfo.stage.module       = vkCS->vkModule();
	pipelineInfo.stage.pName        = "main";
	pipelineInfo.layout             = m_vkPipelineLayout;
	pipelineInfo.basePipelineHandle = nullptr;
	pipelineInfo.basePipelineIndex  = 0;
	VK_CHECK(vkCreateComputePipelines(m_RenderDevice.vkDevice(), nullptr, 1, &pipelineInfo, nullptr, &m_vkPipeline));


	// **
	// Clean up
	// **
	if (!bCacheExist && vkPipelineCache)
	{
		FileIO::Data writeData = {};
		VK_CHECK(vkGetPipelineCacheData(m_RenderDevice.vkDevice(), vkPipelineCache, &writeData.size, nullptr));

		writeData.data = _aligned_malloc(writeData.size, 64);
		VK_CHECK(vkGetPipelineCacheData(m_RenderDevice.vkDevice(), vkPipelineCache, &writeData.size, writeData.data));

		FileIO::WriteBinary(PIPELINE_PATH.string(), filename, writeData);
		writeData.Deallocate();
	}
	vkDestroyPipelineCache(m_RenderDevice.vkDevice(), vkPipelineCache, nullptr);
}

} // namespace vk
