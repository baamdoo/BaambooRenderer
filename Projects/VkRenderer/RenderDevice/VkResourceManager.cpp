#include "RendererPch.h"
#include "VkResourceManager.h"
#include "VkCommandContext.h"
#include "RenderResource/VkBuffer.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>

namespace vk
{

ResourceManager::ResourceManager(RenderDevice& device)
	: m_RenderDevice(device)
{
	m_pStagingBuffer = 
		UniformBuffer::Create(m_RenderDevice, "StagingBufferPool", _MB(8), VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);

	m_pWhiteTexture = CreateFlatWhiteTexture();
	m_pBlackTexture = CreateFlatBlackTexture();
	m_pGrayTexture  = CreateFlat2DTexture("DefaultTexture::Gray", 0xFF808080u);
}

ResourceManager::~ResourceManager()
{
}

Arc< Texture > ResourceManager::LoadTexture(const std::string& filepath)
{
	fs::path path = filepath;

	u32 width, height, numChannels;
	u8* pData = stbi_load(path.string().c_str(), (int*)&width, (int*)&height, (int*)&numChannels, STBI_rgb_alpha);
	BB_ASSERT(pData, "No texture found on the path: %s", path.string().c_str());

	auto pTex = Texture::Create(m_RenderDevice, path.filename().string(),
		{
			.resolution = { width, height, 1 },
			.format     = VK_FORMAT_R8G8B8A8_UNORM,
			.imageUsage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT
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

	UploadData(pTex, (void*)pData, texSizeInBytes, region);

	RELEASE(pData);

	return pTex;
}

void ResourceManager::UploadData(Arc< Texture > pTexture, const void* pData, u64 sizeInBytes, VkBufferImageCopy region)
{
	if (m_pStagingBuffer->SizeInBytes() < sizeInBytes)
	{
		m_pStagingBuffer->Resize(sizeInBytes);
	}
	memcpy(m_pStagingBuffer->MappedMemory(), pData, sizeInBytes);

	auto& context = m_RenderDevice.BeginCommand(eCommandType::Graphics, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, true);
	context.CopyBuffer(pTexture, m_pStagingBuffer, { region });
	//if (bGenerateMips)
	//	cmdBuffer.GenerateMips(pTex);
	context.Close();
	context.Execute();
}

void ResourceManager::UploadData(Arc< Buffer > pBuffer, const void* pData, u64 sizeInBytes, VkPipelineStageFlags2 dstStageMask, u64 dstOffsetInBytes)
{
	UploadData(pBuffer->vkBuffer(), pData, sizeInBytes, dstStageMask, dstOffsetInBytes);
}

void ResourceManager::UploadData(VkBuffer vkBuffer, const void* pData, u64 sizeInBytes, VkPipelineStageFlags2 dstStageMask, u64 dstOffsetInBytes)
{
	if (m_pStagingBuffer->SizeInBytes() < sizeInBytes)
	{
		m_pStagingBuffer->Resize(sizeInBytes);
	}
	memcpy(m_pStagingBuffer->MappedMemory(), pData, sizeInBytes);

	auto& context = m_RenderDevice.BeginCommand(eCommandType::Transfer, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, true);
	context.CopyBuffer(vkBuffer, m_pStagingBuffer->vkBuffer(), sizeInBytes, dstStageMask, dstOffsetInBytes, 0);
	context.Close();
	context.Execute();
}

Arc< Texture > ResourceManager::CreateFlat2DTexture(const std::string& name, u32 color)
{
	auto pFlatTexture =
		Texture::Create(
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
		.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.mipLevel = 0,
		.baseArrayLayer = 0,
		.layerCount = 1
	};
	region.imageExtent = { 1, 1, 1 };
	UploadData(pFlatTexture, pData, sizeof(u32) * 4, region);

	RELEASE(pData);
	return pFlatTexture;
}

Arc< Texture > ResourceManager::CreateFlatWhiteTexture()
{
	return CreateFlat2DTexture("DefaultTexture::White", 0xFFFFFFFFu);
}

Arc< Texture > ResourceManager::CreateFlatBlackTexture()
{
	return CreateFlat2DTexture("DefaultTexture::Black", 0xFF000000u);
}

} // namespace vk