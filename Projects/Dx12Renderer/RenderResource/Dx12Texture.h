#pragma once
#include "Dx12Resource.h"

namespace dx12
{

class Dx12Texture : public render::Texture, public Dx12Resource
{
public:
    static Arc< Dx12Texture > Create(Dx12RenderDevice& rd, const std::string& name, CreationInfo&& info);
    static Arc< Dx12Texture > CreateEmpty(Dx12RenderDevice& rd, const std::string& name);

    Dx12Texture(Dx12RenderDevice& rd, const std::string& name);
    Dx12Texture(Dx12RenderDevice& rd, const std::string& name, CreationInfo&& info);
    virtual ~Dx12Texture();

    virtual void Reset() override;
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
    DXGI_FORMAT GetFormat(bool bSRV = false) const;

    const D3D12_CLEAR_VALUE* GetClearValue() const { return m_pClearValue; }

    D3D12_CPU_DESCRIPTOR_HANDLE GetRenderTargetView() const { return m_RenderTargetView.GetCPUHandle(); }
    D3D12_CPU_DESCRIPTOR_HANDLE GetDepthStencilView() const { return m_DepthStencilView.GetCPUHandle(); }
    D3D12_CPU_DESCRIPTOR_HANDLE GetShaderResourceView() const { return m_ShaderResourceView.GetCPUHandle(); }
    D3D12_CPU_DESCRIPTOR_HANDLE GetUnorderedAccessView(u32 mip) const { return m_UnorderedAccessView.GetCPUHandle(mip); }

    void CreateShaderResourceView(const D3D12_SHADER_RESOURCE_VIEW_DESC& desc);

protected:
    void CreateViews();

private:
    u32 m_Width  = 0;
    u32 m_Height = 0;
    DXGI_FORMAT m_Format = DXGI_FORMAT::DXGI_FORMAT_UNKNOWN;

    DescriptorAllocation m_RenderTargetView    = {};
    DescriptorAllocation m_DepthStencilView    = {};
    DescriptorAllocation m_ShaderResourceView  = {};
    DescriptorAllocation m_UnorderedAccessView = {};
};

}