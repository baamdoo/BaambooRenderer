#include "RendererPch.h"
#include "Dx12ResourceManager.h"
#include "Dx12DescriptorPool.h"
#include "Dx12CommandContext.h"
#include "RenderResource/Dx12Buffer.h"
#include "RenderResource/Dx12Texture.h"
#include "RenderResource/Dx12SceneResource.h"

#include <DirectXTex.h>

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

Arc< render::Texture > Dx12ResourceManager::LoadTexture(const std::string& filepath, bool bGenerateMips)
{
    auto d3d12Device = m_RenderDevice.GetD3D12Device();

    fs::path path  = filepath;
    auto extension = path.extension().string();
    if (fs::is_directory(path))
    {
        return LoadTextureArray(filepath, bGenerateMips);
    }

    std::unique_ptr< u8[] > rawData;
    ID3D12Resource* d3d12TexResource = nullptr;

    auto pTex = Dx12Texture::CreateEmpty(m_RenderDevice, path.string());
    if (extension == ".dds")
    {
        std::vector< D3D12_SUBRESOURCE_DATA > subresourceDatas;
        DX_CHECK(DirectX::LoadDDSTextureFromFile(
            d3d12Device, path.c_str(), &d3d12TexResource, rawData, subresourceDatas));

        UINT subresourceSize = (UINT)subresourceDatas.size();

        pTex->SetD3D12Resource(d3d12TexResource);
        m_RenderDevice.UpdateSubresources(pTex.get(), 0, subresourceSize, subresourceDatas.data());
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

Arc< Dx12Texture > Dx12ResourceManager::LoadTextureArray(const fs::path& dirpath, bool bGenerateMips)
{
    using namespace render;

    std::vector< std::pair< std::wstring, std::wstring > > imagePaths;
    for (const auto& entry : fs::directory_iterator(dirpath))
    {
        if (entry.is_regular_file())
        {
            std::wstring ext = entry.path().extension().wstring();
            imagePaths.emplace_back(entry.path().stem().wstring(), ext);
        }
    }

    if (imagePaths.empty())
    {
        return nullptr;
    }

    auto extractNumber = [](const std::wstring& name) -> int
        {
            size_t delimiter = name.find_last_of('_');
            if (delimiter != std::string::npos && delimiter < name.length() - 1)
            {
                std::wstring numberStr = name.substr(delimiter + 1);
                if (!numberStr.empty() && std::all_of(numberStr.begin(), numberStr.end(), ::isdigit))
                {
                    return std::stoi(numberStr);
                }
            }

            return -1;
        };

    std::sort(imagePaths.begin(), imagePaths.end(), [&extractNumber](const auto& strA, const auto& strB)
        {
            const int numA = extractNumber(strA.first);
            const int numB = extractNumber(strB.first);

            if (numA != -1 && numB != -1)
            {
                return numA < numB;
            }
            else if (numA != -1)
            {
                return true;
            }
            else if (numB != -1)
            {
                return false;
            }
            else
            {
                return strA.first < strB.first;
            }
        });

    std::vector< DirectX::ScratchImage > images(imagePaths.size());
    DirectX::TexMetadata firstMetadata = {};

    for (size_t i = 0; i < imagePaths.size(); ++i)
    {
        const auto& imagePathPair = imagePaths[i];

        DirectX::TexMetadata metadata;
        DirectX::ScratchImage& image = images[i];

        std::wstring ext     = imagePathPair.second;
        std::wstring imgPath = imagePathPair.first;

        HRESULT hr;
        if (ext == L".dds")
        {
            hr = DirectX::LoadFromDDSFile((dirpath.wstring() + L"/" + imgPath + ext).c_str(), DirectX::DDS_FLAGS_NONE, &metadata, image);
        }
        else // WIC (PNG, JPG, etc.)
        {
            hr = DirectX::LoadFromWICFile((dirpath.wstring() + L"/" + imgPath + ext).c_str(), DirectX::WIC_FLAGS_NONE, &metadata, image);
            if (SUCCEEDED(hr) && bGenerateMips && metadata.mipLevels == 1)
            {
                DirectX::ScratchImage mips;
                hr = DirectX::GenerateMipMaps(*image.GetImage(0, 0, 0), DirectX::TEX_FILTER_DEFAULT, 0, mips);
                if (SUCCEEDED(hr))
                {
                    image = std::move(mips);
                    metadata = image.GetMetadata();
                }
            }
        }
        if (FAILED(hr))
        {
            __debugbreak();
            return nullptr;
        }

        if (i == 0)
        {
            firstMetadata = metadata;
            if (firstMetadata.dimension != DirectX::TEX_DIMENSION_TEXTURE2D)
            {
                __debugbreak();
                return nullptr;
            }
        }
        else if (metadata.width != firstMetadata.width 
              || metadata.height != firstMetadata.height 
              || metadata.format != firstMetadata.format 
              || metadata.mipLevels != firstMetadata.mipLevels)
        {
            __debugbreak();
            return nullptr;
        }
    }

    D3D12_RESOURCE_DESC texArrayDesc = {};
    texArrayDesc.Dimension          = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texArrayDesc.Alignment          = 0;
    texArrayDesc.Width              = static_cast<UINT>(firstMetadata.width);
    texArrayDesc.Height             = static_cast<UINT>(firstMetadata.height);
    texArrayDesc.DepthOrArraySize   = static_cast<UINT16>(images.size());
    texArrayDesc.MipLevels          = static_cast<UINT16>(firstMetadata.mipLevels);
    texArrayDesc.Format             = firstMetadata.format;
    texArrayDesc.SampleDesc.Count   = 1;
    texArrayDesc.SampleDesc.Quality = 0;
    texArrayDesc.Layout             = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    texArrayDesc.Flags              = D3D12_RESOURCE_FLAG_NONE;

    ID3D12Resource* d3d12TexArrayResource = nullptr;
    auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

    DX_CHECK(m_RenderDevice.GetD3D12Device()->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &texArrayDesc,
        D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        IID_PPV_ARGS(&d3d12TexArrayResource)
    ));

    std::vector<D3D12_SUBRESOURCE_DATA> subresourceData;
    subresourceData.reserve(images.size() * firstMetadata.mipLevels);
    for (size_t arraySlice = 0; arraySlice < images.size(); ++arraySlice)
    {
        for (size_t mipLevel = 0; mipLevel < firstMetadata.mipLevels; ++mipLevel)
        {
            const DirectX::Image* img = images[arraySlice].GetImage(mipLevel, 0, 0);
            if (!img)
            {
                __debugbreak();
                return nullptr;
            }

            D3D12_SUBRESOURCE_DATA sd = {};
            sd.pData      = img->pixels;
            sd.RowPitch   = static_cast<LONG_PTR>(img->rowPitch);
            sd.SlicePitch = static_cast<LONG_PTR>(img->slicePitch);
            subresourceData.push_back(sd);
        }
    }

    auto pTex = Dx12Texture::CreateEmpty(m_RenderDevice, dirpath.string());
    pTex->SetD3D12Resource(d3d12TexArrayResource);

    UINT totalSubresources = (UINT)subresourceData.size();
    m_RenderDevice.UpdateSubresources(pTex.get(), 0, totalSubresources, subresourceData.data());

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