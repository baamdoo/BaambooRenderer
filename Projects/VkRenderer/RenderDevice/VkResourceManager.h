#pragma once
#include "RenderCommon/RenderResources.h"

namespace vk
{

class VulkanBuffer;
class VulkanTexture;
class VulkanUniformBuffer;

enum
{
    eDefaultTexture_White = 0,
    eDefaultTexture_Black = 1,
    eDefaultTexture_Gray  = 2,
};

class VkResourceManager : public render::ResourceManager
{
public:
    VkResourceManager(VkRenderDevice& rd);
    ~VkResourceManager();

    virtual Arc< render::Texture > LoadTexture(const std::string& filepath) override;

    void UploadData(Arc< VulkanTexture > texture, const void* pData, u64 sizeInBytes, VkBufferImageCopy region);
    void UploadData(Arc< VulkanBuffer > buffer, const void* pData, u64 sizeInBytes, VkPipelineStageFlags2 dstStageMask, u64 dstOffsetInBytes);
    void UploadData(VkBuffer vkBuffer, const void* pData, u64 sizeInBytes, VkPipelineStageFlags2 dstStageMask, u64 dstOffsetInBytes);

private:
    Arc< render::Texture > CreateFlat2DTexture(const std::string& name, u32 color);
    Arc< render::Texture > CreateFlatWhiteTexture();
    Arc< render::Texture > CreateFlatBlackTexture();

private:
    VkRenderDevice& m_RenderDevice;

    Arc< VulkanUniformBuffer > m_pStagingBuffer;
};

} // namespace vk