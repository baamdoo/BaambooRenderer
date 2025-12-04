#include "RendererPch.h"
#include "VkTexture.h"
#include "VkBuffer.h"
#include "VkSampler.h"
#include "RenderDevice/VkCommandContext.h"

#include "Utils/Math.hpp"

namespace vk
{

inline u32 GetFormatElementSizeInBytes(VkFormat format)
{
	u32 result = 0;
	switch (format)
	{
	case VK_FORMAT_R8G8B8A8_UNORM:
	case VK_FORMAT_R8G8B8A8_SNORM:
	case VK_FORMAT_R8G8B8A8_SRGB:
	case VK_FORMAT_B8G8R8A8_SRGB:
		result = 4;
		break;
	case VK_FORMAT_R16G16B16A16_UINT:
	case VK_FORMAT_R16G16B16A16_SINT:
	case VK_FORMAT_R16G16B16A16_SFLOAT:
		result = 8;
		break;
	case VK_FORMAT_R32G32B32A32_SFLOAT:
		result = 16;
		break;
	}

	BB_ASSERT(result > 0, "Invalid format!");
	return result;
}

inline VkSampleCountFlagBits GetSampleCount(u32 sampleCount)
{
	switch (sampleCount)
	{
		case 1  : return VK_SAMPLE_COUNT_1_BIT;
		case 2  : return VK_SAMPLE_COUNT_2_BIT;
		case 4  : return VK_SAMPLE_COUNT_4_BIT;
		case 8  : return VK_SAMPLE_COUNT_8_BIT;
		case 16 : return VK_SAMPLE_COUNT_16_BIT;
		case 32 : return VK_SAMPLE_COUNT_32_BIT;
		case 64 : return VK_SAMPLE_COUNT_64_BIT;
		default : return VK_SAMPLE_COUNT_1_BIT;
	}
}

VkImageCreateInfo GetVkImageCreateInfo(const render::Texture::CreationInfo& info)
{
	using namespace render; 

	VkImageCreateInfo desc = {};
	desc.flags         = info.imageType == eImageType::TextureCube ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;
	desc.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	desc.imageType     = info.imageType == eImageType::TextureCube ? VK_IMAGE_TYPE_2D : static_cast<VkImageType>(info.imageType);
	desc.format        = VK_FORMAT(info.format);
	desc.extent        = VkExtent3D(info.resolution.x, info.resolution.y, info.resolution.z);
	desc.mipLevels     = info.bGenerateMips ?
		baamboo::math::CalculateMipCount(info.resolution.x, info.resolution.y, 1) : 1;
	desc.arrayLayers   = info.arrayLayers;
	desc.samples       = GetSampleCount(info.sampleCount);
	desc.tiling        = VK_IMAGE_TILING_OPTIMAL;
	desc.usage         = VK_IMAGE_USAGE_FLAGS(info.imageUsage);
	desc.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
	desc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	return desc;
}

void VulkanTexture::CreateImageAndView(const CreationInfo& info)
{
	// **
	// Create image
	// **
	m_Desc = GetVkImageCreateInfo(info);

	VmaAllocationCreateInfo vmaInfo = {};
	vmaInfo.usage = VMA_MEMORY_USAGE_AUTO;
	vmaInfo.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
	VK_CHECK(vmaCreateImage(m_RenderDevice.vmaAllocator(), &m_Desc, &vmaInfo, &m_vkImage, &m_vmaAllocation, &m_AllocationInfo));


	// **
	// Create image view
	// **
	auto viewInfo = GetViewDesc(m_Desc);
	m_AspectFlags = viewInfo.subresourceRange.aspectMask;
	VK_CHECK(vkCreateImageView(m_RenderDevice.vkDevice(), &viewInfo, nullptr, &m_vkImageView));

	if (m_Desc.flags & VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT)
	{
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
		VK_CHECK(vkCreateImageView(m_RenderDevice.vkDevice(), &viewInfo, nullptr, &m_vkImageUAV));
	}
}

VkImageViewCreateInfo VulkanTexture::GetViewDesc(const VkImageCreateInfo& imageInfo)
{
	using namespace render;

	VkImageAspectFlags aspectFlags = imageInfo.format < VK_FORMAT_D16_UNORM ? VK_IMAGE_ASPECT_COLOR_BIT : VK_IMAGE_ASPECT_DEPTH_BIT;
	if (imageInfo.format >= VK_FORMAT_D16_UNORM_S8_UINT && !(imageInfo.usage & VK_IMAGE_USAGE_SAMPLED_BIT))
		aspectFlags |= VK_IMAGE_ASPECT_STENCIL_BIT;

	VkImageViewCreateInfo imageViewInfo{};
	imageViewInfo.sType      = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	imageViewInfo.image      = m_vkImage;
	imageViewInfo.format     = imageInfo.format;
	imageViewInfo.components = {
		VK_COMPONENT_SWIZZLE_R,
		VK_COMPONENT_SWIZZLE_G,
		VK_COMPONENT_SWIZZLE_B,
		VK_COMPONENT_SWIZZLE_A
	};
	imageViewInfo.subresourceRange.baseMipLevel   = 0;
	imageViewInfo.subresourceRange.baseArrayLayer = 0;
	imageViewInfo.subresourceRange.aspectMask     = aspectFlags;
	imageViewInfo.subresourceRange.levelCount     = imageInfo.mipLevels;

	switch (imageInfo.imageType)
	{
	case VK_IMAGE_TYPE_1D:
	{
		if (imageInfo.arrayLayers > 1)
		{
			imageViewInfo.viewType                    = VK_IMAGE_VIEW_TYPE_1D_ARRAY;
			imageViewInfo.subresourceRange.layerCount = imageInfo.arrayLayers;
		}
		else
		{
			imageViewInfo.viewType                    = VK_IMAGE_VIEW_TYPE_1D;
			imageViewInfo.subresourceRange.layerCount = 1;
		}
		break;
	}
	case VK_IMAGE_TYPE_2D:
	{
		if (imageInfo.arrayLayers > 1)
		{
			if (m_CreationInfo.imageType == eImageType::TextureCube)
			{
				imageViewInfo.viewType                    = VK_IMAGE_VIEW_TYPE_CUBE;
				imageViewInfo.subresourceRange.layerCount = 6;
			}
			else
			{
				imageViewInfo.viewType                    = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
				imageViewInfo.subresourceRange.layerCount = 1;
			}
		}
		else
		{
			imageViewInfo.viewType                    = VK_IMAGE_VIEW_TYPE_2D;
			imageViewInfo.subresourceRange.layerCount = 1;
		}
		break;
	}
	case VK_IMAGE_TYPE_3D:
	{
		assert(imageInfo.arrayLayers == 1);
		{
			imageViewInfo.viewType                    = VK_IMAGE_VIEW_TYPE_3D;
			imageViewInfo.subresourceRange.layerCount = 1;
		}
		break;
	}

	default:
		BB_ASSERT(false, "Invalid entry! (VkTexture::GetViewDesc())");
	}

	return imageViewInfo;
}

VkImageView VulkanTexture::vkView() const
{
	auto layout = GetState().GetSubresourceState().layout;
	if (m_vkImageUAV && layout == VK_IMAGE_LAYOUT_GENERAL)
	{
		return m_vkImageUAV;
	}

	if (m_vkImageSRV && layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
	{
		return m_vkImageSRV;
	}

	return m_vkImageView;
}

VkClearValue VulkanTexture::ClearValue() const
{
	VkClearValue clearValue = {};
	if (m_AspectFlags & VK_IMAGE_ASPECT_COLOR_BIT)
	{
		clearValue.color.float32[0] = m_CreationInfo.clearValue[0];
		clearValue.color.float32[1] = m_CreationInfo.clearValue[1];
		clearValue.color.float32[2] = m_CreationInfo.clearValue[2];
		clearValue.color.float32[3] = m_CreationInfo.clearValue[3];
	}
	else
	{
		clearValue.depthStencil.depth   = m_CreationInfo.depthClearValue;
		clearValue.depthStencil.stencil = m_CreationInfo.stencilClearValue;
	}

	return clearValue;
}

u64 VulkanTexture::SizeInBytes() const
{
	return m_Desc.extent.width * m_Desc.extent.height * m_Desc.extent.depth * GetFormatElementSizeInBytes(m_Desc.format);
}

VulkanTexture::VulkanTexture(VkRenderDevice& rd, const char* name)
	: render::Texture(name)
	, VulkanResource(rd, name)
{
}

VulkanTexture::VulkanTexture(VkRenderDevice& rd, const char* name, CreationInfo&& info)
	: render::Texture(name, std::move(info))
	, VulkanResource(rd, name)
{
	CreateImageAndView(m_CreationInfo);
	SetDeviceObjectName((u64)m_vkImage, VK_OBJECT_TYPE_IMAGE);
}

VulkanTexture::~VulkanTexture()
{
	vkDestroyImageView(m_RenderDevice.vkDevice(), m_vkImageView, nullptr);
	if (m_vkImageSRV)
		vkDestroyImageView(m_RenderDevice.vkDevice(), m_vkImageSRV, nullptr);
	if (m_vkImageUAV)
		vkDestroyImageView(m_RenderDevice.vkDevice(), m_vkImageUAV, nullptr);

	if (m_vmaAllocation)
		vmaDestroyImage(m_RenderDevice.vmaAllocator(), m_vkImage, m_vmaAllocation);
}

Arc< VulkanTexture > VulkanTexture::Create(VkRenderDevice& rd, const char* name, CreationInfo&& desc)
{
	return MakeArc< VulkanTexture >(rd, name, std::move(desc));
}

Arc< VulkanTexture > VulkanTexture::CreateEmpty(VkRenderDevice& rd, const char* name)
{
	return MakeArc< VulkanTexture >(rd, name);
}

void VulkanTexture::Resize(u32 width, u32 height, u32 depth)
{
	assert(m_vkImage && m_vkImageView);

	vkDestroyImageView(m_RenderDevice.vkDevice(), m_vkImageView, nullptr);
	if (m_vmaAllocation)
		vmaDestroyImage(m_RenderDevice.vmaAllocator(), m_vkImage, m_vmaAllocation);

	m_CreationInfo.resolution = { width, height, depth };
	CreateImageAndView(m_CreationInfo);
}

void VulkanTexture::SetResource(VkImage vkImage, VkImageView vkImageView, VkImageCreateInfo createInfo, VmaAllocation vmaAllocation, VmaAllocationInfo vmaAllocInfo, VkImageAspectFlags aspectMask)
{
	assert(!m_vkImage && !m_vkImageView);

	m_vkImage        = vkImage;
	m_vkImageView    = vkImageView;
	m_Desc           = createInfo;
	m_vmaAllocation  = vmaAllocation;
	m_AllocationInfo = vmaAllocInfo;
	m_AspectFlags    = aspectMask;

	SetDeviceObjectName((u64)m_vkImage, VK_OBJECT_TYPE_IMAGE);
}

} // namespace vk