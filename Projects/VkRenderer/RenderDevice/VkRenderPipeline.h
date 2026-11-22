#pragma once
#include "RenderResource/VkShader.h"

namespace vk
{

class VulkanRenderTarget;

//-------------------------------------------------------------------------
// Graphics Pipeline
//-------------------------------------------------------------------------
class VulkanGraphicsPipeline : public render::GraphicsPipeline
{
public:
	VulkanGraphicsPipeline(VkRenderDevice& rd, const char* name);
	~VulkanGraphicsPipeline();

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

	virtual bool Build() override;
	virtual bool IsBuilt() const override { return m_vkPipeline != VK_NULL_HANDLE; }

	[[nodiscard]]
	inline VkPipeline vkPipeline() const { return m_vkPipeline; }

private:
	VkRenderDevice& m_RenderDevice;

	VkPipeline m_vkPipeline = VK_NULL_HANDLE;

	Arc< VulkanRenderTarget > m_pRenderTarget;

	struct PipelineDesc
	{
		std::vector< VkPipelineColorBlendAttachmentState > blendStates;
		u32                                                colorAttachmentCount = 0;
		VkBool32                                           bLogicOpEnabled = VK_FALSE;
		VkLogicOp                                          blendLogicOp = VK_LOGIC_OP_NO_OP;

		VkPipelineVertexInputStateCreateInfo	vertexInputInfo   = {};
		VkPipelineInputAssemblyStateCreateInfo	inputAssemblyInfo = {};
		VkPipelineViewportStateCreateInfo		viewportStateInfo = {};
		VkPipelineRasterizationStateCreateInfo	rasterizerInfo    = {};
		VkPipelineDepthStencilStateCreateInfo	depthStencilInfo  = {};
		VkPipelineMultisampleStateCreateInfo	multisamplingInfo = {};
	} m_PipelineDesc = {};
};


//-------------------------------------------------------------------------
// Compute Pipeline
//-------------------------------------------------------------------------
class VulkanComputePipeline : public render::ComputePipeline
{
public:
	VulkanComputePipeline(VkRenderDevice& rd, const char* name);
	~VulkanComputePipeline();

	virtual bool Build() override;
	virtual bool IsBuilt() const override { return m_vkPipeline != VK_NULL_HANDLE; }

	[[nodiscard]]
	inline VkPipeline vkPipeline() const { return m_vkPipeline; }

private:
	VkRenderDevice& m_RenderDevice;

	VkPipeline m_vkPipeline = VK_NULL_HANDLE;
};

} // namespace vk
