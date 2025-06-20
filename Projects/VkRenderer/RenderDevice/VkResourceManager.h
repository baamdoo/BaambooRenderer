#pragma once

namespace vk
{

class Buffer;
class Texture;
class UniformBuffer;

enum
{
    eDefaultTexture_White = 0,
    eDefaultTexture_Black = 1,
    eDefaultTexture_Gray  = 2,
};

class ResourceManager
{
public:
    ResourceManager(RenderDevice& device);
    ~ResourceManager();

    void UploadData(Arc< Texture > pTexture, const void* pData, u64 sizeInBytes, VkBufferImageCopy region);
    void UploadData(Arc< Buffer > pBuffer, const void* pData, u64 sizeInBytes, VkPipelineStageFlags2 dstStageMask, u64 dstOffsetInBytes);
    void UploadData(VkBuffer vkBuffer, const void* pData, u64 sizeInBytes, VkPipelineStageFlags2 dstStageMask, u64 dstOffsetInBytes);

    [[nodiscard]]
    Arc< Texture > GetFlatWhiteTexture() { return m_pWhiteTexture; }
    [[nodiscard]]
    Arc< Texture > GetFlatBlackTexture() { return m_pBlackTexture; }
    [[nodiscard]]
    Arc< Texture > GetFlatGrayTexture() { return m_pGrayTexture; }

private:
    Arc< Texture > CreateFlat2DTexture(const std::string& name, u32 color);
    Arc< Texture > CreateFlatWhiteTexture();
    Arc< Texture > CreateFlatBlackTexture();

private:
    RenderDevice& m_RenderDevice;

    Arc< UniformBuffer > m_pStagingBuffer;

    // default textures
    Arc< Texture > m_pWhiteTexture;
    Arc< Texture > m_pBlackTexture;
    Arc< Texture > m_pGrayTexture;
};

} // namespace vk