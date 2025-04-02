#pragma once

namespace vk
{

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

class DescriptorSet
{
public:
	DescriptorSet(RenderContext& context, VkDescriptorPool vkPool);
	~DescriptorSet();

private:
	RenderContext& m_renderContext;

	VkDescriptorPool           m_vkPoolBelonged = VK_NULL_HANDLE;
	VkDescriptorSet            m_vkSet = VK_NULL_HANDLE;
	VkDescriptorSetLayout      m_vkSetLayout = VK_NULL_HANDLE;
	VkDescriptorUpdateTemplate m_vkTemplate = VK_NULL_HANDLE;

	std::vector< VkWriteDescriptorSet > m_descriptions;
};

} // namespace vk