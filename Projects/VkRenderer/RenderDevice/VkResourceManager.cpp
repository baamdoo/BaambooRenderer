#include "RendererPch.h"
#include "VkResourceManager.h"

namespace vk
{

ResourceManager::ResourceManager(RenderContext& context)
	: m_RenderContext(context)
{
}

ResourceManager::~ResourceManager()
{
}

Buffer* ResourceManager::GetStagingBuffer(u32 numElements, u64 elementSize) const
{
	Buffer::CreationInfo bufferInfo = {};
	bufferInfo.count = numElements;
	bufferInfo.elementSize = elementSize;
	bufferInfo.bMap = true;
	bufferInfo.bufferUsage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	return new Buffer(m_RenderContext, L"StagingBuffer", std::move(bufferInfo));
}

} // namespace vk