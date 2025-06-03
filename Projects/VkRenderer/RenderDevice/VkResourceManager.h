#pragma once

namespace vk
{

class Buffer;
class Texture;
class UniformBuffer;

class ResourceManager
{
public:
    ResourceManager(RenderDevice& device);
    ~ResourceManager();

    void UploadData(Arc< Texture > pTexture, const void* pData, u64 sizeInBytes, VkBufferImageCopy region);
    void UploadData(Arc< Buffer > pBuffer, const void* pData, u64 sizeInBytes, VkPipelineStageFlags2 dstStageMask, u64 dstOffsetInBytes);
    void UploadData(VkBuffer vkBuffer, const void* pData, u64 sizeInBytes, VkPipelineStageFlags2 dstStageMask, u64 dstOffsetInBytes);

private:
    RenderDevice& m_RenderDevice;

    Arc< UniformBuffer > m_pStagingBuffer;
};

} // namespace vk