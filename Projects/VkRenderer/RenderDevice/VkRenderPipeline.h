#pragma once
#include "RenderResource/VkShader.h"

namespace vk
{

class RenderTarget;

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
	}
	DescriptorInfo(VkSampler sampler, VkImageView imageView, VkImageLayout imageLayout)
	{
		imageInfo.sampler = sampler;
		imageInfo.imageView = imageView;
		imageInfo.imageLayout = imageLayout;
	}

	DescriptorInfo(const VkDescriptorBufferInfo& bufferInfo_)
	{
		bufferInfo = bufferInfo_;
	}
	DescriptorInfo(VkBuffer buffer, VkDeviceSize offset = 0, VkDeviceSize range = VK_WHOLE_SIZE)
	{
		bufferInfo.buffer = buffer;
		bufferInfo.offset = offset;
		bufferInfo.range = range;
	}
};

//-------------------------------------------------------------------------
// Graphics Pipeline
//-------------------------------------------------------------------------
class GraphicsPipeline
{
public:
	GraphicsPipeline(RenderContext& context, std::string name);
	~GraphicsPipeline();

	GraphicsPipeline& SetShaders(
		baamboo::ResourceHandle< Shader > vs, 
		baamboo::ResourceHandle< Shader > fs,
		baamboo::ResourceHandle< Shader > gs = baamboo::ResourceHandle< Shader >(),
		baamboo::ResourceHandle< Shader > hs = baamboo::ResourceHandle< Shader >(),
		baamboo::ResourceHandle< Shader > ds = baamboo::ResourceHandle< Shader >());
	GraphicsPipeline& SetMeshShaders(
		baamboo::ResourceHandle< Shader > ms,
		baamboo::ResourceHandle< Shader > ts = baamboo::ResourceHandle< Shader >());

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
	[[nodiscard]]
	inline VkDescriptorSetLayout* vkSetLayouts(u32 offset = 0) { return m_vkSetLayouts + offset; }

private:
	RenderContext& m_renderContext;
	std::string    m_name;

	VkPipeline		           m_vkPipeline = VK_NULL_HANDLE;
	VkPipelineLayout           m_vkPipelineLayout = VK_NULL_HANDLE;
	VkDescriptorSetLayout      m_vkSetLayouts[eNumDescriptorSet] = { VK_NULL_HANDLE };

	baamboo::ResourceHandle< Shader > m_vs;
	baamboo::ResourceHandle< Shader > m_fs;
	baamboo::ResourceHandle< Shader > m_gs;
	baamboo::ResourceHandle< Shader > m_ds;
	baamboo::ResourceHandle< Shader > m_hs;

	baamboo::ResourceHandle< Shader > m_ts;
	baamboo::ResourceHandle< Shader > m_ms;

	bool m_bMeshShader = false;

	struct
	{
		VkRenderPass renderPass = VK_NULL_HANDLE;

		std::vector< VkPipelineShaderStageCreateInfo >     shaderStages;
		std::vector< VkPipelineColorBlendAttachmentState > blendStates;
		VkLogicOp                                          blendLogicOp;

		VkPipelineVertexInputStateCreateInfo	vertexInputInfo;
		VkPipelineInputAssemblyStateCreateInfo	inputAssemblyInfo;
		VkPipelineViewportStateCreateInfo		viewportStateInfo;
		VkPipelineRasterizationStateCreateInfo	rasterizerInfo;
		VkPipelineDepthStencilStateCreateInfo	depthStencilInfo;
		VkPipelineMultisampleStateCreateInfo	multisamplingInfo;
	} m_createInfos;
};


//-------------------------------------------------------------------------
// Compute Pipeline
//-------------------------------------------------------------------------
class ComputePipeline
{
public:
	ComputePipeline(RenderContext& context);
	~ComputePipeline();

	[[nodiscard]]
	inline VkPipeline vkPipeline() const { return m_vkPipeline; }

private:
	RenderContext& m_renderContext;

	VkPipeline		 m_vkPipeline = VK_NULL_HANDLE;
	VkPipelineLayout m_vkPipelineLayout = VK_NULL_HANDLE;

	baamboo::ResourceHandle< Shader > m_cs;
};

} // namespace vk