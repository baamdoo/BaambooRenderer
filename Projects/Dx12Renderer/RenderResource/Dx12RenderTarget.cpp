#include "RendererPch.h"
#include "Dx12RenderTarget.h"
#include "Dx12Texture.h"
#include "RenderDevice/Dx12CommandList.h"

namespace dx12
{

RenderTarget::RenderTarget()
	: m_pTextures(eAttachmentPoint::NumAttachmentPoints)
{
}

RenderTarget& RenderTarget::AttachTexture(eAttachmentPoint attachmentPoint, Texture* texture)
{
    BB_ASSERT(attachmentPoint < eAttachmentPoint::NumAttachmentPoints, "Invalid attachment point!");
    m_pTextures[attachmentPoint] = texture;

    return *this;
}

void RenderTarget::ClearTexture(CommandList& commandList, eAttachmentPoint attachmentPoint)
{
    for (u32 i = 0; i < eAttachmentPoint::DepthStencil; ++i)
    {
        if (m_pTextures[i] && (attachmentPoint == eAttachmentPoint::All || eAttachmentPoint(i) == attachmentPoint))
        {
            commandList.ClearTexture(m_pTextures[i]);
        }
    }

    if (m_pTextures[eAttachmentPoint::DepthStencil] && (attachmentPoint == eAttachmentPoint::All || attachmentPoint == eAttachmentPoint::DepthStencil))
    {
        const auto& desc = m_pTextures[eAttachmentPoint::DepthStencil]->GetResourceDesc();

        const D3D12_CLEAR_FLAGS flags = desc.Format == DXGI_FORMAT_D32_FLOAT ? D3D12_CLEAR_FLAG_DEPTH : D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL;
        commandList.ClearDepthStencilTexture(m_pTextures[eAttachmentPoint::DepthStencil], flags);
    }
}

void RenderTarget::Resize(u32 width, u32 height)
{
    for (auto pTexture : m_pTextures)
    {
        if (pTexture)
            pTexture->Resize(width, height);
    }
}

void RenderTarget::Reset()
{
    m_pTextures = std::vector< Texture* >(eAttachmentPoint::NumAttachmentPoints);
}

D3D12_VIEWPORT RenderTarget::GetViewport(float2 scale, float2 bias, float minDepth, float maxDepth) const
{
    u32 width = m_pTextures[0]->GetWidth();
    u32 height = m_pTextures[0]->GetHeight();

    D3D12_VIEWPORT viewport = {};
    viewport.TopLeftX = (width * bias.x);
    viewport.TopLeftY = (height * bias.y);
    viewport.Width = (width * scale.x);
    viewport.Height = (height * scale.y);
    viewport.MinDepth = minDepth;
    viewport.MaxDepth = maxDepth;

    return viewport;
}

D3D12_RECT RenderTarget::GetScissorRect() const
{
    u32 width = m_pTextures[0]->GetWidth();
    u32 height = m_pTextures[0]->GetHeight();

    D3D12_RECT rect = {};
    rect.left = 0;
    rect.top = 0;
    rect.right = width;
    rect.bottom = height;

    return rect;
}

}