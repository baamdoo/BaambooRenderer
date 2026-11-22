#pragma once
#include "RenderCommon/RenderResources.h"

namespace vk
{

class VulkanTexture;
class VulkanUniformBuffer;

class VkResourceManager : public render::ResourceManager
{
public:
    VkResourceManager(VkRenderDevice& rd);
    ~VkResourceManager();

    virtual Arc< render::Texture > LoadTexture(const std::string& filepath, bool bGenerateMips = false, render::eTextureColorSpace colorSpace = render::eTextureColorSpace::Linear) override;

    virtual void UploadData(const Arc< render::Buffer >& pBuffer, const void* pData, u64 sizeInBytes, render::ePipelineStage dstStage, u64 dstOffsetInBytes) override;
    virtual void BuildAccelerationStructures(std::span< render::BottomLevelAccelerationStructure* const > pendingBLAS, render::TopLevelAccelerationStructure* pTLAS) override;
    void UploadData(Arc< VulkanTexture > pTexture, const void* pData, u64 sizeInBytes, const std::vector< VkBufferImageCopy >& regions, bool bGenerateMips = false);

private:
    Arc< render::Texture > CreateFlat2DTexture(const char* name, u32 color);
    Arc< render::Texture > CreateFlat3DTexture(const char* name, u32 color);
    Arc< render::Texture > CreateFlatCubeTexture(const char* name, u32 color);
    Arc< render::Texture > CreateFlatWhiteTexture();
    Arc< render::Texture > CreateFlatBlackTexture();

    Arc< render::Texture > LoadTextureArray(const fs::path& dirpath, bool bGenerateMips = false, render::eTextureColorSpace colorSpace = render::eTextureColorSpace::Linear);

private:
    VkRenderDevice& m_RenderDevice;

    Arc< VulkanUniformBuffer > m_pStagingBuffer;
};

} // namespace vk
