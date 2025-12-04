#pragma once
#include "RenderCommon/RenderResources.h"

namespace dx12
{

class DescriptorPool;
class Dx12Buffer;
class Dx12ConstantBuffer;
class Dx12Texture;
class Dx12RootSignature;

class Dx12ResourceManager : public render::ResourceManager
{
public:
    Dx12ResourceManager(Dx12RenderDevice& rd);
    ~Dx12ResourceManager();

    virtual Arc< render::Texture > LoadTexture(const std::string& filepath, bool bGenerateMips = false) override;

    void UploadData(Dx12Resource* pResource, const void* pData, u64 sizeInBytes, u64 dstOffsetInBytes, D3D12_RESOURCE_STATES stateAfter);
    void UploadData(Arc< Dx12Buffer > pBuffer, const void* pData, u64 sizeInBytes, u64 dstOffsetInBytes, D3D12_RESOURCE_STATES stateAfter);
    void UploadData(Arc< Dx12Texture > pTexture, const void* pData, u64 sizeInBytes, D3D12_RESOURCE_STATES stateAfter);

    [[nodiscard]]
    DescriptorAllocation AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE type, u32 numDescriptors = 1);

    // Overriding to avoid resource dependency order
    // [ResourceManager] => [RenderResources]
    virtual render::SceneResource& GetSceneResource() override;

    virtual Arc< render::Texture > GetFlatWhiteTexture() override;
    virtual Arc< render::Texture > GetFlatBlackTexture() override;
    virtual Arc< render::Texture > GetFlatGrayTexture() override;

    virtual Arc< render::Texture > GetFlatWhiteTexture3D() override;
    virtual Arc< render::Texture > GetFlatBlackTexture3D() override;

    Arc< Dx12RootSignature > GetGlobalRootSignature() const;
    Arc< DescriptorPool > GetGlobalDescriptorHeap() const;


private:
    Arc< Dx12Texture > CreateFlat2DTexture(const char* name, u32 color);
    Arc< Dx12Texture > CreateFlat3DTexture(const char* name, u32 color);

    Arc< Dx12Texture > LoadTextureArray(const fs::path& dirpath, bool bGenerateMips);

private:
    Dx12RenderDevice& m_RenderDevice;

    Arc< Dx12Buffer > m_pStagingBuffer;

    
    Box< DescriptorPool > m_pRtvDescriptorPool;
    Box< DescriptorPool > m_pDsvDescriptorPool;
    Arc< DescriptorPool > m_pGlobalDescriptorHeap;

    Arc< Dx12RootSignature > m_pGlobalRootSignature;
};

}
