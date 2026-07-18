#include "RendererPch.h"
#include "VkResourceManager.h"
#include "VkCommandContext.h"

#include "RenderResource/VkBuffer.h"
#include "RenderResource/VkSceneResource.h"
#include "RenderDevice/VkDescriptorPool.h"
#include "Utils/Math.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>
#include <gli/gli.hpp>
#include <numeric>

namespace vk
{

namespace
{
	render::eFormat GetRgba8Format(render::eTextureColorSpace colorSpace)
	{
		return colorSpace == render::eTextureColorSpace::SRGB
			? render::eFormat::RGBA8_SRGB
			: render::eFormat::RGBA8_UNORM;
	}
}

VkResourceManager::VkResourceManager(VkRenderDevice& rd)
	: m_RenderDevice(rd)
{
	m_pStagingBuffer =
		VulkanUniformBuffer::Create(m_RenderDevice, "StagingBufferPool", _MB(8), VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);

	m_pWhiteTexture = CreateFlatWhiteTexture();
	m_pBlackTexture = CreateFlatBlackTexture();
	m_pGrayTexture  = CreateFlat2DTexture("DefaultTexture::Gray", 0xFF808080u);

	m_pWhiteTexture3D = CreateFlat3DTexture("DefaultTexture3D::White", 0xFFFFFFFFu);
	m_pBlackTexture3D = CreateFlat3DTexture("DefaultTexture3D::Black", 0xFF000000u);

	m_pBlackTextureCube = CreateFlatCubeTexture("DefaultTextureCube::Black", 0xFF000000u);

	m_pSceneResource = new VkSceneResource(m_RenderDevice);
}

VkResourceManager::~VkResourceManager()
{
}

Arc< render::Texture > VkResourceManager::LoadTexture(const std::string& filepath, bool bGenerateMips, render::eTextureColorSpace colorSpace)
{
	using namespace render;

	const VkImageUsageFlags DefaultUsage = eTextureUsage_Sample | eTextureUsage_TransferDest | (bGenerateMips ? eTextureUsage_TransferSource : 0);

	fs::path path = filepath;
	if (fs::is_directory(path))
	{
		return LoadTextureArray(filepath, bGenerateMips, colorSpace);
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
				case gli::target::TARGET_1D:
				case gli::target::TARGET_1D_ARRAY:   return VK_IMAGE_TYPE_1D;
				case gli::target::TARGET_2D:
				case gli::target::TARGET_2D_ARRAY:
				case gli::target::TARGET_CUBE:
				case gli::target::TARGET_CUBE_ARRAY: return VK_IMAGE_TYPE_2D;
				case gli::target::TARGET_3D:         return VK_IMAGE_TYPE_3D;

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
		const bool bCube = gliTexture.target() == gli::target::TARGET_CUBE ||
			gliTexture.target() == gli::target::TARGET_CUBE_ARRAY;
		const bool bCubeArray = gliTexture.target() == gli::target::TARGET_CUBE_ARRAY;
		BB_ASSERT(!bCubeArray || m_RenderDevice.DeviceFeatures().imageCubeArray,
			"Vulkan cube-array textures are not supported by the selected physical device.");

		const VkFormat textureFormat = static_cast<VkFormat>(gliTexture.format());
		const auto& deviceFeatures = m_RenderDevice.DeviceFeatures();
		const bool bBC = textureFormat >= VK_FORMAT_BC1_RGB_UNORM_BLOCK &&
			textureFormat <= VK_FORMAT_BC7_SRGB_BLOCK;
		const bool bETC2 = textureFormat >= VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK &&
			textureFormat <= VK_FORMAT_EAC_R11G11_SNORM_BLOCK;
		const bool bASTC = textureFormat >= VK_FORMAT_ASTC_4x4_UNORM_BLOCK &&
			textureFormat <= VK_FORMAT_ASTC_12x12_SRGB_BLOCK;
		BB_ASSERT(!bBC || deviceFeatures.textureCompressionBC,
			"Vulkan BC texture compression is not supported by the selected physical device.");
		BB_ASSERT(!bETC2 || deviceFeatures.textureCompressionETC2,
			"Vulkan ETC2/EAC texture compression is not supported by the selected physical device.");
		BB_ASSERT(!bASTC || deviceFeatures.textureCompressionASTC_LDR,
			"Vulkan ASTC LDR texture compression is not supported by the selected physical device.");
		const u32 arrayLayers = static_cast<u32>(gliTexture.layers() * gliTexture.faces());

		VkImageCreateInfo createInfo = {};
		createInfo.sType       = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		createInfo.flags       = bCube ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;
		createInfo.imageType   = ImageTypeConverter(gliTexture.target());
		createInfo.format      = textureFormat;
		createInfo.extent      = { (u32)gliTexture.extent().x, (u32)gliTexture.extent().y, (u32)gliTexture.extent().z };
		createInfo.mipLevels   = bGenerateMips ? 
			baamboo::math::CalculateMipCount((u32)gliTexture.extent().x, (u32)gliTexture.extent().y, (u32)gliTexture.extent().z) : (u32)gliTexture.levels();
		createInfo.arrayLayers = arrayLayers;
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
		viewCreateInfo.format           = textureFormat;
		viewCreateInfo.subresourceRange =
		{
			.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT, // assume .dds is always color
			.baseMipLevel   = 0,
			.levelCount     = createInfo.mipLevels, // image may carry generated mips beyond what the file stores
			.baseArrayLayer = 0,
			.layerCount     = createInfo.arrayLayers
		};
		VK_CHECK(vkCreateImageView(m_RenderDevice.vkDevice(), &viewCreateInfo, nullptr, &vkImageView));

		auto pTex = VulkanTexture::CreateEmpty(m_RenderDevice, path.filename().string().c_str());
		pTex->SetResource(vkImage, vkImageView, createInfo, vmaAllocation, vmaAllocationInfo, VK_IMAGE_ASPECT_COLOR_BIT);

		// **
		// Upload Data
		// **
		const u32 uploadLevels = bGenerateMips ? 1u : static_cast<u32>(gliTexture.levels());
		const size_t blockSize = gli::block_size(gliTexture.format());
		BB_ASSERT(blockSize > 0, "DDS texture has an invalid texel block size.");
		const size_t bufferOffsetAlignment = std::lcm(size_t{ 4 }, blockSize);
		std::vector< u8 > uploadData;
		std::vector< VkBufferImageCopy > regions;
		regions.reserve(arrayLayers * uploadLevels);
		for (u32 layer = 0; layer < static_cast<u32>(gliTexture.layers()); ++layer)
		{
			for (u32 face = 0; face < static_cast<u32>(gliTexture.faces()); ++face)
			{
				for (u32 mip = 0; mip < uploadLevels; ++mip)
				{
					const auto extent = gliTexture.extent(mip);
					const auto* subresourceData = static_cast<const u8*>(gliTexture.data(layer, face, mip));
					const size_t subresourceSize = gliTexture.size(mip);
					const size_t alignedOffset =
						((uploadData.size() + bufferOffsetAlignment - 1) / bufferOffsetAlignment) * bufferOffsetAlignment;
					uploadData.resize(alignedOffset + subresourceSize);
					std::memcpy(uploadData.data() + alignedOffset, subresourceData, subresourceSize);

					VkBufferImageCopy region = {};
					region.bufferOffset = static_cast<VkDeviceSize>(alignedOffset);
					region.imageSubresource =
					{
						.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
						.mipLevel       = mip,
						.baseArrayLayer = layer * static_cast<u32>(gliTexture.faces()) + face,
						.layerCount     = 1
					};
					region.imageExtent = { static_cast<u32>(extent.x), static_cast<u32>(extent.y), static_cast<u32>(extent.z) };
					regions.push_back(region);
				}
			}
		}
		UploadData(pTex, uploadData.data(), static_cast<u64>(uploadData.size()), regions, bGenerateMips);

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

		auto pTex = VulkanTexture::Create(m_RenderDevice, path.filename().string().c_str(),
			{
				.resolution    = { width, height, 1 },
				.format        = GetRgba8Format(colorSpace),
				.imageUsage    = DefaultUsage,
				.bGenerateMips = bGenerateMips,
			});

		// **
		// Copy data to staging buffer
		// **
		const u64 texSizeInBytes = static_cast<u64>(width) * height * STBI_rgb_alpha;
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
		UploadData(pTex, pData, texSizeInBytes, { region }, bGenerateMips);

		stbi_image_free(pData);

		return pTex;
	}
}

Arc< render::Texture > VkResourceManager::LoadTextureArray(const fs::path& dirpath, bool bGenerateMips, render::eTextureColorSpace colorSpace)
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

	const VkImageUsageFlags arrayUsage = eTextureUsage_Sample | eTextureUsage_TransferDest | (bGenerateMips ? eTextureUsage_TransferSource : 0);
	auto pTextureArray = VulkanTexture::Create(m_RenderDevice, std::string(dirpath.filename().string() + "_Array").c_str(),
		{
			.imageType     = eImageType::Texture2D,
			.resolution    = { static_cast<u32>(baseWidth), static_cast<u32>(baseHeight), 1 },
			.format        = GetRgba8Format(colorSpace),
			.imageUsage    = arrayUsage,
			.arrayLayers   = layerCount,
			.bGenerateMips = bGenerateMips
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
	m_pStagingBuffer->FlushMappedRange(0, totalSize);

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

void VkResourceManager::UploadData(Arc< VulkanBuffer > pBuffer, const void* pData, u64 sizeInBytes, VkPipelineStageFlags2 dstStageMask, u64 dstOffsetInBytes)
{
	if (m_pStagingBuffer->SizeInBytes() < sizeInBytes)
	{
		m_pStagingBuffer->Resize(sizeInBytes);
	}
	memcpy(m_pStagingBuffer->MappedMemory(), pData, sizeInBytes);
	m_pStagingBuffer->FlushMappedRange(0, sizeInBytes);

	auto pContext = m_RenderDevice.BeginCommand(eCommandType::Graphics, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, true);
	pContext->CopyBuffer(pBuffer, m_pStagingBuffer, sizeInBytes, dstOffsetInBytes, 0);
	pContext->TransitionBufferToRead(pBuffer, dstStageMask, dstOffsetInBytes, true);
	pContext->Close();
	m_RenderDevice.ExecuteCommand(pContext);
}

void VkResourceManager::UploadData(Arc< VulkanTexture > pTexture, const void* pData, u64 sizeInBytes,
	const std::vector< VkBufferImageCopy >& regions, bool bGenerateMips)
{
	if (m_pStagingBuffer->SizeInBytes() < sizeInBytes)
	{
		m_pStagingBuffer->Resize(sizeInBytes);
	}
	memcpy(m_pStagingBuffer->MappedMemory(), pData, sizeInBytes);
	m_pStagingBuffer->FlushMappedRange(0, sizeInBytes);

	auto pContext = m_RenderDevice.BeginCommand(eCommandType::Graphics, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, true);
	pContext->CopyBuffer(pTexture, m_pStagingBuffer, regions);
	if (bGenerateMips)
	{
		VkFormatProperties formatProperties = {};
		vkGetPhysicalDeviceFormatProperties(
			m_RenderDevice.vkPhysicalDevice(), pTexture->Desc().format, &formatProperties);
		constexpr VkFormatFeatureFlags kRequiredBlitFeatures =
			VK_FORMAT_FEATURE_BLIT_SRC_BIT |
			VK_FORMAT_FEATURE_BLIT_DST_BIT |
			VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
		BB_ASSERT((formatProperties.optimalTilingFeatures & kRequiredBlitFeatures) == kRequiredBlitFeatures,
			"Vulkan texture format %d does not support linear-blit mip generation.",
			static_cast<i32>(pTexture->Desc().format));
		pContext->GenerateMips(pTexture);
	}
	pContext->Close();
	m_RenderDevice.ExecuteCommand(pContext);
}

Arc< render::Texture > VkResourceManager::CreateFlat2DTexture(const char* name, u32 color)
{
	auto flatTexture =
		VulkanTexture::Create(
			m_RenderDevice,
			name,
			{
				.resolution = { 1, 1, 1 },
				.imageUsage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT
			});

	const u32 data = color;
	VkBufferImageCopy region = {};
	region.imageSubresource =
	{
		.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
		.mipLevel       = 0,
		.baseArrayLayer = 0,
		.layerCount     = 1
	};
	region.imageExtent = { 1, 1, 1 };
	UploadData(flatTexture, &data, sizeof(data), { region });

	return flatTexture;
}

Arc< render::Texture > VkResourceManager::CreateFlat3DTexture(const char* name, u32 color)
{
	auto flatTexture =
		VulkanTexture::Create(
			m_RenderDevice,
			name,
			{
				.imageType  = render::eImageType::Texture3D,
				.resolution = { 1, 1, 1 },
				.imageUsage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT
			});

	const u32 data = color;
	VkBufferImageCopy region = {};
	region.imageSubresource =
	{
		.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
		.mipLevel       = 0,
		.baseArrayLayer = 0,
		.layerCount     = 1
	};
	region.imageExtent = { 1, 1, 1 };
	UploadData(flatTexture, &data, sizeof(data), { region });

	return flatTexture;
}

Arc< render::Texture > VkResourceManager::CreateFlatCubeTexture(const char* name, u32 color)
{
	auto flatTexture =
		VulkanTexture::Create(
			m_RenderDevice,
			name,
			{
				.imageType   = render::eImageType::TextureCube,
				.resolution  = { 1, 1, 1 },
				.imageUsage  = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
				.arrayLayers = 6,
			});

	const u32 data[6] = { color, color, color, color, color, color };
	VkBufferImageCopy region = {};
	region.imageSubresource =
	{
		.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
		.mipLevel       = 0,
		.baseArrayLayer = 0,
		.layerCount     = 6
	};
	region.imageExtent = { 1, 1, 1 };
	UploadData(flatTexture, data, sizeof(data), { region });

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
