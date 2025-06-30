#include "RendererPch.h"
#include "Dx12Texture.h"
#include "RenderDevice/Dx12RenderDevice.h"
#include "RenderDevice/Dx12ResourceManager.h"

namespace dx12
{

bool IsTypelessFormat(DXGI_FORMAT format)
{
    return format == DXGI_FORMAT_R32G32B32A32_TYPELESS
        || format == DXGI_FORMAT_R32G32B32_TYPELESS
        || format == DXGI_FORMAT_R16G16B16A16_TYPELESS
        || format == DXGI_FORMAT_R32G32_TYPELESS
        || format == DXGI_FORMAT_R32G8X24_TYPELESS
        || format == DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS
        || format == DXGI_FORMAT_R10G10B10A2_TYPELESS
        || format == DXGI_FORMAT_R8G8B8A8_TYPELESS
        || format == DXGI_FORMAT_R16G16_TYPELESS
        || format == DXGI_FORMAT_R32_TYPELESS
        || format == DXGI_FORMAT_R24G8_TYPELESS
        || format == DXGI_FORMAT_R8G8_TYPELESS
        || format == DXGI_FORMAT_R16_TYPELESS
        || format == DXGI_FORMAT_R8_TYPELESS
        || format == DXGI_FORMAT_BC1_TYPELESS
        || format == DXGI_FORMAT_BC2_TYPELESS
        || format == DXGI_FORMAT_BC3_TYPELESS
        || format == DXGI_FORMAT_BC4_TYPELESS
        || format == DXGI_FORMAT_BC5_TYPELESS
        || format == DXGI_FORMAT_BC6H_TYPELESS
        || format == DXGI_FORMAT_BC7_TYPELESS
        || format == DXGI_FORMAT_B8G8R8A8_TYPELESS
        || format == DXGI_FORMAT_B8G8R8X8_TYPELESS;
}

DXGI_FORMAT ConvertToViewFormat(DXGI_FORMAT resourceFormat, bool bIsSRV)
{
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    switch (resourceFormat)
    {
    case DXGI_FORMAT_R32G32B32A32_TYPELESS:
        format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        break;
    case DXGI_FORMAT_R32G32B32_TYPELESS:
        format = DXGI_FORMAT_R32G32B32_FLOAT;
        break;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS:
        format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        break;
    case DXGI_FORMAT_R32G32_TYPELESS:
        format = DXGI_FORMAT_R32G32_FLOAT;
        break;
    case DXGI_FORMAT_R32G8X24_TYPELESS:
        format = DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
        break;
    case DXGI_FORMAT_R10G10B10A2_TYPELESS:
        format = DXGI_FORMAT_R10G10B10A2_UNORM;
        break;
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:
        format = DXGI_FORMAT_R8G8B8A8_UNORM;
        break;
    case DXGI_FORMAT_R16G16_TYPELESS:
        format = DXGI_FORMAT_R16G16_FLOAT;
        break;
    case DXGI_FORMAT_R32_TYPELESS:
        format = bIsSRV ? DXGI_FORMAT_R32_FLOAT : DXGI_FORMAT_D32_FLOAT;
        break;
    case DXGI_FORMAT_R24G8_TYPELESS:
        format = bIsSRV ? DXGI_FORMAT_R24_UNORM_X8_TYPELESS : DXGI_FORMAT_D24_UNORM_S8_UINT;
        break;
    case DXGI_FORMAT_R8G8_TYPELESS:
        format = DXGI_FORMAT_R8G8_UNORM;
        break;
    case DXGI_FORMAT_R16_TYPELESS:
        format = bIsSRV ? DXGI_FORMAT_R16_FLOAT : DXGI_FORMAT_D16_UNORM;
        break;
    case DXGI_FORMAT_R8_TYPELESS:
        format = DXGI_FORMAT_R8_UNORM;
        break;
    }

    return format;
}

D3D12_RENDER_TARGET_VIEW_DESC GetRTVDesc(const D3D12_RESOURCE_DESC& resDesc, UINT mip, UINT arraySlice = 0)
{
    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
    rtvDesc.Format = resDesc.Format;

    switch (resDesc.Dimension)
    {
    case D3D12_RESOURCE_DIMENSION_TEXTURE1D:
        if (resDesc.DepthOrArraySize > 1)
        {
            rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE1DARRAY;
            rtvDesc.Texture1DArray.ArraySize = 1;
            rtvDesc.Texture1DArray.FirstArraySlice = arraySlice;
            rtvDesc.Texture1DArray.MipSlice = mip;
        }
        else
        {
            rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE1D;
            rtvDesc.Texture1D.MipSlice = mip;
        }
        break;
    case D3D12_RESOURCE_DIMENSION_TEXTURE2D:
        if (resDesc.DepthOrArraySize > 1)
        {
            rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
            rtvDesc.Texture2DArray.ArraySize = 1;
            rtvDesc.Texture2DArray.FirstArraySlice = arraySlice;
            rtvDesc.Texture2DArray.MipSlice = mip;
        }
        else
        {
            rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
            rtvDesc.Texture2D.MipSlice = mip;
        }
        break;
    default:
        throw std::exception("Invalid resource dimension.");
    }

    return rtvDesc;
}

D3D12_DEPTH_STENCIL_VIEW_DESC GetDSVDesc(const D3D12_RESOURCE_DESC& resDesc, UINT mip, UINT arraySlice = 0, UINT arraySize = 1)
{
    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format = resDesc.Format;

    switch (resDesc.Dimension)
    {
    case D3D12_RESOURCE_DIMENSION_TEXTURE1D:
        if (resDesc.DepthOrArraySize > 1)
        {
            dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE1DARRAY;
            dsvDesc.Texture1DArray.ArraySize = arraySize;
            dsvDesc.Texture1DArray.FirstArraySlice = arraySlice;
            dsvDesc.Texture1DArray.MipSlice = mip;
        }
        else
        {
            dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE1D;
            dsvDesc.Texture1D.MipSlice = mip;
        }
        break;
    case D3D12_RESOURCE_DIMENSION_TEXTURE2D:
        if (resDesc.DepthOrArraySize > 1)
        {
            dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
            dsvDesc.Texture2DArray.ArraySize = arraySize;
            dsvDesc.Texture2DArray.FirstArraySlice = arraySlice;
            dsvDesc.Texture2DArray.MipSlice = mip;
        }
        else
        {
            dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
            dsvDesc.Texture2D.MipSlice = mip;
        }
        break;
    default:
        throw std::exception("Invalid resource dimension.");
    }

    return dsvDesc;
}

D3D12_SHADER_RESOURCE_VIEW_DESC GetSRVDesc(const D3D12_RESOURCE_DESC& resDesc, UINT mip, UINT mipLevels = -1, UINT arraySlice = 0, UINT arraySize = 1, UINT planeSlice = 0, bool bTextureCube = false)
{
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = resDesc.Format;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

    switch (resDesc.Dimension)
    {
    case D3D12_RESOURCE_DIMENSION_TEXTURE1D:
        if (resDesc.DepthOrArraySize > 1)
        {
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1DARRAY;
            srvDesc.Texture1DArray.ArraySize = arraySize;
            srvDesc.Texture1DArray.FirstArraySlice = arraySlice;
        }
        else
        {
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1D;
        }
        break;
    case D3D12_RESOURCE_DIMENSION_TEXTURE2D:
        if (bTextureCube)
        {
            if (resDesc.DepthOrArraySize / 6 > 1)
            {
                srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBEARRAY;
                srvDesc.TextureCubeArray.MipLevels = mipLevels;
                srvDesc.TextureCubeArray.MostDetailedMip = mip;
                srvDesc.TextureCubeArray.First2DArrayFace = arraySlice;
                srvDesc.TextureCubeArray.NumCubes = resDesc.DepthOrArraySize / 6;
            }
            else
            {
                srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
                srvDesc.TextureCube.MipLevels = mipLevels;
            }
        }
        else
        {
            if (resDesc.DepthOrArraySize > 1)
            {
                srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
                srvDesc.Texture2DArray.MipLevels = mipLevels;
                srvDesc.Texture2DArray.MostDetailedMip = mip;
                srvDesc.Texture2DArray.ArraySize = arraySize;
                srvDesc.Texture2DArray.FirstArraySlice = arraySlice;
                srvDesc.Texture2DArray.PlaneSlice = planeSlice;
            }
            else
            {
                srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                srvDesc.Texture2D.MostDetailedMip = mip;
                srvDesc.Texture2D.PlaneSlice = planeSlice;
                srvDesc.Texture2D.MipLevels = mipLevels;
            }
        }
        break;
    case D3D12_RESOURCE_DIMENSION_TEXTURE3D:
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
        srvDesc.Texture3D.MipLevels = mipLevels;
        srvDesc.Texture3D.MostDetailedMip = mip;
        break;
    default:
        throw std::exception("Invalid resource dimension.");
    }

    return srvDesc;
}

D3D12_UNORDERED_ACCESS_VIEW_DESC GetUAVDesc(const D3D12_RESOURCE_DESC& resDesc, u32 mipSlice, u32 arraySlice = 0, u32 planeSlice = 0)
{
    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = resDesc.Format;

    switch (resDesc.Dimension)
    {
    case D3D12_RESOURCE_DIMENSION_TEXTURE1D:
        if (resDesc.DepthOrArraySize > 1)
        {
            uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE1DARRAY;
            uavDesc.Texture1DArray.ArraySize = resDesc.DepthOrArraySize - arraySlice;
            uavDesc.Texture1DArray.FirstArraySlice = arraySlice;
            uavDesc.Texture1DArray.MipSlice = mipSlice;
        }
        else
        {
            uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE1D;
            uavDesc.Texture1D.MipSlice = mipSlice;
        }
        break;
    case D3D12_RESOURCE_DIMENSION_TEXTURE2D:
        if (resDesc.DepthOrArraySize > 1)
        {
            uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
            uavDesc.Texture2DArray.ArraySize = resDesc.DepthOrArraySize - arraySlice;
            uavDesc.Texture2DArray.FirstArraySlice = arraySlice;
            uavDesc.Texture2DArray.PlaneSlice = planeSlice;
            uavDesc.Texture2DArray.MipSlice = mipSlice;
        }
        else
        {
            uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            uavDesc.Texture2D.PlaneSlice = planeSlice;
            uavDesc.Texture2D.MipSlice = mipSlice;
        }
        break;
    case D3D12_RESOURCE_DIMENSION_TEXTURE3D:
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
        uavDesc.Texture3D.WSize = resDesc.DepthOrArraySize - arraySlice;
        uavDesc.Texture3D.FirstWSlice = arraySlice;
        uavDesc.Texture3D.MipSlice = mipSlice;
        break;
    default:
        throw std::exception("Invalid resource dimension.");
    }

    return uavDesc;
}

Texture::Texture(RenderDevice& device, const std::wstring& name)
	: Super(device, name, eResourceType::Texture)
{
}

Texture::Texture(RenderDevice& device, const std::wstring& name, CreationInfo&& info)
    : Super(device, name, std::move(info), eResourceType::Texture)
{
    m_Width  = static_cast<u32>(m_ResourceDesc.Width);
    m_Height = static_cast<u32>(m_ResourceDesc.Height);
    m_Format = m_ResourceDesc.Format;

    CreateViews();
}

Texture::~Texture()
{
    Reset();
}

void Texture::Reset()
{
    Super::Release();

    if (m_RenderTargetView.IsValid())
        m_RenderTargetView.Free();

    if (m_DepthStencilView.IsValid())
        m_DepthStencilView.Free();

    if (m_ShaderResourceView.IsValid())
        m_ShaderResourceView.Free();

    if (m_UnorderedAccessView.IsValid())
        m_UnorderedAccessView.Free();
}

void Texture::SetD3D12Resource(ID3D12Resource* d3d12Resource, D3D12_RESOURCE_STATES states)
{
    Super::SetD3D12Resource(d3d12Resource, states);

    m_Width = static_cast<u32>(m_ResourceDesc.Width);
    m_Height = static_cast<u32>(m_ResourceDesc.Height);
    m_Format = m_ResourceDesc.Format;

    CreateViews();
}

void Texture::Resize(u32 width, u32 height, u32 depthOrArraySize)
{
    if (IsValid())
    {
        if (m_ResourceDesc.Width == width && m_ResourceDesc.Height == height)
            return;

        CD3DX12_RESOURCE_DESC desc(m_ResourceDesc);
        desc.Width = std::max(width, 1u);
        desc.Height = std::max(height, 1u);
        desc.DepthOrArraySize = static_cast<u16>(depthOrArraySize);
        desc.MipLevels = desc.SampleDesc.Count > 1 ? 1 : m_ResourceDesc.MipLevels;
        
        Reset();

        ID3D12Resource* d3d12Resource = 
            m_RenderDevice.CreateRHIResource(desc, m_CurrentState.GetSubresourceState(), CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), m_pClearValue);

        SetD3D12Resource(d3d12Resource, m_CurrentState.GetSubresourceState());
    }
}

DXGI_FORMAT Texture::GetFormat(bool bSRV) const
{
    return IsTypelessFormat(m_Format) ? ConvertToViewFormat(m_Format, bSRV) : m_Format;
}

void Texture::CreateShaderResourceView(const D3D12_SHADER_RESOURCE_VIEW_DESC& desc)
{
    if (m_d3d12Resource)
    {
        auto  d3d12Device = m_RenderDevice.GetD3D12Device();
        auto& rm = m_RenderDevice.GetResourceManager();

        m_ShaderResourceView = rm.AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        d3d12Device->CreateShaderResourceView(m_d3d12Resource, &desc, m_ShaderResourceView.GetCPUHandle());
    }
}

void Texture::CreateViews()
{
    if (m_d3d12Resource)
    {
        auto  d3d12Device = m_RenderDevice.GetD3D12Device();
        auto& rm = m_RenderDevice.GetResourceManager();

        CD3DX12_RESOURCE_DESC desc(m_d3d12Resource->GetDesc());

        // Create RTV
        if ((desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) != 0 && IsRTVSupported())
        {
            m_RenderTargetView = rm.AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
            d3d12Device->CreateRenderTargetView(m_d3d12Resource, nullptr, m_RenderTargetView.GetCPUHandle());
        }
        // Create DSV
        if ((desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL) != 0)
        {
            if (IsDSVSupported())
            {
                m_DepthStencilView = rm.AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
                d3d12Device->CreateDepthStencilView(m_d3d12Resource, nullptr, m_DepthStencilView.GetCPUHandle());
            }
            else if (IsTypelessFormat(m_Format))
            {
                auto dsvDesc   = GetDSVDesc(desc, 0);
                dsvDesc.Format = ConvertToViewFormat(m_Format, false);

                m_DepthStencilView = rm.AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
                d3d12Device->CreateDepthStencilView(m_d3d12Resource, &dsvDesc, m_DepthStencilView.GetCPUHandle());
            }
        }
        // Create SRV
        if ((desc.Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE) == 0)
        {
            if (IsSRVSupported())
            {
                m_ShaderResourceView = rm.AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                d3d12Device->CreateShaderResourceView(m_d3d12Resource, nullptr, m_ShaderResourceView.GetCPUHandle());
            }
            else if (IsTypelessFormat(m_Format))
            {
                auto srvDesc   = GetSRVDesc(desc, 0);
                srvDesc.Format = ConvertToViewFormat(m_Format, true);

                m_ShaderResourceView = rm.AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                d3d12Device->CreateShaderResourceView(m_d3d12Resource, &srvDesc, m_ShaderResourceView.GetCPUHandle());
            }
        }
        // Create UAV for each mip (only supported for 1D and 2D textures).
        if ((desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) != 0 && IsUAVSupported() && desc.DepthOrArraySize == 1)
        {
            m_UnorderedAccessView =
                rm.AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, desc.MipLevels);
            for (int i = 0; i < desc.MipLevels; ++i)
            {
                auto uavDesc = GetUAVDesc(desc, i);
                d3d12Device->CreateUnorderedAccessView(m_d3d12Resource, nullptr, &uavDesc, m_UnorderedAccessView.GetCPUHandle(i));
            }
        }
    }
}

Arc< Texture > Texture::Create(RenderDevice& device, const std::wstring& name, CreationInfo&& desc)
{
    return MakeArc< Texture >(device, name, std::move(desc));
}

Arc<Texture> Texture::CreateEmpty(RenderDevice& device, const std::wstring& name)
{
    return MakeArc< Texture >(device, name);
}

}