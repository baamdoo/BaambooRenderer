#include "RendererPch.h"
#include "VkRenderPipeline.h"
#include "VkResourceManager.h"
#include "RenderResource/VkRenderTarget.h"
#include "BaambooUtils/FileIO.hpp"

namespace vk
{

//-------------------------------------------------------------------------
// Graphics Pipeline
//-------------------------------------------------------------------------
GraphicsPipeline::GraphicsPipeline(RenderContext& context, std::string name)
	: m_renderContext(context)
	, m_name(std::move(name))
{
	// **
	// Set default values
	// **

	// input
	m_createInfos.vertexInputInfo = {};
	m_createInfos.vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	m_createInfos.inputAssemblyInfo = {};
	m_createInfos.inputAssemblyInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	m_createInfos.inputAssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

	// viewport
	m_createInfos.viewportStateInfo = {};
	m_createInfos.viewportStateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	m_createInfos.viewportStateInfo.viewportCount = 1;
	m_createInfos.viewportStateInfo.scissorCount = 1;

	// rasterizer
	m_createInfos.rasterizerInfo = {};
	m_createInfos.rasterizerInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	m_createInfos.rasterizerInfo.polygonMode = VK_POLYGON_MODE_FILL;
	m_createInfos.rasterizerInfo.cullMode = VK_CULL_MODE_BACK_BIT;
	m_createInfos.rasterizerInfo.frontFace = VK_FRONT_FACE_CLOCKWISE;
	m_createInfos.rasterizerInfo.lineWidth = 1.f;

	// depth-stencil
	m_createInfos.depthStencilInfo = {};
	m_createInfos.depthStencilInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	m_createInfos.depthStencilInfo.depthTestEnable = VK_TRUE;
	m_createInfos.depthStencilInfo.depthWriteEnable = VK_FALSE; // False as default. Since depth is mainly writable by depth pre-pass only.
	m_createInfos.depthStencilInfo.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
	m_createInfos.depthStencilInfo.minDepthBounds = 0.f;
	m_createInfos.depthStencilInfo.maxDepthBounds = 1.f;

	// multi-sampling
	m_createInfos.multisamplingInfo = {};
	m_createInfos.multisamplingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	m_createInfos.multisamplingInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
	m_createInfos.multisamplingInfo.minSampleShading = 1.f;
}

GraphicsPipeline::~GraphicsPipeline()
{
	vkDestroyPipeline(m_renderContext.vkDevice(), m_vkPipeline, nullptr);
	vkDestroyPipelineLayout(m_renderContext.vkDevice(), m_vkPipelineLayout, nullptr);
	for (u32 i = 0; i < eNumDescriptorSet; ++i)
		vkDestroyDescriptorSetLayout(m_renderContext.vkDevice(), m_vkSetLayouts[i], nullptr);
}

GraphicsPipeline& GraphicsPipeline::SetShaders(
	baamboo::ResourceHandle< Shader > vs,
	baamboo::ResourceHandle< Shader > fs,
	baamboo::ResourceHandle< Shader > gs, 
	baamboo::ResourceHandle< Shader > hs, 
	baamboo::ResourceHandle< Shader > ds)
{
	auto& rm = m_renderContext.GetResourceManager();
	auto pVS = rm.Get(vs); assert(pVS);
	auto pFS = rm.Get(fs);
	auto pGS = rm.Get(gs);
	auto pHS = rm.Get(hs);
	auto pDS = rm.Get(ds);

	// **
	// Create shader stages
	// **
	m_createInfos.shaderStages.clear();

	VkPipelineShaderStageCreateInfo vsStageCreateInfo = {};
	vsStageCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	vsStageCreateInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
	vsStageCreateInfo.module = pVS->vkModule();
	vsStageCreateInfo.pName = "main";
	m_createInfos.shaderStages.push_back(vsStageCreateInfo);

	VkPipelineShaderStageCreateInfo fsStageCreateInfo = {};
	fsStageCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	fsStageCreateInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	fsStageCreateInfo.module = pFS ? pFS->vkModule() : VK_NULL_HANDLE;
	fsStageCreateInfo.pName = "main";
	m_createInfos.shaderStages.push_back(fsStageCreateInfo);

	if (pGS)
	{
		VkPipelineShaderStageCreateInfo gsStageCreateInfo = {};
		gsStageCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		gsStageCreateInfo.stage = VK_SHADER_STAGE_GEOMETRY_BIT;
		gsStageCreateInfo.module = pGS->vkModule();
		gsStageCreateInfo.pName = "main";
		m_createInfos.shaderStages.push_back(gsStageCreateInfo);
	}
	if (pHS)
	{
		VkPipelineShaderStageCreateInfo hsStageCreateInfo = {};
		hsStageCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		hsStageCreateInfo.stage = VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
		hsStageCreateInfo.module = pHS->vkModule();
		hsStageCreateInfo.pName = "main";
		m_createInfos.shaderStages.push_back(hsStageCreateInfo);
	}
	if (pDS)
	{
		VkPipelineShaderStageCreateInfo dsStageCreateInfo = {};
		dsStageCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		dsStageCreateInfo.stage = VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
		dsStageCreateInfo.module = pDS->vkModule();
		dsStageCreateInfo.pName = "main";
		m_createInfos.shaderStages.push_back(dsStageCreateInfo);
	}


	// **
	// Construct descriptor-set layout
	// **
	std::unordered_map< u32, VkDescriptorSetLayoutBinding > descriptorSetLayoutBindingMap[eNumDescriptorSet];
	for (u32 set = 0; set < eNumDescriptorSet; ++set)
	{
		// vs
		const auto& vsReflection = pVS->Reflection();
		for (auto& [binding, info] : vsReflection.descriptors[set])
		{
			VkDescriptorSetLayoutBinding layoutBinding = {};
			layoutBinding.binding = binding;
			layoutBinding.descriptorCount = set == eDescriptorSet_Static ? MAX_BINDLESS_DESCRIPTOR_RESOURCE_SIZE : 1;
			layoutBinding.descriptorType = info.descriptorType;
			layoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
			descriptorSetLayoutBindingMap[set].emplace(binding, layoutBinding);
		}

		// fs
		if (pFS)
		{
			const auto& fsReflection = pFS->Reflection();
			for (auto& [binding, info] : fsReflection.descriptors[set])
			{
				if (descriptorSetLayoutBindingMap[set].contains(binding))
				{
					descriptorSetLayoutBindingMap[set][binding].stageFlags |= VK_SHADER_STAGE_FRAGMENT_BIT;
				}
				else
				{
					VkDescriptorSetLayoutBinding layoutBinding = {};
					layoutBinding.binding = binding;
					layoutBinding.descriptorCount = set == eDescriptorSet_Static ? MAX_BINDLESS_DESCRIPTOR_RESOURCE_SIZE : 1;
					layoutBinding.descriptorType = info.descriptorType;
					layoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
					descriptorSetLayoutBindingMap[set].emplace(binding, layoutBinding);
				}
			}
		}

		// gs
		if (pGS)
		{
			const auto& gsReflection = pGS->Reflection();
			for (auto& [binding, info] : gsReflection.descriptors[set])
			{
				if (descriptorSetLayoutBindingMap[set].contains(binding))
				{
					descriptorSetLayoutBindingMap[set][binding].stageFlags |= VK_SHADER_STAGE_GEOMETRY_BIT;
				}
				else
				{
					VkDescriptorSetLayoutBinding layoutBinding = {};
					layoutBinding.binding = binding;
					layoutBinding.descriptorCount = set == eDescriptorSet_Static ? MAX_BINDLESS_DESCRIPTOR_RESOURCE_SIZE : 1;
					layoutBinding.descriptorType = info.descriptorType;
					layoutBinding.stageFlags = VK_SHADER_STAGE_GEOMETRY_BIT;
					descriptorSetLayoutBindingMap[set].emplace(binding, layoutBinding);
				}
			}
		}

		// hs
		if (pHS)
		{
			const auto& hsReflection = pHS->Reflection();
			for (auto& [binding, info] : hsReflection.descriptors[set])
			{
				if (descriptorSetLayoutBindingMap[set].contains(binding))
				{
					descriptorSetLayoutBindingMap[set][binding].stageFlags |= VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
				}
				else
				{
					VkDescriptorSetLayoutBinding layoutBinding = {};
					layoutBinding.binding = binding;
					layoutBinding.descriptorCount = set == eDescriptorSet_Static ? MAX_BINDLESS_DESCRIPTOR_RESOURCE_SIZE : 1;
					layoutBinding.descriptorType = info.descriptorType;
					layoutBinding.stageFlags = VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
					descriptorSetLayoutBindingMap[set].emplace(binding, layoutBinding);
				}
			}
		}

		// ds
		if (pDS)
		{
			const auto& dsReflection = pDS->Reflection();
			for (auto& [binding, info] : dsReflection.descriptors[set])
			{
				if (descriptorSetLayoutBindingMap[set].contains(binding))
				{
					descriptorSetLayoutBindingMap[set][binding].stageFlags |= VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
				}
				else
				{
					VkDescriptorSetLayoutBinding layoutBinding = {};
					layoutBinding.binding = binding;
					layoutBinding.descriptorCount = 1;
					layoutBinding.descriptorType = info.descriptorType;
					layoutBinding.stageFlags = VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
					descriptorSetLayoutBindingMap[set].emplace(binding, layoutBinding);
				}
			}
		}
	}

	for (u32 set = 0; set < eNumDescriptorSet; ++set)
	{
		std::vector< VkDescriptorSetLayoutBinding > bindings;
		for (auto& [binding, info] : descriptorSetLayoutBindingMap[set])
			bindings.push_back(info);

		/** Descriptor set layout **/
		VkDescriptorSetLayoutCreateInfo layoutInfo = {};
		layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		layoutInfo.bindingCount = static_cast<u32>(bindings.size());
		layoutInfo.pBindings = bindings.data();
		if (set == eDescriptorSet_Static)
		{
			VkDescriptorBindingFlags flags = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT_EXT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT_EXT;
		
			VkDescriptorSetLayoutBindingFlagsCreateInfoEXT bindingFlagsInfo = {};
			bindingFlagsInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO_EXT;
			bindingFlagsInfo.bindingCount = static_cast<u32>(bindings.size());
			bindingFlagsInfo.pBindingFlags = &flags;
		
			layoutInfo.pNext = &bindingFlagsInfo;
			layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT_EXT;
		}
		else if (set == eDescriptorSet_Push)
		{
			layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
		}
		
		VK_CHECK(vkCreateDescriptorSetLayout(m_renderContext.vkDevice(), &layoutInfo, nullptr, &m_vkSetLayouts[set]));
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
	pipelineLayoutInfo.setLayoutCount = eNumDescriptorSet;
	pipelineLayoutInfo.pSetLayouts = m_vkSetLayouts;
	pipelineLayoutInfo.pushConstantRangeCount = static_cast<u32>(pushConstants.size());
	pipelineLayoutInfo.pPushConstantRanges = pushConstants.data();
	VK_CHECK(vkCreatePipelineLayout(m_renderContext.vkDevice(), &pipelineLayoutInfo, nullptr, &m_vkPipelineLayout));


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
	//	VK_CHECK(vkCreateDescriptorUpdateTemplate(m_renderContext.vkDevice(), &templateCreateInfo, nullptr, &m_vkDescriptorTemplate));
	//}

	return *this;
}

GraphicsPipeline& GraphicsPipeline::SetMeshShaders(
	baamboo::ResourceHandle<Shader> ms,
	baamboo::ResourceHandle<Shader> ts)
{
	auto& rm = m_renderContext.GetResourceManager();
	auto pMS = rm.Get(ms); assert(pMS);
	auto pTS = rm.Get(ts);

	// **
	// Create shader stages
	// **
	m_createInfos.shaderStages.clear();

	VkPipelineShaderStageCreateInfo msStageCreateInfo = {};
	msStageCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	msStageCreateInfo.stage = VK_SHADER_STAGE_MESH_BIT_EXT;
	msStageCreateInfo.module = pMS->vkModule();
	msStageCreateInfo.pName = "main";
	m_createInfos.shaderStages.push_back(msStageCreateInfo);

	if (pTS)
	{
		VkPipelineShaderStageCreateInfo tsStageCreateInfo = {};
		tsStageCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		tsStageCreateInfo.stage = VK_SHADER_STAGE_TASK_BIT_EXT;
		tsStageCreateInfo.module = pTS->vkModule();
		tsStageCreateInfo.pName = "main";
		m_createInfos.shaderStages.push_back(tsStageCreateInfo);
	}

	return *this;
}

GraphicsPipeline& GraphicsPipeline::SetVertexInputs(std::vector< VkVertexInputBindingDescription >&& streams, std::vector< VkVertexInputAttributeDescription >&& attributes)
{
	m_createInfos.vertexInputInfo.vertexBindingDescriptionCount = static_cast<u32>(streams.size());
	m_createInfos.vertexInputInfo.pVertexBindingDescriptions = streams.data();
	m_createInfos.vertexInputInfo.vertexAttributeDescriptionCount = static_cast<u32>(attributes.size());
	m_createInfos.vertexInputInfo.pVertexAttributeDescriptions = attributes.data();

	return *this;
}

GraphicsPipeline& GraphicsPipeline::SetRenderTarget(const RenderTarget& renderTarget)
{
	m_createInfos.renderPass = renderTarget.vkRenderPass();
	m_createInfos.blendStates.resize(renderTarget.GetNumColours());
	for (auto& blendStates : m_createInfos.blendStates)
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
	m_createInfos.inputAssemblyInfo.topology = topology;
	return *this;
}

GraphicsPipeline& GraphicsPipeline::SetDepthTestEnable(bool bEnable)
{
	m_createInfos.depthStencilInfo.depthTestEnable = bEnable;
	return *this;
}

GraphicsPipeline& GraphicsPipeline::SetDepthWriteEnable(bool bEnable)
{
	m_createInfos.depthStencilInfo.depthWriteEnable = bEnable;
	return *this;
}

GraphicsPipeline& GraphicsPipeline::SetBlendEnable(u32 renderTargetIndex, bool bEnable)
{
	assert(m_createInfos.blendStates.size() > renderTargetIndex);
	m_createInfos.blendStates[renderTargetIndex].blendEnable = bEnable ? VK_TRUE : VK_FALSE;

	return *this;
}

GraphicsPipeline& GraphicsPipeline::SetColorBlending(u32 renderTargetIndex, VkBlendFactor srcBlend, VkBlendFactor dstBlend, VkBlendOp blendOp)
{
	assert(m_createInfos.blendStates.size() > renderTargetIndex);
	m_createInfos.blendStates[renderTargetIndex].blendEnable = VK_TRUE;
	m_createInfos.blendStates[renderTargetIndex].srcColorBlendFactor = srcBlend;
	m_createInfos.blendStates[renderTargetIndex].srcColorBlendFactor = dstBlend;
	m_createInfos.blendStates[renderTargetIndex].colorBlendOp = blendOp;

	return *this;
}

GraphicsPipeline& GraphicsPipeline::SetAlphaBlending(u32 renderTargetIndex, VkBlendFactor srcBlend, VkBlendFactor dstBlend, VkBlendOp blendOp)
{
	assert(m_createInfos.blendStates.size() > renderTargetIndex);
	m_createInfos.blendStates[renderTargetIndex].blendEnable = VK_TRUE;
	m_createInfos.blendStates[renderTargetIndex].srcAlphaBlendFactor = srcBlend;
	m_createInfos.blendStates[renderTargetIndex].srcAlphaBlendFactor = dstBlend;
	m_createInfos.blendStates[renderTargetIndex].alphaBlendOp = blendOp;

	return *this;
}

GraphicsPipeline& GraphicsPipeline::SetLogicOp(VkLogicOp logicOp)
{
	m_createInfos.blendLogicOp = logicOp;
	return *this;
}

void GraphicsPipeline::Build()
{
	// **
	// Pipeline
	// **

	// Pipeline cache
	std::string filename = m_name + ".cache";
	const bool bCacheExist = FileIO::FileExist(PIPELINE_PATH.string() + filename);

	VkPipelineCache vkPipelineCache = VK_NULL_HANDLE;
	VkPipelineCacheCreateInfo cacheInfo = {};
	cacheInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
	if (bCacheExist) 
	{
		FileIO::Data cacheData = FileIO::ReadBinary(PIPELINE_PATH.string() + filename);

		const auto cacheHeader = (VkPipelineCacheHeaderVersionOne*)cacheData.data;
		if (cacheHeader->deviceID == m_renderContext.DeviceProps().deviceID &&
			cacheHeader->vendorID == m_renderContext.DeviceProps().vendorID &&
			memcmp(cacheHeader->pipelineCacheUUID, m_renderContext.DeviceProps().pipelineCacheUUID, VK_UUID_SIZE) == 0)
		{
			cacheInfo.initialDataSize = cacheData.size;
			cacheInfo.pInitialData = cacheData.data;
		}
		
		delete cacheHeader;
	}
	VK_CHECK(vkCreatePipelineCache(m_renderContext.vkDevice(), &cacheInfo, nullptr, &vkPipelineCache));

	// dynamic state
	std::vector< VkDynamicState > dynamicStates = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	VkPipelineDynamicStateCreateInfo dynamicStateInfo = {};
	dynamicStateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamicStateInfo.dynamicStateCount = static_cast<u32>(dynamicStates.size());
	dynamicStateInfo.pDynamicStates = dynamicStates.data();

	VkPipelineColorBlendStateCreateInfo colorBlendingInfo = 
	{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
		.logicOp = m_createInfos.blendLogicOp,
		.attachmentCount = static_cast<u32>(m_createInfos.blendStates.size()),
		.pAttachments = m_createInfos.blendStates.data(),
		.blendConstants = { 0.f, 0.f, 0.f, 0.f }
	};

	// Pipeline
	VkGraphicsPipelineCreateInfo pipelineInfo = {};
	pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipelineInfo.stageCount = static_cast<u32>(m_createInfos.shaderStages.size());
	pipelineInfo.pStages = m_createInfos.shaderStages.data();
	pipelineInfo.pVertexInputState = &m_createInfos.vertexInputInfo;
	pipelineInfo.pInputAssemblyState = &m_createInfos.inputAssemblyInfo;
	pipelineInfo.pViewportState = &m_createInfos.viewportStateInfo;
	pipelineInfo.pRasterizationState = &m_createInfos.rasterizerInfo;
	pipelineInfo.pMultisampleState = &m_createInfos.multisamplingInfo;
	pipelineInfo.pDepthStencilState = &m_createInfos.depthStencilInfo;
	pipelineInfo.pColorBlendState = &colorBlendingInfo;
	pipelineInfo.pDynamicState = &dynamicStateInfo;
	pipelineInfo.layout = m_vkPipelineLayout;
	pipelineInfo.renderPass = m_createInfos.renderPass;
	pipelineInfo.subpass = 0;
	pipelineInfo.basePipelineIndex = -1;
	VK_CHECK(vkCreateGraphicsPipelines(m_renderContext.vkDevice(), vkPipelineCache, 1, &pipelineInfo, nullptr, &m_vkPipeline));


	// **
	// Clean up
	// **
	if (!bCacheExist && vkPipelineCache)
	{
		FileIO::Data writeData = {};
		VK_CHECK(vkGetPipelineCacheData(m_renderContext.vkDevice(), vkPipelineCache, &writeData.size, nullptr));

		writeData.data = _aligned_malloc(writeData.size, 64);
		VK_CHECK(vkGetPipelineCacheData(m_renderContext.vkDevice(), vkPipelineCache, &writeData.size, writeData.data));

		FileIO::WriteBinary(PIPELINE_PATH.string(), filename, writeData);
		writeData.Deallocate();
	}
	vkDestroyPipelineCache(m_renderContext.vkDevice(), vkPipelineCache, nullptr);
}


//-------------------------------------------------------------------------
// Compute Pipeline
//-------------------------------------------------------------------------
ComputePipeline::ComputePipeline(RenderContext& context)
	: m_renderContext(context)
{
}

ComputePipeline::~ComputePipeline()
{
	vkDestroyPipeline(m_renderContext.vkDevice(), m_vkPipeline, nullptr);
	vkDestroyPipelineLayout(m_renderContext.vkDevice(), m_vkPipelineLayout, nullptr);
}

} // namespace vk
