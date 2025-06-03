#include "RendererPch.h"
#include "VkResourceManager.h"
#include "VkCommandContext.h"
#include "RenderResource/VkBuffer.h"

namespace vk
{

ResourceManager::ResourceManager(RenderDevice& device)
	: m_RenderDevice(device)
{
	m_pStagingBuffer = 
		UniformBuffer::Create(m_RenderDevice, "StagingBufferPool", _MB(8), VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
}

ResourceManager::~ResourceManager()
{
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

} // namespace vk