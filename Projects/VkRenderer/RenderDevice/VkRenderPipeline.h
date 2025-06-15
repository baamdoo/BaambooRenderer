#pragma once
#include "RenderResource/VkShader.h"

namespace vk
{

class RenderTarget;
class DescriptorSet;

struct DescriptorInfo
{
	union
	{
		VkDescriptorImageInfo  imageInfo;
		VkDescriptorBufferInfo bufferInfo;
	};

	DescriptorInfo(const VkDescriptorImageInfo& imageInfo_)
	{
		imageInfo = imageInfo_;

		bImage = true;
	}
	DescriptorInfo(VkSampler sampler, VkImageView imageView, VkImageLayout imageLayout)
	{
		imageInfo.sampler = sampler;
		imageInfo.imageView = imageView;
		imageInfo.imageLayout = imageLayout;

		bImage = true;
	}

	DescriptorInfo(const VkDescriptorBufferInfo& bufferInfo_)
	{
		bufferInfo = bufferInfo_;

		bImage = false;
	}
	DescriptorInfo(VkBuffer buffer, VkDeviceSize offset = 0, VkDeviceSize range = VK_WHOLE_SIZE)
	{
		bufferInfo.buffer = buffer;
		bufferInfo.offset = offset;
		bufferInfo.range = range;

		bImage = false;
	}

	bool bImage = false;
};

//-------------------------------------------------------------------------
// Graphics Pipeline
//-------------------------------------------------------------------------
class GraphicsPipeline
{
public:
	GraphicsPipeline(RenderDevice& device, std::string name);
	~GraphicsPipeline();

	GraphicsPipeline& SetShaders(
		Arc< Shader > vs, 
		Arc< Shader > fs,
		Arc< Shader > gs = Arc< Shader >(),
		Arc< Shader > hs = Arc< Shader >(),
		Arc< Shader > ds = Arc< Shader >());
	GraphicsPipeline& SetMeshShaders(
		Arc< Shader > ms,
		Arc< Shader > ts = Arc< Shader >());

	GraphicsPipeline& SetVertexInputs(std::vector< VkVertexInputBindingDescription >&& streams, std::vector< VkVertexInputAttributeDescription >&& attributes);
	GraphicsPipeline& SetRenderTarget(const RenderTarget& renderTarget);

	GraphicsPipeline& SetTopology(VkPrimitiveTopology topology);
	GraphicsPipeline& SetDepthTestEnable(bool bEnable);
	GraphicsPipeline& SetDepthWriteEnable(bool bEnable);

	GraphicsPipeline& SetBlendEnable(u32 renderTargetIndex, bool bEnable);
	GraphicsPipeline& SetColorBlending(u32 renderTargetIndex, VkBlendFactor srcBlend, VkBlendFactor dstBlend, VkBlendOp blendOp);
	GraphicsPipeline& SetAlphaBlending(u32 renderTargetIndex, VkBlendFactor srcBlend, VkBlendFactor dstBlend, VkBlendOp blendOp);
	GraphicsPipeline& SetLogicOp(VkLogicOp logicOp);

	void Build();

	[[nodiscard]]
	inline VkPipeline vkPipeline() const { return m_vkPipeline; }
	[[nodiscard]]
	inline VkPipelineLayout vkPipelineLayout() const { return m_vkPipelineLayout; }
	[[nodiscard]]
	inline VkDescriptorSetLayout vkSetLayout(u8 set) const { return m_vkSetLayouts[set]; }

private:
	RenderDevice& m_RenderDevice;
	std::string   m_Name;

	VkPipeline		                     m_vkPipeline = VK_NULL_HANDLE;
	VkPipelineLayout                     m_vkPipelineLayout = VK_NULL_HANDLE;
	std::vector< VkDescriptorSetLayout > m_vkSetLayouts;

	bool m_bMeshShader = false;

	struct PipelineDesc
	{
		VkRenderPass renderPass = VK_NULL_HANDLE;

		std::vector< VkPipelineShaderStageCreateInfo >     shaderStages;
		std::vector< VkPipelineColorBlendAttachmentState > blendStates;
		VkLogicOp                                          blendLogicOp = VK_LOGIC_OP_CLEAR;

		VkPipelineVertexInputStateCreateInfo	vertexInputInfo   = {};
		VkPipelineInputAssemblyStateCreateInfo	inputAssemblyInfo = {};
		VkPipelineViewportStateCreateInfo		viewportStateInfo = {};
		VkPipelineRasterizationStateCreateInfo	rasterizerInfo    = {};
		VkPipelineDepthStencilStateCreateInfo	depthStencilInfo  = {};
		VkPipelineMultisampleStateCreateInfo	multisamplingInfo = {};
	} m_PipelineDesc = {};

	std::unordered_map< u32, DescriptorSet& > m_DescriptorTable;
};


//-------------------------------------------------------------------------
// Compute Pipeline
//-------------------------------------------------------------------------
class ComputePipeline
{
public:
	ComputePipeline(RenderDevice& device, std::string name);
	~ComputePipeline();

	ComputePipeline& SetComputeShader(Arc< Shader > pCS);
	void Build();

	[[nodiscard]]
	inline VkPipeline vkPipeline() const { return m_vkPipeline; }
	[[nodiscard]]
	inline VkPipelineLayout vkPipelineLayout() const { return m_vkPipelineLayout; }

private:
	RenderDevice& m_RenderDevice;
	std::string   m_Name;

	VkPipeline		                     m_vkPipeline = VK_NULL_HANDLE;
	VkPipelineLayout                     m_vkPipelineLayout = VK_NULL_HANDLE;
	std::vector< VkDescriptorSetLayout > m_vkSetLayouts;

	Arc< Shader > m_pCS;

	std::unordered_map< u32, DescriptorSet& > m_DescriptorTable;
};

} // namespace vk