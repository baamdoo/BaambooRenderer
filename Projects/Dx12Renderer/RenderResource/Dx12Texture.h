#pragma once
#include "Dx12Resource.h"

namespace dx12
{

class Texture : public Resource
{
using Super = Resource;

public:
    using CreationInfo = ResourceCreationInfo;

    virtual void Release() override;
    virtual void SetD3D12Resource(ID3D12Resource* d3d12Resource, D3D12_RESOURCE_STATES states = D3D12_RESOURCE_STATE_COMMON) override;
    void Resize(u32 width, u32 height, u32 depthOrArraySize = 1);

    bool IsSRVSupported() const { return IsFormatSupported(D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE); }
    bool IsRTVSupported() const { return IsFormatSupported(D3D12_FORMAT_SUPPORT1_RENDER_TARGET); }
    bool IsDSVSupported() const { return IsFormatSupported(D3D12_FORMAT_SUPPORT1_DEPTH_STENCIL); }
    bool IsUAVSupported() const
    {
        return IsFormatSupported(D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW)
            && IsFormatSupported(D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD)
            && IsFormatSupported(D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE);
    }

    inline u32 GetWidth() const { return m_Width; }
    inline u32 GetHeight() const { return m_Height; }
    inline DXGI_FORMAT GetFormat() const { return m_Format; }

    const D3D12_CLEAR_VALUE* GetClearValue() const { return m_pClearValue; }

    D3D12_CPU_DESCRIPTOR_HANDLE GetRenderTargetView() const { return m_RenderTargetView.GetCPUHandle(); }
    D3D12_CPU_DESCRIPTOR_HANDLE GetDepthStencilView() const { return m_DepthStencilView.GetCPUHandle(); }
    D3D12_CPU_DESCRIPTOR_HANDLE GetShaderResourceView() const { return m_ShaderResourceView.GetCPUHandle(); }
    D3D12_CPU_DESCRIPTOR_HANDLE GetUnorderedAccessView(u32 mip) const { return m_UnorderedAccessView.GetCPUHandle(mip); }

protected:
    template< typename T >
    friend class ResourcePool;
    friend class ResourceManager;

    // Resource can only be created by ResourceManger;
    Texture(RenderContext& context, std::wstring_view name);
    Texture(RenderContext& context, std::wstring_view name, CreationInfo&& info);
    virtual ~Texture();

    void CreateViews();

private:
    u32 m_Width = 0;
    u32 m_Height = 0;
    DXGI_FORMAT m_Format = DXGI_FORMAT::DXGI_FORMAT_UNKNOWN;

    DescriptorAllocation m_RenderTargetView = {};
    DescriptorAllocation m_DepthStencilView = {};
    DescriptorAllocation m_ShaderResourceView = {};
    DescriptorAllocation m_UnorderedAccessView = {};
};

}