#include "RendererPch.h"
#include "Dx12RenderTarget.h"
#include "Dx12Texture.h"
#include "RenderDevice/Dx12CommandContext.h"

namespace dx12
{

Dx12RenderTarget::Dx12RenderTarget(const std::string& name)
	: render::RenderTarget(name)
{
}

Dx12RenderTarget::~Dx12RenderTarget() = default;

void Dx12RenderTarget::ClearTexture(Dx12CommandContext& context, render::eAttachmentPoint attachmentPoint)
{
    using namespace render;

    for (u32 i = 0; i < eAttachmentPoint::DepthStencil; ++i)
    {
        if (m_pAttachments[i] && (attachmentPoint == eAttachmentPoint::All || eAttachmentPoint(i) == attachmentPoint))
        {
            context.ClearTexture(StaticCast<Dx12Texture>(m_pAttachments[i]));
        }
    }

    if ((attachmentPoint == eAttachmentPoint::All || attachmentPoint == eAttachmentPoint::DepthStencil))
    {
        if (auto pDepthTex = StaticCast<Dx12Texture>(m_pAttachments[eAttachmentPoint::DepthStencil]))
        {
            const auto& desc = pDepthTex->Desc();

            const D3D12_CLEAR_FLAGS flags = (desc.Format == DXGI_FORMAT_D32_FLOAT || desc.Format == DXGI_FORMAT_D16_UNORM) ? 
                D3D12_CLEAR_FLAG_DEPTH : D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL;
            context.ClearDepthStencilTexture(pDepthTex, flags);
        }
    }
}

void Dx12RenderTarget::Resize(u32 width, u32 height, u32 depth)
{
    for (auto pTexture : m_pAttachments)
    {
        if (pTexture)
            pTexture->Resize(width, height, depth);
    }
}

void Dx12RenderTarget::Reset()
{
    using namespace render;

    m_pAttachments.clear();
    m_pAttachments.reserve(eAttachmentPoint::NumAttachmentPoints);
}

D3D12_VIEWPORT Dx12RenderTarget::GetViewport(float2 scale, float2 bias, float minDepth, float maxDepth) const
{
    u32 width  = m_pAttachments[0]->Width();
    u32 height = m_pAttachments[0]->Height();

    D3D12_VIEWPORT viewport = {};
    viewport.TopLeftX = (width * bias.x);
    viewport.TopLeftY = (height * bias.y);
    viewport.Width    = (width * scale.x);
    viewport.Height   = (height * scale.y);
    viewport.MinDepth = minDepth;
    viewport.MaxDepth = maxDepth;

    return viewport;
}

D3D12_RECT Dx12RenderTarget::GetScissorRect() const
{
    u32 width  = m_pAttachments[0]->Width();
    u32 height = m_pAttachments[0]->Height();

    D3D12_RECT rect = {};
    rect.left   = 0;
    rect.top    = 0;
    rect.right  = width;
    rect.bottom = height;

    return rect;
}

}