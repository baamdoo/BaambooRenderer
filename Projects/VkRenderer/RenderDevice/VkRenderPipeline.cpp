#include "RendererPch.h"
#include "VkRenderPipeline.h"
#include "RenderResource/VkRenderTarget.h"
#include "RenderResource/VkTexture.h"
#include "Utils/FileIO.hpp"

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <new>

namespace vk
{

using namespace render;

namespace
{

bool IsSucceeded(VkResult result, VkPipeline vkCurrent)
{
	if (result != VK_SUCCESS && vkCurrent == VK_NULL_HANDLE)
		VK_CHECK(result);

	return result == VK_SUCCESS;
}

bool IsCompatiblePipelineCache(const FileIO::Data& cacheData, const VkPhysicalDeviceProperties& deviceProps)
{
	if (!cacheData.data || cacheData.size < sizeof(VkPipelineCacheHeaderVersionOne))
		return false;

	VkPipelineCacheHeaderVersionOne header = {};
	std::memcpy(&header, cacheData.data, sizeof(header));
	return header.headerSize == sizeof(VkPipelineCacheHeaderVersionOne) &&
		header.headerSize <= cacheData.size &&
		header.headerVersion == VK_PIPELINE_CACHE_HEADER_VERSION_ONE &&
		header.vendorID == deviceProps.vendorID &&
		header.deviceID == deviceProps.deviceID &&
		std::memcmp(header.pipelineCacheUUID, deviceProps.pipelineCacheUUID, VK_UUID_SIZE) == 0;
}

VkPipelineCache CreatePipelineCache(VkRenderDevice& renderDevice, const std::string& filename)
{
	FileIO::Data cacheData = FileIO::ReadBinary((PIPELINE_PATH / filename).string());

	VkPipelineCacheCreateInfo cacheInfo = {};
	cacheInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
	if (IsCompatiblePipelineCache(cacheData, renderDevice.DeviceProps()))
	{
		cacheInfo.initialDataSize = static_cast<std::size_t>(cacheData.size);
		cacheInfo.pInitialData = cacheData.data;
	}

	VkPipelineCache pipelineCache = VK_NULL_HANDLE;
	const VkResult result = vkCreatePipelineCache(renderDevice.vkDevice(), &cacheInfo, nullptr, &pipelineCache);
	cacheData.Deallocate();
	VK_CHECK(result);
	return pipelineCache;
}

void WritePipelineCache(VkRenderDevice& renderDevice, VkPipelineCache pipelineCache, const std::string& filename)
{
	for (;;)
	{
		std::size_t cacheSize = 0;
		const VkResult sizeResult =
			vkGetPipelineCacheData(renderDevice.vkDevice(), pipelineCache, &cacheSize, nullptr);
		if (sizeResult != VK_SUCCESS)
		{
			VK_CHECK(sizeResult);
			return;
		}
		if (cacheSize == 0)
			return;

		FileIO::Data writeData = {};
		writeData.data = std::malloc(cacheSize);
		if (!writeData.data)
			throw std::bad_alloc();
		writeData.size = static_cast<u64>(cacheSize);

		const VkResult dataResult =
			vkGetPipelineCacheData(renderDevice.vkDevice(), pipelineCache, &cacheSize, writeData.data);
		if (dataResult == VK_SUCCESS)
		{
			writeData.size = static_cast<u64>(cacheSize);
			try
			{
				FileIO::WriteBinary(PIPELINE_PATH.string(), filename, writeData);
			}
			catch (...)
			{
				writeData.Deallocate();
				throw;
			}
			writeData.Deallocate();
			return;
		}

		writeData.Deallocate();
		if (dataResult != VK_INCOMPLETE)
		{
			VK_CHECK(dataResult);
			return;
		}
	}
}

VkPipelineColorBlendAttachmentState MakeDefaultBlendState()
{
	VkPipelineColorBlendAttachmentState state = {};
	state.blendEnable         = VK_FALSE;
	state.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
	state.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
	state.colorBlendOp        = VK_BLEND_OP_ADD;
	state.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	state.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
	state.alphaBlendOp        = VK_BLEND_OP_ADD;
	state.colorWriteMask      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
		VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	return state;
}
} // namespace

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
VulkanGraphicsPipeline::VulkanGraphicsPipeline(VkRenderDevice& rd, const char* name)
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
	m_PipelineDesc.depthStencilInfo.depthCompareOp   = VK_COMPARE_OP_GREATER;
	m_PipelineDesc.depthStencilInfo.minDepthBounds   = 0.f;
	m_PipelineDesc.depthStencilInfo.maxDepthBounds   = 1.f;

	// multi-sampling
	m_PipelineDesc.multisamplingInfo                      = {};
	m_PipelineDesc.multisamplingInfo.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	m_PipelineDesc.multisamplingInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
	m_PipelineDesc.multisamplingInfo.minSampleShading     = 1.f;

	m_PipelineDesc.blendStates.assign(eAttachmentPoint::NumColorAttachments, MakeDefaultBlendState());
}

VulkanGraphicsPipeline::~VulkanGraphicsPipeline()
{
	m_RenderDevice.DeferRelease(m_vkPipeline);
}

GraphicsPipeline& VulkanGraphicsPipeline::SetRenderTarget(Arc< render::RenderTarget > pRenderTarget)
{
	auto rhiRenderTarget = StaticCast<VulkanRenderTarget>(pRenderTarget);
	assert(rhiRenderTarget);

	m_pRenderTarget = rhiRenderTarget;
	m_PipelineDesc.colorAttachmentCount = rhiRenderTarget->GetNumColors();
	BB_ASSERT(m_PipelineDesc.colorAttachmentCount <= m_PipelineDesc.blendStates.size(),
		"Vulkan render target exposes too many color attachment slots (%u).",
		m_PipelineDesc.colorAttachmentCount);

	VkSampleCountFlagBits sampleCount = VK_SAMPLE_COUNT_1_BIT;
	bool bHasAttachment = false;
	for (const auto& attachment : rhiRenderTarget->GetAttachments())
	{
		auto texture = StaticCast<VulkanTexture>(attachment);
		if (!texture)
			continue;

		if (!bHasAttachment)
		{
			sampleCount = texture->Desc().samples;
			bHasAttachment = true;
		}
		else
		{
			BB_ASSERT(sampleCount == texture->Desc().samples,
				"Vulkan render target attachments must use the same sample count.");
		}
	}
	m_PipelineDesc.multisamplingInfo.rasterizationSamples = sampleCount;

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
	m_PipelineDesc.blendStates[renderTargetIndex].blendEnable = bEnable ? VK_TRUE : VK_FALSE;

	return *this;
}

GraphicsPipeline& VulkanGraphicsPipeline::SetColorBlending(u32 renderTargetIndex, eBlendFactor srcBlend, eBlendFactor dstBlend, eBlendOp blendOp)
{
	m_PipelineDesc.blendStates[renderTargetIndex].blendEnable = VK_TRUE;
	m_PipelineDesc.blendStates[renderTargetIndex].srcColorBlendFactor = VK_PIPELINE_BLENDFACTOR(srcBlend);
	m_PipelineDesc.blendStates[renderTargetIndex].dstColorBlendFactor = VK_PIPELINE_BLENDFACTOR(dstBlend);
	m_PipelineDesc.blendStates[renderTargetIndex].colorBlendOp        = VK_PIPELINE_BLENDOP(blendOp);

	return *this;
}

GraphicsPipeline& VulkanGraphicsPipeline::SetAlphaBlending(u32 renderTargetIndex, eBlendFactor srcBlend, eBlendFactor dstBlend, eBlendOp blendOp)
{
	m_PipelineDesc.blendStates[renderTargetIndex].blendEnable         = VK_TRUE;
	m_PipelineDesc.blendStates[renderTargetIndex].srcAlphaBlendFactor = VK_PIPELINE_BLENDFACTOR(srcBlend);
	m_PipelineDesc.blendStates[renderTargetIndex].dstAlphaBlendFactor = VK_PIPELINE_BLENDFACTOR(dstBlend);
	m_PipelineDesc.blendStates[renderTargetIndex].alphaBlendOp        = VK_PIPELINE_BLENDOP(blendOp);

	return *this;
}

GraphicsPipeline& VulkanGraphicsPipeline::SetLogicOp(eLogicOp logicOp)
{
	BB_ASSERT(logicOp == eLogicOp::None || m_RenderDevice.DeviceFeatures().logicOp,
		"Vulkan logic operations are not supported by the selected physical device.");

	m_PipelineDesc.bLogicOpEnabled = logicOp == eLogicOp::None ? VK_FALSE : VK_TRUE;
	m_PipelineDesc.blendLogicOp    = VK_PIPELINE_LOGICOP(logicOp);
	return *this;
}

bool VulkanGraphicsPipeline::Build()
{
	auto vs = StaticCast<VulkanShader>(m_pVS);
	auto ms = StaticCast<VulkanShader>(m_pMS);
	auto ps = StaticCast<VulkanShader>(m_pPS);
	auto ts = StaticCast<VulkanShader>(m_pTS);
	assert((m_bMeshShader ? ms : vs) && ps);

	// **
	// Create shader stages
	// **
	std::vector< VkPipelineShaderStageCreateInfo > shaderStages;

	if (m_bMeshShader)
	{
		VkPipelineShaderStageCreateInfo msStageCreateInfo = {};
		msStageCreateInfo.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		msStageCreateInfo.stage  = VK_SHADER_STAGE_MESH_BIT_EXT;
		msStageCreateInfo.module = ms->vkModule();
		msStageCreateInfo.pName  = "main";
		shaderStages.push_back(msStageCreateInfo);
	}
	else
	{
		// vertexInputInfo stays empty: vertex shaders fetch their data from the heap by SV_VertexID
		VkPipelineShaderStageCreateInfo vsStageCreateInfo = {};
		vsStageCreateInfo.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		vsStageCreateInfo.stage  = VK_SHADER_STAGE_VERTEX_BIT;
		vsStageCreateInfo.module = vs->vkModule();
		vsStageCreateInfo.pName  = "main";
		shaderStages.push_back(vsStageCreateInfo);
	}

	VkPipelineShaderStageCreateInfo fsStageCreateInfo = {};
	fsStageCreateInfo.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	fsStageCreateInfo.stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
	fsStageCreateInfo.module = ps->vkModule();
	fsStageCreateInfo.pName  = "main";
	shaderStages.push_back(fsStageCreateInfo);

	if (ts)
	{
		VkPipelineShaderStageCreateInfo tsStageCreateInfo = {};
		tsStageCreateInfo.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		tsStageCreateInfo.stage  = VK_SHADER_STAGE_TASK_BIT_EXT;
		tsStageCreateInfo.module = ts->vkModule();
		tsStageCreateInfo.pName  = "main";
		shaderStages.push_back(tsStageCreateInfo);
	}

	std::unordered_map< std::string, u64 > bindingMap;
	const auto AddPushOffsets = [&bindingMap](const VulkanShader& shader)
		{
			const auto& bindings = *shader.GetCreationInfo().bindings;
			BB_ASSERT(bindings.pushBlockSize <= render::kSlangPushBlockBytes, "PushBlockSize is overflowed!");

			for (const auto& [name, offset] : bindings.pushOffsets)
			{
				bindingMap.emplace(name, u64(offset) << 32llu);
			}
		};
	AddPushOffsets(m_bMeshShader ? *ms : *vs);
	AddPushOffsets(*ps);
	if (ts) AddPushOffsets(*ts);


	// **
	// Pipeline
	// **

	// Pipeline cache
	std::string filename   = std::string(m_Name) + ".cache";
	const VkPipelineCache vkPipelineCache = CreatePipelineCache(m_RenderDevice, filename);

	// dynamic state
	std::vector< VkDynamicState > dynamicStates = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	VkPipelineDynamicStateCreateInfo dynamicStateInfo = {};
	dynamicStateInfo.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamicStateInfo.dynamicStateCount = static_cast<u32>(dynamicStates.size());
	dynamicStateInfo.pDynamicStates    = dynamicStates.data();

	VkPipelineColorBlendStateCreateInfo colorBlendingInfo = 
	{
		.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
		.logicOpEnable   = m_PipelineDesc.bLogicOpEnabled,
		.logicOp         = m_PipelineDesc.blendLogicOp,
		.attachmentCount = m_PipelineDesc.colorAttachmentCount,
		.pAttachments    = m_PipelineDesc.blendStates.data(),
		.blendConstants  = { 0.f, 0.f, 0.f, 0.f }
	};

	if (m_PipelineDesc.bLogicOpEnabled)
	{
		for (u32 i = 0; i < m_PipelineDesc.colorAttachmentCount; ++i)
		{
			BB_ASSERT(!m_PipelineDesc.blendStates[i].blendEnable,
				"Vulkan color attachment %u cannot enable blending and a logic operation simultaneously.", i);
		}
	}

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
	pipelineInfo.layout              = m_RenderDevice.GetGlobalPipelineLayout();
	pipelineInfo.renderPass          = m_pRenderTarget ? m_pRenderTarget->vkRenderPass() : VK_NULL_HANDLE; // a resized render target has a new pass
	pipelineInfo.subpass             = 0;
	pipelineInfo.basePipelineIndex   = -1;

	// **
	// Clean up
	// **
	VkPipeline vkPipeline = VK_NULL_HANDLE;
	const VkResult result = vkCreateGraphicsPipelines(m_RenderDevice.vkDevice(), vkPipelineCache, 1, &pipelineInfo, nullptr, &vkPipeline);
	if (result == VK_SUCCESS)
		WritePipelineCache(m_RenderDevice, vkPipelineCache, filename);
	vkDestroyPipelineCache(m_RenderDevice.vkDevice(), vkPipelineCache, nullptr);

	if (!IsSucceeded(result, m_vkPipeline))
		return false;

	if (m_vkPipeline)
		m_RenderDevice.DeferRelease(m_vkPipeline);
	m_vkPipeline         = vkPipeline;
	m_ResourceBindingMap = std::move(bindingMap);

	++m_BuildCount;
	return true;
}


//-------------------------------------------------------------------------
// Compute Pipeline
//-------------------------------------------------------------------------
VulkanComputePipeline::VulkanComputePipeline(VkRenderDevice& rd, const char* name)
	: render::ComputePipeline(name)
	, m_RenderDevice(rd)
{
}

VulkanComputePipeline::~VulkanComputePipeline()
{
	m_RenderDevice.DeferRelease(m_vkPipeline);
}

bool VulkanComputePipeline::Build()
{
	auto vkCS = StaticCast<VulkanShader>(m_pCS);
	assert(vkCS);

	const auto& bindings = vkCS->GetCreationInfo().bindings;
	BB_ASSERT(bindings, "Compute pipeline '%s': the shader has no Slang bindings. Create it through the PipelineFactory.", m_Name.c_str());
	BB_ASSERT(bindings->pushBlockSize <= render::kSlangPushBlockBytes, "PushBlockSize is overflowed!");

	std::unordered_map< std::string, u64 > bindingMap;
	for (const auto& [name, offset] : bindings->pushOffsets)
	{
		bindingMap.emplace(name, u64(offset) << 32llu);
	}

	// Pipeline cache
	std::string filename   = std::string(m_Name) + ".cache";
	const VkPipelineCache vkPipelineCache = CreatePipelineCache(m_RenderDevice, filename);

	VkComputePipelineCreateInfo pipelineInfo = {};
	pipelineInfo.sType              = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
	pipelineInfo.stage.sType        = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	pipelineInfo.stage.stage        = VK_SHADER_STAGE_COMPUTE_BIT;
	pipelineInfo.stage.module       = vkCS->vkModule();
	pipelineInfo.stage.pName        = "main";
	pipelineInfo.layout             = m_RenderDevice.GetGlobalPipelineLayout();
	pipelineInfo.basePipelineHandle = nullptr;
	pipelineInfo.basePipelineIndex  = 0;

	// **
	// Clean up
	// **
	VkPipeline vkPipeline = VK_NULL_HANDLE;
	const VkResult result = vkCreateComputePipelines(m_RenderDevice.vkDevice(), vkPipelineCache, 1, &pipelineInfo, nullptr, &vkPipeline);
	if (result == VK_SUCCESS)
		WritePipelineCache(m_RenderDevice, vkPipelineCache, filename);
	vkDestroyPipelineCache(m_RenderDevice.vkDevice(), vkPipelineCache, nullptr);

	if (!IsSucceeded(result, m_vkPipeline))
		return false;

	if (m_vkPipeline)
		m_RenderDevice.DeferRelease(m_vkPipeline);
	m_vkPipeline         = vkPipeline;
	m_ResourceBindingMap = std::move(bindingMap);

	++m_BuildCount;
	return true;
}

} // namespace vk
