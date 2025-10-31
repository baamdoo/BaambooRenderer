#include "RendererPch.h"
#include "VkResourceManager.h"
#include "VkCommandContext.h"
#include "RenderResource/VkBuffer.h"
#include "RenderResource/VkSceneResource.h"
#include "Utils/Math.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>
#include <gli/gli.hpp>

namespace vk
{

VkResourceManager::VkResourceManager(VkRenderDevice& rd)
	: m_RenderDevice(rd)
{
	m_pStagingBuffer =
		VulkanUniformBuffer::Create(m_RenderDevice, "StagingBufferPool", _MB(8), VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);

	m_pWhiteTexture = CreateFlatWhiteTexture();
	m_pBlackTexture = CreateFlatBlackTexture();
	m_pGrayTexture  = CreateFlat2DTexture("DefaultTexture::Gray", 0xFF808080u);

	m_pSceneResource = new VkSceneResource(m_RenderDevice);
}

VkResourceManager::~VkResourceManager()
{
}

Arc< render::Texture > VkResourceManager::LoadTexture(const std::string& filepath, bool bGenerateMips)
{
	using namespace render;

	const VkImageUsageFlags DefaultUsage = eTextureUsage_Sample | eTextureUsage_TransferDest | (bGenerateMips ? eTextureUsage_TransferSource : 0);

	fs::path path = filepath;
	if (fs::is_directory(path))
	{
		return LoadTextureArray(filepath, bGenerateMips);
	}

	std::string extension = path.extension().string();
	if (extension == ".dds")
	{
		gli::texture gliTexture = gli::load_dds(path.string().c_str());
		if (gliTexture.empty())
		{
			__debugbreak();
			return nullptr;
		}

		auto ImageTypeConverter = [](gli::target gliType)
			{
				switch (gliType)
				{
				case gli::target::TARGET_1D: return VK_IMAGE_TYPE_1D;
				case gli::target::TARGET_2D: return VK_IMAGE_TYPE_2D;
				case gli::target::TARGET_3D: return VK_IMAGE_TYPE_3D;

				default: __debugbreak(); break;
				}
				return VK_IMAGE_TYPE_2D;
			};

		auto ImageViewTypeConverter = [](gli::target gliType)
			{
				switch (gliType)
				{
				case gli::target::TARGET_1D        : return VK_IMAGE_VIEW_TYPE_1D;
				case gli::target::TARGET_2D        : return VK_IMAGE_VIEW_TYPE_2D;
				case gli::target::TARGET_3D        : return VK_IMAGE_VIEW_TYPE_3D;
				case gli::target::TARGET_CUBE      : return VK_IMAGE_VIEW_TYPE_CUBE;
				case gli::target::TARGET_1D_ARRAY  : return VK_IMAGE_VIEW_TYPE_1D_ARRAY;
				case gli::target::TARGET_2D_ARRAY  : return VK_IMAGE_VIEW_TYPE_2D_ARRAY;
				case gli::target::TARGET_CUBE_ARRAY: return VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;

				default: __debugbreak(); break;
				}
				return VK_IMAGE_VIEW_TYPE_2D;
			};

		VkImage           vkImage           = VK_NULL_HANDLE;
		VkImageView       vkImageView       = VK_NULL_HANDLE;
		VmaAllocation     vmaAllocation     = VK_NULL_HANDLE;
		VmaAllocationInfo vmaAllocationInfo = {};

		// **
		// Create image
		// **
		VkImageCreateInfo createInfo = {};
		createInfo.sType       = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		createInfo.imageType   = ImageTypeConverter(gliTexture.target());
		createInfo.format      = static_cast<VkFormat>(gliTexture.format());
		createInfo.extent      = { (u32)gliTexture.extent().x, (u32)gliTexture.extent().y, (u32)gliTexture.extent().z };
		createInfo.mipLevels   = bGenerateMips ? 
			baamboo::math::CalculateMipCount((u32)gliTexture.extent().x, (u32)gliTexture.extent().y, (u32)gliTexture.extent().z) : (u32)gliTexture.levels();
		createInfo.arrayLayers = (u32)gliTexture.layers();
		createInfo.samples     = VK_SAMPLE_COUNT_1_BIT;
		createInfo.usage       = VK_IMAGE_USAGE_FLAGS(DefaultUsage);
		
		VmaAllocationCreateInfo vmaInfo = {};
		vmaInfo.usage = VMA_MEMORY_USAGE_AUTO;
		vmaInfo.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
		VK_CHECK(vmaCreateImage(m_RenderDevice.vmaAllocator(), &createInfo, &vmaInfo, &vkImage, &vmaAllocation, &vmaAllocationInfo));

		// **
		// Create image view
		// **
		VkImageViewCreateInfo viewCreateInfo = {};
		viewCreateInfo.sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewCreateInfo.image            = vkImage;
		viewCreateInfo.viewType         = ImageViewTypeConverter(gliTexture.target());
		viewCreateInfo.format           = static_cast<VkFormat>(gliTexture.format());
		viewCreateInfo.subresourceRange = 
		{
			.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT, // assume .dds is always color
			.baseMipLevel   = static_cast<u32>(gliTexture.base_level()),
			.levelCount     = static_cast<u32>(gliTexture.levels()),
			.baseArrayLayer = static_cast<u32>(gliTexture.base_layer()),
			.layerCount     = static_cast<u32>(gliTexture.layers())
		};
		VK_CHECK(vkCreateImageView(m_RenderDevice.vkDevice(), &viewCreateInfo, nullptr, &vkImageView));

		auto pTex = VulkanTexture::CreateEmpty(m_RenderDevice, path.filename().string());
		pTex->SetResource(vkImage, vkImageView, createInfo, vmaAllocation, vmaAllocationInfo, VK_IMAGE_ASPECT_COLOR_BIT);

		// **
		// Upload Data
		// **
		auto texSizeInBytes = pTex->SizeInBytes();
		VkBufferImageCopy region = {};
		region.bufferOffset      = 0;
		region.bufferRowLength   = 0;
		region.bufferImageHeight = 0;
		region.imageSubresource  =
		{
			.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
			.mipLevel       = 0,
			.baseArrayLayer = 0,
			.layerCount     = 1
		};
		region.imageExtent = pTex->Desc().extent;
		UploadData(pTex, gliTexture.data(), texSizeInBytes, region, bGenerateMips);

		return pTex;
	}
	else
	{
		u32 width, height, numChannels;
		u8* pData = stbi_load(path.string().c_str(), (int*)&width, (int*)&height, (int*)&numChannels, STBI_rgb_alpha);
		if (!pData)
		{
			__debugbreak();
			return nullptr;
		}

		auto pTex = VulkanTexture::Create(m_RenderDevice, path.filename().string(),
			{
				.resolution    = { width, height, 1 },
				.format        = eFormat::RGBA8_UNORM,
				.imageUsage    = DefaultUsage,
			});

		// **
		// Copy data to staging buffer
		// **
		auto texSizeInBytes = pTex->SizeInBytes();
		VkBufferImageCopy region = {};
		region.bufferOffset      = 0;
		region.bufferRowLength   = 0;
		region.bufferImageHeight = 0;
		region.imageSubresource  =
		{
			.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
			.mipLevel       = 0,
			.baseArrayLayer = 0,
			.layerCount     = 1
		};
		region.imageExtent = { width, height, 1 };
		UploadData(pTex, (void*)pData, texSizeInBytes, region, bGenerateMips);

		RELEASE(pData);

		return pTex;
	}
}

Arc< render::Texture > VkResourceManager::LoadTextureArray(const fs::path& dirpath, bool bGenerateMips)
{
	using namespace render;

	std::vector< std::pair< std::string, std::string > > imagePaths;
	for (const auto& entry : fs::directory_iterator(dirpath))
	{
		if (entry.is_regular_file())
		{
			std::string ext = entry.path().extension().string();
			imagePaths.emplace_back(entry.path().stem().string(), ext);
		}
	}

	if (imagePaths.empty())
	{
		return nullptr;
	}

	auto extractNumber = [](const std::string& name) -> int
		{
			size_t delimiter = name.find_last_of('_');
			if (delimiter != std::string::npos && delimiter < name.length() - 1)
			{
				std::string numberStr = name.substr(delimiter + 1);
				if (!numberStr.empty() && std::all_of(numberStr.begin(), numberStr.end(), ::isdigit))
				{
					return std::stoi(numberStr);
				}
			}

			return -1;
		};

	std::sort(imagePaths.begin(), imagePaths.end(), [&extractNumber](const auto& strA, const auto& strB)
		{
			const int numA = extractNumber(strA.first);
			const int numB = extractNumber(strB.first);

			if (numA != -1 && numB != -1)
			{
				return numA < numB;
			}
			else if (numA != -1)
			{
				return true;
			}
			else if (numB != -1)
			{
				return false;
			}
			else
			{
				return strA.first < strB.first;
			}
		});

	struct StbImageDeleter
	{
		void operator()(u8* pData) const
		{
			stbi_image_free(pData);
		}
	};

	std::vector< std::unique_ptr< u8, StbImageDeleter > > imageDataList;
	u32 layerCount = static_cast<u32>(imagePaths.size());

	int i = 0;
	int baseWidth = 0, baseHeight = 0, numChannels = 0;
	for (const auto& imagePath : imagePaths)
	{
		int width, height, channels;
		std::string fullpath = (dirpath.string() + "/" + imagePath.first + imagePath.second);
		u8* pData = stbi_load(fullpath.c_str(), &width, &height, &channels, STBI_rgb_alpha);
		if (!pData)
		{
			__debugbreak();
			return nullptr;
		}

		if (i == 0)
		{
			baseWidth   = width;
			baseHeight  = height;
			numChannels = channels;
		}
		else if (width != baseWidth || height != baseHeight || numChannels != channels)
		{
			printf("Error: Image %s (size %d_x_%d) has different dimensions than base (size %d_x_%d)\n", fullpath.c_str(), width, height, baseWidth, baseHeight);
			stbi_image_free(pData);

			__debugbreak();
			return nullptr;
		}

		imageDataList.emplace_back(pData, StbImageDeleter{});
		++i;
	}

	auto pTextureArray = VulkanTexture::Create(m_RenderDevice, dirpath.filename().string() + "_Array",
		{
			.type        = eTextureType::Texture2D,
			.resolution  = { static_cast<u32>(baseWidth), static_cast<u32>(baseHeight), 1 },
			.format      = eFormat::RGBA8_UNORM,
			.imageUsage  = eTextureUsage_Sample | eTextureUsage_TransferDest,
			.arrayLayers = layerCount
		});

	u32 layerSize = baseWidth * baseHeight * 4;
	u64 totalSize = layerSize * layerCount;
	if (m_pStagingBuffer->SizeInBytes() < totalSize)
	{
		m_pStagingBuffer->Resize(totalSize);
	}

	u8* pMappedData = static_cast<u8*>(m_pStagingBuffer->MappedMemory());
	for (u32 layer = 0; layer < layerCount; ++layer)
	{
		memcpy(pMappedData + layer * layerSize, imageDataList[layer].get(), layerSize);
	}

	std::vector< VkBufferImageCopy > regions(layerCount);
	for (u32 layer = 0; layer < layerCount; ++layer)
	{
		regions[layer].bufferOffset      = layer * layerSize;
		regions[layer].bufferRowLength   = 0;
		regions[layer].bufferImageHeight = 0;
		regions[layer].imageSubresource  =
		{
			.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
			.mipLevel       = 0,
			.baseArrayLayer = layer,
			.layerCount     = 1
		};
		regions[layer].imageOffset = { 0, 0, 0 };
		regions[layer].imageExtent = { static_cast<u32>(baseWidth), static_cast<u32>(baseHeight), 1 };
	}

	auto pContext = m_RenderDevice.BeginCommand(eCommandType::Graphics, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, true);
	pContext->CopyBuffer(pTextureArray, m_pStagingBuffer, regions);
	if (bGenerateMips)
		pContext->GenerateMips(pTextureArray);
	pContext->Close();
	m_RenderDevice.ExecuteCommand(pContext);

	printf("Loaded texture array '%s' with %u layers from directory\n", dirpath.filename().string().c_str(), layerCount);
	return pTextureArray;
}

void VkResourceManager::UploadData(VkBuffer vkBuffer, const void* pData, u64 sizeInBytes, VkPipelineStageFlags2 dstStageMask, u64 dstOffsetInBytes)
{
	if (m_pStagingBuffer->SizeInBytes() < sizeInBytes)
	{
		m_pStagingBuffer->Resize(sizeInBytes);
	}
	memcpy(m_pStagingBuffer->MappedMemory(), pData, sizeInBytes);

	auto pContext = m_RenderDevice.BeginCommand(eCommandType::Transfer, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, true);
	pContext->CopyBuffer(vkBuffer, m_pStagingBuffer->vkBuffer(), sizeInBytes, dstStageMask, dstOffsetInBytes, 0);
	pContext->Close();
	m_RenderDevice.ExecuteCommand(pContext);
}

void VkResourceManager::UploadData(Arc< VulkanBuffer > pBuffer, const void* pData, u64 sizeInBytes, VkPipelineStageFlags2 dstStageMask, u64 dstOffsetInBytes)
{
	UploadData(pBuffer->vkBuffer(), pData, sizeInBytes, dstStageMask, dstOffsetInBytes);
}

void VkResourceManager::UploadData(Arc< VulkanTexture > pTexture, const void* pData, u64 sizeInBytes, VkBufferImageCopy region, bool bGenerateMips)
{
	if (m_pStagingBuffer->SizeInBytes() < sizeInBytes)
	{
		m_pStagingBuffer->Resize(sizeInBytes);
	}
	memcpy(m_pStagingBuffer->MappedMemory(), pData, sizeInBytes);

	auto pContext = m_RenderDevice.BeginCommand(eCommandType::Graphics, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, true);
	pContext->CopyBuffer(pTexture, m_pStagingBuffer, { region });
	if (bGenerateMips)
		pContext->GenerateMips(pTexture);
	pContext->Close();
	m_RenderDevice.ExecuteCommand(pContext);
}

Arc< render::Texture > VkResourceManager::CreateFlat2DTexture(const std::string& name, u32 color)
{
	auto flatTexture =
		VulkanTexture::Create(
			m_RenderDevice,
			name,
			{
				.resolution = { 1, 1, 1 },
				.imageUsage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT
			});

	u32* pData = (u32*)malloc(4);
	*pData = color;

	VkBufferImageCopy region = {};
	region.imageSubresource = 
	{
		.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
		.mipLevel       = 0,
		.baseArrayLayer = 0,
		.layerCount     = 1
	};
	region.imageExtent = { 1, 1, 1 };
	UploadData(flatTexture, pData, sizeof(u32) * 4, region);

	RELEASE(pData);
	return flatTexture;
}

Arc< render::Texture > VkResourceManager::CreateFlatWhiteTexture()
{
	return CreateFlat2DTexture("DefaultTexture::White", 0xFFFFFFFFu);
}

Arc< render::Texture > VkResourceManager::CreateFlatBlackTexture()
{
	return CreateFlat2DTexture("DefaultTexture::Black", 0xFF000000u);
}

} // namespace vk