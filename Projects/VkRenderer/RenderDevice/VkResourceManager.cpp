#include "RendererPch.h"
#include "VkResourceManager.h"
#include "VkCommandContext.h"
#include "RenderResource/VkBuffer.h"
#include "RenderResource/VkSceneResource.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>

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

Arc< render::Texture > VkResourceManager::LoadTexture(const std::string& filepath)
{
	using namespace render;

	fs::path path = filepath;

	u32 width, height, numChannels;
	u8* pData = stbi_load(path.string().c_str(), (int*)&width, (int*)&height, (int*)&numChannels, STBI_rgb_alpha);
	BB_ASSERT(pData, "No texture found on the path: %s", path.string().c_str());

	auto tex = VulkanTexture::Create(m_RenderDevice, path.filename().string(),
		{
			.resolution = { width, height, 1 },
			.format     = eFormat::RGBA8_UNORM,
			.imageUsage = eTextureUsage_Sample | eTextureUsage_TransferDest
		});
	
	// **
	// Copy data to staging buffer
	// **
	auto texSizeInBytes = tex->SizeInBytes();
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

	UploadData(tex, (void*)pData, texSizeInBytes, region);

	RELEASE(pData);

	return tex;
}

void VkResourceManager::UploadData(Arc< VulkanTexture > texture, const void* pData, u64 sizeInBytes, VkBufferImageCopy region)
{
	if (m_pStagingBuffer->SizeInBytes() < sizeInBytes)
	{
		m_pStagingBuffer->Resize(sizeInBytes);
	}
	memcpy(m_pStagingBuffer->MappedMemory(), pData, sizeInBytes);

	auto pContext = m_RenderDevice.BeginCommand(eCommandType::Graphics, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, true);
	pContext->CopyBuffer(texture, m_pStagingBuffer, { region });
	//if (bGenerateMips)
	//	cmdBuffer.GenerateMips(pTex);
	pContext->Close();
	m_RenderDevice.ExecuteCommand(pContext);
}

void VkResourceManager::UploadData(Arc< VulkanBuffer > buffer, const void* pData, u64 sizeInBytes, VkPipelineStageFlags2 dstStageMask, u64 dstOffsetInBytes)
{
	UploadData(buffer->vkBuffer(), pData, sizeInBytes, dstStageMask, dstOffsetInBytes);
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
		.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.mipLevel = 0,
		.baseArrayLayer = 0,
		.layerCount = 1
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