#include "RendererPch.h"
#include "VkResourceManager.h"
#include "VkCommandQueue.h"
#include "VkCommandBuffer.h"
#include "BaambooUtils/Math.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>

namespace vk
{

ResourceManager::ResourceManager(RenderContext& context)
	: m_renderContext(context)
{
}

ResourceManager::~ResourceManager()
{
}

VertexHandle ResourceManager::CreateVertexBuffer(std::wstring_view name, u32 numVertices, u64 elementSizeInBytes, void* data)
{
	u64 sizeInBytes = u64(elementSizeInBytes) * numVertices;

	// staging buffer
	Buffer::CreationInfo creationInfo = {};
	creationInfo.count = numVertices;
	creationInfo.elementSize = elementSizeInBytes;
	creationInfo.bMap = true;
	creationInfo.bufferUsage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	auto pStagingBuffer = new Buffer(m_renderContext, L"StagingBuffer", std::move(creationInfo));
	memcpy(pStagingBuffer->MappedMemory(), data, sizeInBytes);

	// target buffer
	creationInfo = {};
	creationInfo.count = numVertices;
	creationInfo.elementSize = elementSizeInBytes;
	creationInfo.bMap = false;
	creationInfo.bufferUsage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	auto vb = Create< Buffer >(name, std::move(creationInfo));

	// copy
	auto& cmdBuffer = m_renderContext.GraphicsQueue().Allocate(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
	cmdBuffer.CopyBuffer(pStagingBuffer, Get(vb));
	cmdBuffer.Close();
	m_renderContext.GraphicsQueue().ExecuteCommandBuffer(cmdBuffer);

	RELEASE(pStagingBuffer);

	VertexHandle handle = {};
	handle.vb = static_cast<u32>(vb);
	handle.vOffset = 0;
	handle.vCount = numVertices;
	return handle;
}

IndexHandle ResourceManager::CreateIndexBuffer(std::wstring_view name, u32 numIndices, u64 elementSizeInBytes, void* data)
{
	u64 sizeInBytes = elementSizeInBytes * numIndices;

	// staging buffer
	Buffer::CreationInfo creationInfo = {};
	creationInfo.count = numIndices;
	creationInfo.elementSize = elementSizeInBytes;
	creationInfo.bMap = true;
	creationInfo.bufferUsage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	auto pStagingBuffer = new Buffer(m_renderContext, L"StagingBuffer", std::move(creationInfo));
	memcpy(pStagingBuffer->MappedMemory(), data, sizeInBytes);

	// target buffer
	creationInfo = {};
	creationInfo.count = numIndices;
	creationInfo.elementSize = elementSizeInBytes;
	creationInfo.bMap = false;
	creationInfo.bufferUsage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	auto ib = Create< Buffer >(name, std::move(creationInfo));

	// copy
	auto& cmdBuffer = m_renderContext.GraphicsQueue().Allocate(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
	cmdBuffer.CopyBuffer(pStagingBuffer, Get(ib));
	cmdBuffer.Close();
	m_renderContext.GraphicsQueue().ExecuteCommandBuffer(cmdBuffer);

	RELEASE(pStagingBuffer);

	IndexHandle handle = {};
	handle.ib = static_cast<u32>(ib);
	handle.iOffset = 0;
	handle.iCount = numIndices;
	return handle;
}

TextureHandle ResourceManager::CreateTexture(std::string_view filepath, bool bGenerateMips)
{
	fs::path path = filepath;

	u32 width, height, numChannels;
	u8* data = stbi_load(path.string().c_str(), (int*)&width, (int*)&height, (int*)&numChannels, 0);

	Texture::CreationInfo texInfo = {};
	texInfo.resolution = { width, height, 1 };
	texInfo.format = numChannels == 1 ?
		VK_FORMAT_R8_UNORM : numChannels == 2 ? 
		VK_FORMAT_R8G8_UNORM : numChannels == 3 ? 
		VK_FORMAT_R8G8B8_UNORM : VK_FORMAT_R8G8B8A8_UNORM;
	texInfo.bGenerateMips = bGenerateMips;
	texInfo.imageUsage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	auto handle = Create< Texture >(path.filename().wstring(), std::move(texInfo));

	// **
	// Copy data to staging buffer
	// **
	auto pTex = Get(handle);
	auto texSize = pTex->SizeInBytes();

	auto& cmdBuffer = m_renderContext.GraphicsQueue().Allocate(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
	Buffer::CreationInfo bufferInfo = {};
	bufferInfo.count = 1;
	bufferInfo.elementSize = texSize;
	bufferInfo.bMap = true;
	bufferInfo.bufferUsage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	auto pStagingBuffer = new Buffer(m_renderContext, L"StagingBuffer", std::move(bufferInfo));
	memcpy(pStagingBuffer->MappedMemory(), data, texSize);

	VkBufferImageCopy region = {};
	region.bufferOffset = 0;
	region.bufferRowLength = 0;
	region.bufferImageHeight = 0;
	region.imageSubresource = {
		.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.mipLevel = 1,
		.baseArrayLayer = 0,
		.layerCount = 1
	};
	region.imageExtent = { width, height, 1 };

	cmdBuffer.CopyBuffer(pTex, pStagingBuffer, { region });
	if (bGenerateMips)
		cmdBuffer.GenerateMips(pTex);

	RELEASE(pStagingBuffer);
	return static_cast<u32>(handle);
}

} // namespace vk