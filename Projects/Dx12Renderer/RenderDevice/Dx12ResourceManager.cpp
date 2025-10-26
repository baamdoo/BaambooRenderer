#include "RendererPch.h"
#include "Dx12ResourceManager.h"
#include "Dx12DescriptorPool.h"
#include "Dx12CommandContext.h"
#include "RenderResource/Dx12Buffer.h"
#include "RenderResource/Dx12Texture.h"
#include "RenderResource/Dx12SceneResource.h"

namespace dx12
{

Dx12ResourceManager::Dx12ResourceManager(Dx12RenderDevice& rd)
    : m_RenderDevice(rd)
{
    m_pViewDescriptorPool =
        MakeBox< DescriptorPool >(m_RenderDevice, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, MAX_NUM_DESCRIPTOR_PER_POOL[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV]);
    m_pRtvDescriptorPool =
        MakeBox< DescriptorPool >(m_RenderDevice, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, MAX_NUM_DESCRIPTOR_PER_POOL[D3D12_DESCRIPTOR_HEAP_TYPE_RTV]);
    m_pDsvDescriptorPool =
        MakeBox< DescriptorPool >(m_RenderDevice, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, MAX_NUM_DESCRIPTOR_PER_POOL[D3D12_DESCRIPTOR_HEAP_TYPE_DSV]);
}

Dx12ResourceManager::~Dx12ResourceManager()
{
    ResourceManager::~ResourceManager();
}

Arc< render::Texture > Dx12ResourceManager::LoadTexture(const std::string& filepath)
{
    auto d3d12Device = m_RenderDevice.GetD3D12Device();

    fs::path path = filepath;
    auto extension = path.extension().string();

    std::unique_ptr< u8[] > rawData;
    ID3D12Resource* d3d12TexResource = nullptr;

    auto pTex = MakeArc< Dx12Texture >(m_RenderDevice, path.string());
    if (extension == ".dds")
    {
        std::vector< D3D12_SUBRESOURCE_DATA > subresourceData;
        DX_CHECK(DirectX::LoadDDSTextureFromFile(
            d3d12Device, path.c_str(), &d3d12TexResource, rawData, subresourceData));

        UINT subresourceSize = (UINT)subresourceData.size();

        pTex->SetD3D12Resource(d3d12TexResource);
        m_RenderDevice.UpdateSubresources(pTex.get(), 0, subresourceSize, subresourceData.data());
    }
    else
    {
        D3D12_SUBRESOURCE_DATA subresouceData = {};
        DX_CHECK(DirectX::LoadWICTextureFromFile(
            d3d12Device, path.c_str(), &d3d12TexResource, rawData, subresouceData));

        pTex->SetD3D12Resource(d3d12TexResource);

        UINT subresouceSize = 1;
        m_RenderDevice.UpdateSubresources(pTex.get(), 0, subresouceSize, &subresouceData);
    }

    return pTex;
}

void Dx12ResourceManager::UploadData(Dx12Resource* pResource, const void* pData, u64 sizeInBytes, u64 dstOffsetInBytes, D3D12_RESOURCE_STATES stateAfter)
{
    if (!m_pStagingBuffer)
    {
        m_pStagingBuffer =
            Dx12Buffer::Create(m_RenderDevice, "StagingBufferPool", 
                {
                    .count              = 1,
                    .elementSizeInBytes = _MB(8),
                    .bMap               = true,
                    .bufferUsage        = render::eBufferUsage_TransferSource | render::eBufferUsage_TransferDest
                });
    }

    if (m_pStagingBuffer->SizeInBytes() < sizeInBytes)
    {
        m_pStagingBuffer->Resize(sizeInBytes);
    }
    memcpy(m_pStagingBuffer->GetSystemMemoryAddress(), pData, sizeInBytes);

    auto pContext = m_RenderDevice.BeginCommand(D3D12_COMMAND_LIST_TYPE_DIRECT);
    {
        pContext->TransitionBarrier(pResource, D3D12_RESOURCE_STATE_COPY_DEST);
        pContext->CopyBuffer(pResource->GetD3D12Resource(), m_pStagingBuffer->GetD3D12Resource(), sizeInBytes, dstOffsetInBytes);
        if (stateAfter != D3D12_RESOURCE_STATE_COMMON)
            pContext->TransitionBarrier(pResource, stateAfter);
        pContext->Close();
    }
    m_RenderDevice.ExecuteCommand(std::move(pContext)).Wait();
}

void Dx12ResourceManager::UploadData(Arc< Dx12Buffer > pBuffer, const void* pData, u64 sizeInBytes, u64 dstOffsetInBytes, D3D12_RESOURCE_STATES stateAfter)
{
    UploadData(pBuffer.get(), pData, sizeInBytes, dstOffsetInBytes, stateAfter);
}

void Dx12ResourceManager::UploadData(Arc< Dx12Texture > pTexture, const void* pData, u64 sizeInBytes, D3D12_RESOURCE_STATES stateAfter)
{
    UploadData(pTexture.get(), pData, sizeInBytes, 0, stateAfter);
}

DescriptorAllocation Dx12ResourceManager::AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE type, u32 numDescriptors)
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
        __debugbreak();
        assert(false && "Invalid entry!");
    }

    return DescriptorAllocation();
}

render::SceneResource& Dx12ResourceManager::GetSceneResource()
{
    if (!m_pSceneResource)
    {
        m_pSceneResource = new Dx12SceneResource(m_RenderDevice);
    }

    return *m_pSceneResource;
}

Arc< render::Texture > Dx12ResourceManager::GetFlatWhiteTexture()
{
    if (!m_pWhiteTexture)
    {
        m_pWhiteTexture = CreateFlat2DTexture("DefaultTexture::White", 0xFFFFFFFFu);
    }
    return m_pWhiteTexture;
}

Arc< render::Texture > Dx12ResourceManager::GetFlatBlackTexture()
{
    if (!m_pBlackTexture)
    {
        m_pBlackTexture = CreateFlat2DTexture("DefaultTexture::White", 0xFF000000u);
    }
    return m_pBlackTexture;
}

Arc< render::Texture > Dx12ResourceManager::GetFlatGrayTexture()
{
    if (!m_pGrayTexture)
    {
        m_pGrayTexture = CreateFlat2DTexture("DefaultTexture::Gray", 0xFF808080u);
    }
    return m_pGrayTexture;
}

Arc< Dx12Texture > Dx12ResourceManager::CreateFlat2DTexture(const std::string& name, u32 color)
{
    using namespace render;

	auto pFlatTexture =
		Dx12Texture::Create(
			m_RenderDevice,
			name,
			{
                .resolution = { 1, 1, 0 },
                .format     = eFormat::RGBA8_UNORM,
			});

	u32* pData = (u32*)malloc(4);
	*pData = color;
    
    D3D12_SUBRESOURCE_DATA subresouceData = {};
    subresouceData.pData      = pData;
    subresouceData.RowPitch   = 4;
    subresouceData.SlicePitch = 4;
    m_RenderDevice.UpdateSubresources(pFlatTexture.get(), 0, 1, &subresouceData);

	RELEASE(pData);
	return pFlatTexture;
}

} // namespace dx12