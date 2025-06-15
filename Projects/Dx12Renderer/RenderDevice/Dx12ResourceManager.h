#pragma once

namespace dx12
{

class DescriptorPool;
class Buffer; 
class Texture;

class ResourceManager
{
public:
    ResourceManager(RenderDevice& device);
    ~ResourceManager();

    [[nodiscard]]
    DescriptorAllocation AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE type, u32 numDescriptors = 1);

    [[nodiscard]]
    Arc< Texture > GetFlatWhiteTexture();
    [[nodiscard]]
    Arc< Texture > GetFlatBlackTexture();
    [[nodiscard]]
    Arc< Texture > GetFlatGrayTexture();

private:
    Arc< Texture > CreateFlat2DTexture(std::wstring_view name, u32 color);

private:
    RenderDevice& m_RenderDevice;

    Box< DescriptorPool > m_pViewDescriptorPool;
    Box< DescriptorPool > m_pRtvDescriptorPool;
    Box< DescriptorPool > m_pDsvDescriptorPool;

    // default textures
    Arc< Texture > m_pWhiteTexture;
    Arc< Texture > m_pBlackTexture;
    Arc< Texture > m_pGrayTexture;
};

}
