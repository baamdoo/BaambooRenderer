#include "RendererPch.h"
#include "Dx12Texture.h"
#include "RenderDevice/Dx12RenderDevice.h"
#include "RenderDevice/Dx12ResourceManager.h"

namespace dx12
{

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

Texture::Texture(RenderDevice& device, std::wstring_view name)
	: Super(device, name, eResourceType::Texture)
{
}

Texture::Texture(RenderDevice& device, std::wstring_view name, CreationInfo&& info)
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
        if ((desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL) != 0 && IsDSVSupported())
        {
            m_DepthStencilView = rm.AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
            d3d12Device->CreateDepthStencilView(m_d3d12Resource, nullptr, m_DepthStencilView.GetCPUHandle());
        }
        // Create SRV
        if ((desc.Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE) == 0 && IsSRVSupported())
        {
            m_ShaderResourceView = rm.AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            d3d12Device->CreateShaderResourceView(m_d3d12Resource, nullptr, m_ShaderResourceView.GetCPUHandle());
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

Arc< Texture > Texture::Create(RenderDevice& device, std::wstring_view name, CreationInfo&& desc)
{
    return MakeArc< Texture >(device, name, std::move(desc));
}

Arc<Texture> Texture::CreateEmpty(RenderDevice& device, std::wstring_view name)
{
    return MakeArc< Texture >(device, name);
}

}