#include "RendererPch.h"
#include "Dx12ResourceManager.h"
#include "Dx12DescriptorPool.h"
#include "RenderResource/Dx12Texture.h"

namespace dx12
{

ResourceManager::ResourceManager(RenderDevice& device)
    : m_RenderDevice(device)
{
    m_pViewDescriptorPool =
        MakeBox< DescriptorPool >(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, MAX_NUM_DESCRIPTOR_PER_POOL[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV]);
    m_pRtvDescriptorPool =
        MakeBox< DescriptorPool >(device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, MAX_NUM_DESCRIPTOR_PER_POOL[D3D12_DESCRIPTOR_HEAP_TYPE_RTV]);
    m_pDsvDescriptorPool =
        MakeBox< DescriptorPool >(device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, MAX_NUM_DESCRIPTOR_PER_POOL[D3D12_DESCRIPTOR_HEAP_TYPE_DSV]);
}

ResourceManager::~ResourceManager() = default;

DescriptorAllocation ResourceManager::AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE type, u32 numDescriptors)
{
    switch (type)
    {
    case D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV:
        return m_pViewDescriptorPool->Allocate(numDescriptors);
    case D3D12_DESCRIPTOR_HEAP_TYPE_RTV:
        return m_pRtvDescriptorPool->Allocate(numDescriptors);
    case D3D12_DESCRIPTOR_HEAP_TYPE_DSV:
        return m_pDsvDescriptorPool->Allocate(numDescriptors);

    default:
        assert(false && "Invalid entry!");
    }

    return DescriptorAllocation();
}

Arc< Texture > ResourceManager::CreateFlat2DTexture(std::wstring_view name, u32 color)
{
	auto pFlatTexture =
		Texture::Create(
			m_RenderDevice,
			name,
			{
				.desc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM, 1, 1),
			});

	u32* pData = (u32*)malloc(4);
	*pData = color;
    
    D3D12_SUBRESOURCE_DATA subresouceData = {};
    subresouceData.pData      = pData;
    subresouceData.RowPitch   = 4;
    subresouceData.SlicePitch = 4;
    m_RenderDevice.UpdateSubresources(pFlatTexture, 0, 1, &subresouceData);

	RELEASE(pData);
	return pFlatTexture;
}

Arc< Texture > ResourceManager::GetFlatWhiteTexture() 
{
    if (m_pWhiteTexture)
    {
        m_pWhiteTexture = CreateFlat2DTexture(L"DefaultTexture::White", 0xFFFFFFFFu);
    }
    return m_pWhiteTexture; 
}

Arc< Texture > ResourceManager::GetFlatBlackTexture()
{
    if (m_pBlackTexture)
    {
        m_pBlackTexture = CreateFlat2DTexture(L"DefaultTexture::White", 0xFF000000u);
    }
    return m_pBlackTexture;
}

Arc< Texture > ResourceManager::GetFlatGrayTexture()
{
    if (m_pGrayTexture)
    {
        m_pGrayTexture = CreateFlat2DTexture(L"DefaultTexture::White", 0xFF808080u);
    }
    return m_pGrayTexture;
}

} // namespace dx12