#include "RendererPch.h"
#include "VkRenderPipeline.h"
#include "VkResourceManager.h"
#include "RenderResource/VkRenderTarget.h"
#include "RenderResource/VkSceneResource.h"
#include "Utils/FileIO.hpp"

#include <unordered_set>

namespace vk
{

//-------------------------------------------------------------------------
// Graphics Pipeline
//-------------------------------------------------------------------------
GraphicsPipeline::GraphicsPipeline(RenderDevice& device, std::string name)
	: m_RenderDevice(device)
	, m_Name(std::move(name))
{
	// **
	// Set default values
	// **

	// input
	m_PipelineDesc.vertexInputInfo = {};
	m_PipelineDesc.vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	m_PipelineDesc.inputAssemblyInfo = {};
	m_PipelineDesc.inputAssemblyInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	m_PipelineDesc.inputAssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

	// viewport
	m_PipelineDesc.viewportStateInfo = {};
	m_PipelineDesc.viewportStateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	m_PipelineDesc.viewportStateInfo.viewportCount = 1;
	m_PipelineDesc.viewportStateInfo.scissorCount = 1;

	// rasterizer
	m_PipelineDesc.rasterizerInfo = {};
	m_PipelineDesc.rasterizerInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	m_PipelineDesc.rasterizerInfo.polygonMode = VK_POLYGON_MODE_FILL;
	m_PipelineDesc.rasterizerInfo.cullMode = VK_CULL_MODE_BACK_BIT;
	m_PipelineDesc.rasterizerInfo.frontFace = VK_FRONT_FACE_CLOCKWISE;
	m_PipelineDesc.rasterizerInfo.lineWidth = 1.f;

	// depth-stencil
	m_PipelineDesc.depthStencilInfo = {};
	m_PipelineDesc.depthStencilInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	m_PipelineDesc.depthStencilInfo.depthTestEnable = VK_TRUE;
	m_PipelineDesc.depthStencilInfo.depthWriteEnable = VK_FALSE; // False as default. Since depth is mainly writable by depth pre-pass only.
	m_PipelineDesc.depthStencilInfo.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
	m_PipelineDesc.depthStencilInfo.minDepthBounds = 0.f;
	m_PipelineDesc.depthStencilInfo.maxDepthBounds = 1.f;

	// multi-sampling
	m_PipelineDesc.multisamplingInfo = {};
	m_PipelineDesc.multisamplingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	m_PipelineDesc.multisamplingInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
	m_PipelineDesc.multisamplingInfo.minSampleShading = 1.f;
}

GraphicsPipeline::~GraphicsPipeline()
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

GraphicsPipeline& GraphicsPipeline::SetShaders(
	Arc< Shader > vs,
	Arc< Shader > fs,
	Arc< Shader > gs, 
	Arc< Shader > hs, 
	Arc< Shader > ds)
{
	auto pVS = vs; assert(pVS);
	auto pFS = fs;
	auto pGS = gs;
	auto pHS = hs;
	auto pDS = ds;

	// **
	// Create shader stages
	// **
	m_PipelineDesc.shaderStages.clear();

	VkPipelineShaderStageCreateInfo vsStageCreateInfo = {};
	vsStageCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	vsStageCreateInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
	vsStageCreateInfo.module = pVS->vkModule();
	vsStageCreateInfo.pName = "main";
	m_PipelineDesc.shaderStages.push_back(vsStageCreateInfo);

	VkPipelineShaderStageCreateInfo fsStageCreateInfo = {};
	fsStageCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	fsStageCreateInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	fsStageCreateInfo.module = pFS ? pFS->vkModule() : VK_NULL_HANDLE;
	fsStageCreateInfo.pName = "main";
	m_PipelineDesc.shaderStages.push_back(fsStageCreateInfo);

	if (pGS)
	{
		VkPipelineShaderStageCreateInfo gsStageCreateInfo = {};
		gsStageCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		gsStageCreateInfo.stage = VK_SHADER_STAGE_GEOMETRY_BIT;
		gsStageCreateInfo.module = pGS->vkModule();
		gsStageCreateInfo.pName = "main";
		m_PipelineDesc.shaderStages.push_back(gsStageCreateInfo);
	}
	if (pHS)
	{
		VkPipelineShaderStageCreateInfo hsStageCreateInfo = {};
		hsStageCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		hsStageCreateInfo.stage = VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
		hsStageCreateInfo.module = pHS->vkModule();
		hsStageCreateInfo.pName = "main";
		m_PipelineDesc.shaderStages.push_back(hsStageCreateInfo);
	}
	if (pDS)
	{
		VkPipelineShaderStageCreateInfo dsStageCreateInfo = {};
		dsStageCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		dsStageCreateInfo.stage = VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
		dsStageCreateInfo.module = pDS->vkModule();
		dsStageCreateInfo.pName = "main";
		m_PipelineDesc.shaderStages.push_back(dsStageCreateInfo);
	}


	// **
	// Construct descriptor-set layout
	// **
	i32 maxSet = -1;
	std::unordered_map< u32, std::unordered_map< u32, VkDescriptorSetLayoutBinding > > descriptorSetLayoutBindingMap;

	// vs
	const auto& vsReflection = pVS->Reflection();
	for (const auto& [set, infos] : vsReflection.descriptors)
	{
		for (const auto& info : infos)
		{
			VkDescriptorSetLayoutBinding layoutBinding = {};
			layoutBinding.binding = info.binding;
			layoutBinding.descriptorCount = 1;
			layoutBinding.descriptorType = info.descriptorType;
			layoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
			descriptorSetLayoutBindingMap[set].emplace(info.binding, layoutBinding);
		}

		maxSet = maxSet < (i32)set ? (i32)set : maxSet;
	}

	// fs
	if (pFS)
	{
		const auto& fsReflection = pFS->Reflection();
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
					layoutBinding.binding = info.binding;
					layoutBinding.descriptorCount = 1;
					layoutBinding.descriptorType = info.descriptorType;
					layoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
					descriptorSetLayoutBindingMap[set].emplace(info.binding, layoutBinding);
				}
			}

			maxSet = maxSet < (i32)set ? (i32)set : maxSet;
		}
	}

	// gs
	if (pGS)
	{
		const auto& gsReflection = pGS->Reflection();
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
					layoutBinding.binding = info.binding;
					layoutBinding.descriptorCount = 1;
					layoutBinding.descriptorType = info.descriptorType;
					layoutBinding.stageFlags = VK_SHADER_STAGE_GEOMETRY_BIT;
					descriptorSetLayoutBindingMap[set].emplace(info.binding, layoutBinding);
				}
			}

			maxSet = maxSet < (i32)set ? (i32)set : maxSet;
		}
	}

	// hs
	if (pHS)
	{
		const auto& hsReflection = pHS->Reflection();
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
					layoutBinding.binding = info.binding;
					layoutBinding.descriptorCount = 1;
					layoutBinding.descriptorType = info.descriptorType;
					layoutBinding.stageFlags = VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
					descriptorSetLayoutBindingMap[set].emplace(info.binding, layoutBinding);
				}
			}

			maxSet = maxSet < (i32)set ? (i32)set : maxSet;
		}
	}

	// ds
	if (pDS)
	{
		const auto& dsReflection = pDS->Reflection();
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
					layoutBinding.binding = info.binding;
					layoutBinding.descriptorCount = 1;
					layoutBinding.descriptorType = info.descriptorType;
					layoutBinding.stageFlags = VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
					descriptorSetLayoutBindingMap[set].emplace(info.binding, layoutBinding);
				}
			}

			maxSet = maxSet < (i32)set ? (i32)set : maxSet;
		}
	}

	m_vkSetLayouts.clear();
	m_vkSetLayouts.resize(maxSet + 1);
	for (const auto&[set, binding] : descriptorSetLayoutBindingMap)
	{
		if (set == eDescriptorSet_Static)
		{
			m_vkSetLayouts[eDescriptorSet_Static] = g_FrameData.pSceneResource->GetSceneDescriptorSetLayout();
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
	std::vector< VkPushConstantRange > pushConstants;
	const auto& vsResourceInfo = pVS->Reflection();
	pushConstants.append_range(vsResourceInfo.pushConstants);
	if (pFS)
	{
		const auto& reflection = pFS->Reflection();
		pushConstants.append_range(reflection.pushConstants);
	}
	if (pGS)
	{
		const auto& reflection = pGS->Reflection();
		pushConstants.append_range(reflection.pushConstants);
	}
	if (pHS)
	{
		const auto& reflection = pHS->Reflection();
		pushConstants.append_range(reflection.pushConstants);
	}
	if (pDS)
	{
		const auto& reflection = pDS->Reflection();
		pushConstants.append_range(reflection.pushConstants);
	}


	// **
	// Create pipeline layout
	// **
	VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
	pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutInfo.setLayoutCount = static_cast<u32>(m_vkSetLayouts.size());
	pipelineLayoutInfo.pSetLayouts = m_vkSetLayouts.data();
	pipelineLayoutInfo.pushConstantRangeCount = static_cast<u32>(pushConstants.size());
	pipelineLayoutInfo.pPushConstantRanges = pushConstants.data();
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

	return *this;
}

GraphicsPipeline& GraphicsPipeline::SetMeshShaders(
	Arc< Shader > ms,
	Arc< Shader > ts)
{
	auto pMS = ms; assert(pMS);
	auto pTS = ts;

	// **
	// Create shader stages
	// **
	m_PipelineDesc.shaderStages.clear();

	VkPipelineShaderStageCreateInfo msStageCreateInfo = {};
	msStageCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	msStageCreateInfo.stage = VK_SHADER_STAGE_MESH_BIT_EXT;
	msStageCreateInfo.module = pMS->vkModule();
	msStageCreateInfo.pName = "main";
	m_PipelineDesc.shaderStages.push_back(msStageCreateInfo);

	if (pTS)
	{
		VkPipelineShaderStageCreateInfo tsStageCreateInfo = {};
		tsStageCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		tsStageCreateInfo.stage = VK_SHADER_STAGE_TASK_BIT_EXT;
		tsStageCreateInfo.module = pTS->vkModule();
		tsStageCreateInfo.pName = "main";
		m_PipelineDesc.shaderStages.push_back(tsStageCreateInfo);
	}

	return *this;
}

GraphicsPipeline& GraphicsPipeline::SetVertexInputs(std::vector< VkVertexInputBindingDescription >&& streams, std::vector< VkVertexInputAttributeDescription >&& attributes)
{
	m_PipelineDesc.vertexInputInfo.vertexBindingDescriptionCount = static_cast<u32>(streams.size());
	m_PipelineDesc.vertexInputInfo.pVertexBindingDescriptions = streams.data();
	m_PipelineDesc.vertexInputInfo.vertexAttributeDescriptionCount = static_cast<u32>(attributes.size());
	m_PipelineDesc.vertexInputInfo.pVertexAttributeDescriptions = attributes.data();

	return *this;
}

GraphicsPipeline& GraphicsPipeline::SetRenderTarget(const RenderTarget& renderTarget)
{
	m_PipelineDesc.renderPass = renderTarget.vkRenderPass();
	m_PipelineDesc.blendStates.resize(renderTarget.GetNumColors());
	for (auto& blendStates : m_PipelineDesc.blendStates)
	{
		blendStates.blendEnable = VK_FALSE;
		blendStates.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
		blendStates.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
		blendStates.colorBlendOp = VK_BLEND_OP_ADD;
		blendStates.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
		blendStates.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
		blendStates.alphaBlendOp = VK_BLEND_OP_ADD;
		blendStates.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	}

	return *this;
}

GraphicsPipeline& GraphicsPipeline::SetTopology(VkPrimitiveTopology topology)
{
	m_PipelineDesc.inputAssemblyInfo.topology = topology;
	return *this;
}

GraphicsPipeline& GraphicsPipeline::SetDepthTestEnable(bool bEnable)
{
	m_PipelineDesc.depthStencilInfo.depthTestEnable = bEnable;
	return *this;
}

GraphicsPipeline& GraphicsPipeline::SetDepthWriteEnable(bool bEnable)
{
	m_PipelineDesc.depthStencilInfo.depthWriteEnable = bEnable;
	return *this;
}

GraphicsPipeline& GraphicsPipeline::SetBlendEnable(u32 renderTargetIndex, bool bEnable)
{
	assert(m_PipelineDesc.blendStates.size() > renderTargetIndex);
	m_PipelineDesc.blendStates[renderTargetIndex].blendEnable = bEnable ? VK_TRUE : VK_FALSE;

	return *this;
}

GraphicsPipeline& GraphicsPipeline::SetColorBlending(u32 renderTargetIndex, VkBlendFactor srcBlend, VkBlendFactor dstBlend, VkBlendOp blendOp)
{
	assert(m_PipelineDesc.blendStates.size() > renderTargetIndex);
	m_PipelineDesc.blendStates[renderTargetIndex].blendEnable = VK_TRUE;
	m_PipelineDesc.blendStates[renderTargetIndex].srcColorBlendFactor = srcBlend;
	m_PipelineDesc.blendStates[renderTargetIndex].srcColorBlendFactor = dstBlend;
	m_PipelineDesc.blendStates[renderTargetIndex].colorBlendOp = blendOp;

	return *this;
}

GraphicsPipeline& GraphicsPipeline::SetAlphaBlending(u32 renderTargetIndex, VkBlendFactor srcBlend, VkBlendFactor dstBlend, VkBlendOp blendOp)
{
	assert(m_PipelineDesc.blendStates.size() > renderTargetIndex);
	m_PipelineDesc.blendStates[renderTargetIndex].blendEnable = VK_TRUE;
	m_PipelineDesc.blendStates[renderTargetIndex].srcAlphaBlendFactor = srcBlend;
	m_PipelineDesc.blendStates[renderTargetIndex].srcAlphaBlendFactor = dstBlend;
	m_PipelineDesc.blendStates[renderTargetIndex].alphaBlendOp = blendOp;

	return *this;
}

GraphicsPipeline& GraphicsPipeline::SetLogicOp(VkLogicOp logicOp)
{
	m_PipelineDesc.blendLogicOp = logicOp;
	return *this;
}

void GraphicsPipeline::Build()
{
	// **
	// Pipeline
	// **

	// Pipeline cache
	std::string filename = m_Name + ".cache";
	const bool bCacheExist = FileIO::FileExist(PIPELINE_PATH.string() + filename);

	VkPipelineCache vkPipelineCache = VK_NULL_HANDLE;
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

	// dynamic state
	std::vector< VkDynamicState > dynamicStates = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	VkPipelineDynamicStateCreateInfo dynamicStateInfo = {};
	dynamicStateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamicStateInfo.dynamicStateCount = static_cast<u32>(dynamicStates.size());
	dynamicStateInfo.pDynamicStates = dynamicStates.data();

	VkPipelineColorBlendStateCreateInfo colorBlendingInfo = 
	{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
		.logicOp         = m_PipelineDesc.blendLogicOp,
		.attachmentCount = static_cast<u32>(m_PipelineDesc.blendStates.size()),
		.pAttachments    = m_PipelineDesc.blendStates.data(),
		.blendConstants  = { 0.f, 0.f, 0.f, 0.f }
	};

	// Pipeline
	VkGraphicsPipelineCreateInfo pipelineInfo = {};
	pipelineInfo.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipelineInfo.stageCount          = static_cast<u32>(m_PipelineDesc.shaderStages.size());
	pipelineInfo.pStages             = m_PipelineDesc.shaderStages.data();
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
ComputePipeline::ComputePipeline(RenderDevice& device, std::string name)
	: m_RenderDevice(device)
	, m_Name(name)
{
}

ComputePipeline::~ComputePipeline()
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

ComputePipeline& ComputePipeline::SetComputeShader(Arc<Shader> pCS)
{
	assert(pCS);
	m_pCS = pCS;


	// **
	// Construct descriptor-set layout
	// **
	i32 maxSet = -1;
	std::unordered_map< u32, std::unordered_map< u32, VkDescriptorSetLayoutBinding > > descriptorSetLayoutBindingMap;

	// vs
	const auto& csReflection = m_pCS->Reflection();
	for (const auto& [set, infos] : csReflection.descriptors)
	{
		for (const auto& info : infos)
		{
			VkDescriptorSetLayoutBinding layoutBinding = {};
			layoutBinding.binding = info.binding;
			layoutBinding.descriptorCount = 1;
			layoutBinding.descriptorType = info.descriptorType;
			layoutBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
			descriptorSetLayoutBindingMap[set].emplace(info.binding, layoutBinding);
		}

		maxSet = maxSet < (i32)set ? (i32)set : maxSet;
	}

	m_vkSetLayouts.clear();
	m_vkSetLayouts.resize(maxSet + 1);
	for (const auto& [set, binding] : descriptorSetLayoutBindingMap)
	{
		if (set == eDescriptorSet_Static)
		{
			m_vkSetLayouts[eDescriptorSet_Static] = g_FrameData.pSceneResource->GetSceneDescriptorSetLayout();
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
	std::vector< VkPushConstantRange > pushConstants;
	const auto& resourceInfo = m_pCS->Reflection();
	pushConstants.append_range(resourceInfo.pushConstants);


	// **
	// Create pipeline layout
	// **
	VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
	pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutInfo.setLayoutCount = static_cast<u32>(m_vkSetLayouts.size());
	pipelineLayoutInfo.pSetLayouts = m_vkSetLayouts.data();
	pipelineLayoutInfo.pushConstantRangeCount = static_cast<u32>(pushConstants.size());
	pipelineLayoutInfo.pPushConstantRanges = pushConstants.data();
	VK_CHECK(vkCreatePipelineLayout(m_RenderDevice.vkDevice(), &pipelineLayoutInfo, nullptr, &m_vkPipelineLayout));

	return *this;
}

void ComputePipeline::Build()
{
	// **
	// Pipeline
	// **

	// Pipeline cache
	std::string filename   = m_Name + ".cache";
	const bool bCacheExist = FileIO::FileExist(PIPELINE_PATH.string() + filename);

	VkPipelineCache vkPipelineCache = VK_NULL_HANDLE;
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
	pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
	pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	pipelineInfo.stage.module = m_pCS->vkModule();
	pipelineInfo.stage.pName = "main";
	pipelineInfo.layout = m_vkPipelineLayout;
	pipelineInfo.basePipelineHandle = nullptr;
	pipelineInfo.basePipelineIndex = 0;
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
