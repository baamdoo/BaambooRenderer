#include "RendererPch.h"
#include "VkTexture.h"
#include "VkBuffer.h"
#include "RenderDevice/VkCommandQueue.h"
#include "RenderDevice/VkCommandBuffer.h"

namespace vk
{

inline u32 GetFormatElementSizeInBytes(VkFormat format)
{
	u32 result = 0;
	switch (format)
	{
	case VK_FORMAT_R8G8B8A8_UNORM:
	case VK_FORMAT_R8G8B8A8_SNORM:
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
		case 1: return VK_SAMPLE_COUNT_1_BIT;
		case 2: return VK_SAMPLE_COUNT_2_BIT;
		case 4: return VK_SAMPLE_COUNT_4_BIT;
		case 8: return VK_SAMPLE_COUNT_8_BIT;
		case 16: return VK_SAMPLE_COUNT_16_BIT;
		case 32: return VK_SAMPLE_COUNT_32_BIT;
		case 64: return VK_SAMPLE_COUNT_64_BIT;
		default: return VK_SAMPLE_COUNT_1_BIT;
	}
}

inline u32 CalculateMipCount(u32 width, u32 height)
{
	return (u32)std::floor(std::log2(glm::min(width, height))) + 1;
}

Texture::CreationInfo::operator VkImageCreateInfo() const
{
	VkImageCreateInfo desc = {};
	desc.flags = type == eTextureType::TextureCube ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;
	desc.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	desc.imageType = type == eTextureType::TextureCube ? VK_IMAGE_TYPE_2D : static_cast<VkImageType>(type);
	desc.format = format;
	desc.extent = resolution;
	desc.mipLevels = bGenerateMips ?
		CalculateMipCount(resolution.width, resolution.height) : 1;
	desc.arrayLayers = arrayLayers;
	desc.samples = GetSampleCount(sampleCount);
	desc.tiling = VK_IMAGE_TILING_OPTIMAL;
	desc.usage = imageUsage;
	desc.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	desc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	return desc;
}

void Texture::CreateImageAndView(const CreationInfo& info)
{
	// **
	// Create image
	// **
	m_desc = info;

	VmaAllocationCreateInfo vmaInfo = {};
	vmaInfo.usage = info.memoryUsage;

	VK_CHECK(vmaCreateImage(m_renderContext.vmaAllocator(), &m_desc, &vmaInfo, &m_vkImage, &m_vmaAllocation, &m_vmaAllocationInfo));


	// **
	// Create image view
	// **
	auto viewInfo = GetViewDesc(m_desc);
	VK_CHECK(vkCreateImageView(m_renderContext.vkDevice(), &viewInfo, nullptr, &m_vkImageView));
}

VkImageViewCreateInfo Texture::GetViewDesc(const VkImageCreateInfo& imageInfo)
{
	VkImageAspectFlags aspectFlags = imageInfo.format < VK_FORMAT_D16_UNORM ? VK_IMAGE_ASPECT_COLOR_BIT : VK_IMAGE_ASPECT_DEPTH_BIT;
	if (imageInfo.format >= VK_FORMAT_D16_UNORM_S8_UINT && !(imageInfo.usage & VK_IMAGE_USAGE_SAMPLED_BIT))
		aspectFlags |= VK_IMAGE_ASPECT_STENCIL_BIT;

	VkImageViewCreateInfo imageViewInfo{};
	imageViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	imageViewInfo.image = m_vkImage;
	imageViewInfo.format = imageInfo.format;
	imageViewInfo.components = {
		VK_COMPONENT_SWIZZLE_R,
		VK_COMPONENT_SWIZZLE_G,
		VK_COMPONENT_SWIZZLE_B,
		VK_COMPONENT_SWIZZLE_A
	};
	imageViewInfo.subresourceRange.baseMipLevel = 0;
	imageViewInfo.subresourceRange.baseArrayLayer = 0;
	imageViewInfo.subresourceRange.aspectMask = aspectFlags;
	imageViewInfo.subresourceRange.levelCount = imageInfo.mipLevels;

	switch (imageInfo.imageType)
	{
	case VK_IMAGE_TYPE_1D:
	{
		if (imageInfo.arrayLayers > 1)
		{
			imageViewInfo.viewType = VK_IMAGE_VIEW_TYPE_1D_ARRAY;
			imageViewInfo.subresourceRange.layerCount = imageInfo.arrayLayers;
		}
		else
		{
			imageViewInfo.viewType = VK_IMAGE_VIEW_TYPE_1D;
			imageViewInfo.subresourceRange.layerCount = 1;
		}
		break;
	}
	case VK_IMAGE_TYPE_2D:
	{
		if (imageInfo.arrayLayers > 1)
		{
			if (m_creationInfo.type == eTextureType::TextureCube)
			{
				imageViewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
				imageViewInfo.subresourceRange.layerCount = imageInfo.arrayLayers * 6;
			}
			else
			{
				imageViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
				imageViewInfo.subresourceRange.layerCount = 1;
			}
		}
		else
		{
			if (m_creationInfo.type == eTextureType::TextureCube)
			{
				imageViewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
				imageViewInfo.subresourceRange.layerCount = 6;
			}
			else
			{
				imageViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
				imageViewInfo.subresourceRange.layerCount = 1;
			}
		}
		break;
	}
	case VK_IMAGE_TYPE_3D:
	{
		assert(imageInfo.arrayLayers == 1);
		{
			imageViewInfo.viewType = VK_IMAGE_VIEW_TYPE_3D;
			imageViewInfo.subresourceRange.layerCount = 1;
		}
		break;
	}

	default:
		BB_ASSERT(false, "Invalid entry! (VkTexture::GetViewDesc())");
	}

	return imageViewInfo;
}

Texture::Texture(RenderContext& context, std::string_view name)
	: Super(context, name)
{
}

Texture::Texture(RenderContext& context, std::string_view name, CreationInfo&& info)
	: Super(context, name)
	, m_creationInfo(info)
{
	CreateImageAndView(m_creationInfo);
}

Texture::Texture(RenderContext& context, std::string_view name, CreationInfo&& info, void* data)
	: Super(context, name)
	, m_creationInfo(info)
{
	CreateImageAndView(m_creationInfo);

	// **
	// Generate mips
	// **
	std::vector< VkBufferImageCopy > regions(m_desc.mipLevels * m_desc.arrayLayers);
	VkDeviceSize offset = 0; // considered as 1-dimensional image buffer
	VkDeviceSize texSize = 0;
	for (u32 level = 0; level < m_desc.mipLevels; level++)
	{
		for (u32 layer = 0; layer < m_desc.arrayLayers; layer++)
		{
			const u32 w = info.resolution.width >> level;
			const u32 h = info.resolution.height >> level;

			VkBufferImageCopy region = {};
			region.bufferOffset = offset;
			region.bufferRowLength = 0;
			region.bufferImageHeight = 0;
			region.imageSubresource = {
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, // assume no depth texture uses mip-map
				.mipLevel = level,
				.baseArrayLayer = layer,
				.layerCount = 1
			};
			region.imageExtent = { w, h, 1 };
			regions[layer * m_desc.mipLevels + level] = region;

			VkDeviceSize stride = m_renderContext.GetAlignedSize(w * h * GetFormatElementSizeInBytes(info.format));
			offset += stride;
			texSize += stride;
		}
	}


	// **
	// Copy data via staging buffer
	// **
	auto pTransferQueue = m_renderContext.TransferQueue();
	if (pTransferQueue)
	{
		auto& cmdBuffer = pTransferQueue->Allocate();
		// cmdBuffer.CopyBuffer()
	}
}

Texture::Texture(RenderContext& context, std::string_view name, CreationInfo&& info, std::string_view filepath)
	: Super(context, name)
	, m_creationInfo(info)
{

}

Texture::~Texture()
{
	vkDestroyImageView(m_renderContext.vkDevice(), m_vkImageView, nullptr);
	if (!m_bOwnedBySwapChain)
		vmaDestroyImage(m_renderContext.vmaAllocator(), m_vkImage, m_vmaAllocation);
}

void Texture::Resize(u32 width, u32 height, u32 depth)
{
	assert(m_vkImage && m_vkImageView);

	vkDestroyImageView(m_renderContext.vkDevice(), m_vkImageView, nullptr);
	if (!m_bOwnedBySwapChain)
		vmaDestroyImage(m_renderContext.vmaAllocator(), m_vkImage, m_vmaAllocation);

	m_creationInfo.resolution = { width, height, depth };
	CreateImageAndView(m_creationInfo);
}

void Texture::SetResource(VkImage vkImage, VkImageView vkImageView, VmaAllocation vmaAllocation)
{
	assert(!m_vkImage && !m_vkImageView);

	m_vkImage = vkImage;
	m_vkImageView = vkImageView;
	m_vmaAllocation = vmaAllocation;
	m_bOwnedBySwapChain = vmaAllocation == VK_NULL_HANDLE;
}

} // namespace vk