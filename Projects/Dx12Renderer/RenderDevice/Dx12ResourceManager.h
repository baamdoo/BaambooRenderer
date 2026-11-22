#pragma once
#include "RenderCommon/RenderResources.h"

namespace dx12
{

class Dx12Buffer;
class Dx12Texture;

class Dx12ResourceManager : public render::ResourceManager
{
public:
    Dx12ResourceManager(Dx12RenderDevice& rd);

    virtual Arc< render::Texture > LoadTexture(const std::string& filepath, bool bGenerateMips = false, render::eTextureColorSpace colorSpace = render::eTextureColorSpace::Linear) override;

    virtual void UploadData(const Arc< render::Buffer >& pBuffer, const void* pData, u64 sizeInBytes, render::ePipelineStage dstStage, u64 dstOffsetInBytes) override;
    virtual void BuildAccelerationStructures(std::span< render::BottomLevelAccelerationStructure* const > pendingBLAS, render::TopLevelAccelerationStructure* pTLAS) override;

    // Overriding to avoid resource dependency order
    // [ResourceManager] => [RenderResources]
    virtual render::SceneResource& GetSceneResource() override;

    virtual Arc< render::Texture > GetFlatWhiteTexture() override;
    virtual Arc< render::Texture > GetFlatBlackTexture() override;
    virtual Arc< render::Texture > GetFlatGrayTexture() override;

    virtual Arc< render::Texture > GetFlatWhiteTexture3D() override;
    virtual Arc< render::Texture > GetFlatBlackTexture3D() override;

    virtual Arc< render::Texture > GetFlatBlackTextureCube() override;


private:
    Arc< Dx12Texture > CreateFlat2DTexture(const char* name, u32 color);
    Arc< Dx12Texture > CreateFlat3DTexture(const char* name, u32 color);
    Arc< Dx12Texture > CreateFlatCubeTexture(const char* name, u32 color);

    Arc< Dx12Texture > LoadTextureArray(const fs::path& dirpath, bool bGenerateMips, render::eTextureColorSpace colorSpace);

private:
    Dx12RenderDevice& m_RenderDevice;

    Arc< Dx12Buffer > m_pStagingBuffer;
};

}
