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
class VulkanGraphicsPipeline : public render::GraphicsPipeline
{
public:
	VulkanGraphicsPipeline(VkRenderDevice& rd, const char* name);
	~VulkanGraphicsPipeline();

	render::GraphicsPipeline& SetVertexInputs(std::vector< VkVertexInputBindingDescription >&& streams, std::vector< VkVertexInputAttributeDescription >&& attributes);
	virtual render::GraphicsPipeline& SetRenderTarget(Arc< render::RenderTarget > pRenderTarget) override;

	virtual render::GraphicsPipeline& SetFillMode(bool bWireframe) override;
	virtual render::GraphicsPipeline& SetCullMode(render::eCullMode cullMode) override;

	virtual render::GraphicsPipeline& SetTopology(render::ePrimitiveTopology topology) override;
	virtual render::GraphicsPipeline& SetDepthTestEnable(bool bEnable, render::eCompareOp) override;
	virtual render::GraphicsPipeline& SetDepthWriteEnable(bool bEnable, render::eCompareOp) override;

	virtual render::GraphicsPipeline& SetLogicOp(render::eLogicOp logicOp) override;
	virtual render::GraphicsPipeline& SetBlendEnable(u32 renderTargetIndex, bool bEnable) override;
	virtual render::GraphicsPipeline& SetColorBlending(u32 renderTargetIndex, render::eBlendFactor srcBlend, render::eBlendFactor dstBlend, render::eBlendOp blendOp) override;
	virtual render::GraphicsPipeline& SetAlphaBlending(u32 renderTargetIndex, render::eBlendFactor srcBlend, render::eBlendFactor dstBlend, render::eBlendOp blendOp) override;

	virtual void Build() override;

	[[nodiscard]]
	inline VkPipeline vkPipeline() const { return m_vkPipeline; }
	[[nodiscard]]
	inline VkPipelineLayout vkPipelineLayout() const { return m_vkPipelineLayout; }
	[[nodiscard]]
	inline VkDescriptorSetLayout vkSetLayout(u8 set) const { return m_vkSetLayouts[set]; }

private:
	VkRenderDevice& m_RenderDevice;

	VkPipeline		                     m_vkPipeline = VK_NULL_HANDLE;
	VkPipelineLayout                     m_vkPipelineLayout = VK_NULL_HANDLE;
	std::vector< VkDescriptorSetLayout > m_vkSetLayouts;

	struct PipelineDesc
	{
		VkRenderPass renderPass = VK_NULL_HANDLE;

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
class VulkanComputePipeline : public render::ComputePipeline
{
public:
	VulkanComputePipeline(VkRenderDevice& rd, const char* name);
	~VulkanComputePipeline();

	virtual void Build() override;

	[[nodiscard]]
	inline VkPipeline vkPipeline() const { return m_vkPipeline; }
	[[nodiscard]]
	inline VkPipelineLayout vkPipelineLayout() const { return m_vkPipelineLayout; }

private:
	VkRenderDevice& m_RenderDevice;

	VkPipeline		                     m_vkPipeline = VK_NULL_HANDLE;
	VkPipelineLayout                     m_vkPipelineLayout = VK_NULL_HANDLE;
	std::vector< VkDescriptorSetLayout > m_vkSetLayouts;

	std::unordered_map< u32, DescriptorSet& > m_DescriptorTable;
};

} // namespace vk