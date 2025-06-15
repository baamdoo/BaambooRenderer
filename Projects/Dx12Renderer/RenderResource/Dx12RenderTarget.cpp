#include "RendererPch.h"
#include "Dx12RenderTarget.h"
#include "Dx12Texture.h"
#include "RenderDevice/Dx12CommandContext.h"

namespace dx12
{

RenderTarget::RenderTarget()
	: m_pAttachments(eAttachmentPoint::NumAttachmentPoints)
{
}

RenderTarget::~RenderTarget() = default;

RenderTarget& RenderTarget::AttachTexture(eAttachmentPoint attachmentPoint, Arc< Texture > pTexture)
{
    BB_ASSERT(attachmentPoint < eAttachmentPoint::NumAttachmentPoints, "Invalid attachment point!");
    m_pAttachments[attachmentPoint] = pTexture;

    return *this;
}

void RenderTarget::ClearTexture(CommandContext& context, eAttachmentPoint attachmentPoint)
{
    for (u32 i = 0; i < eAttachmentPoint::DepthStencil; ++i)
    {
        if (m_pAttachments[i] && (attachmentPoint == eAttachmentPoint::All || eAttachmentPoint(i) == attachmentPoint))
        {
            context.ClearTexture(m_pAttachments[i]);
        }
    }

    if (m_pAttachments[eAttachmentPoint::DepthStencil] && (attachmentPoint == eAttachmentPoint::All || attachmentPoint == eAttachmentPoint::DepthStencil))
    {
        const auto& desc = m_pAttachments[eAttachmentPoint::DepthStencil]->Desc();

        const D3D12_CLEAR_FLAGS flags = desc.Format == DXGI_FORMAT_D32_FLOAT ? D3D12_CLEAR_FLAG_DEPTH : D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL;
        context.ClearDepthStencilTexture(m_pAttachments[eAttachmentPoint::DepthStencil], flags);
    }
}

void RenderTarget::Resize(u32 width, u32 height)
{
    for (auto pTexture : m_pAttachments)
    {
        if (pTexture)
            pTexture->Resize(width, height);
    }
}

void RenderTarget::Reset()
{
    m_pAttachments = std::vector< Arc< Texture > >(eAttachmentPoint::NumAttachmentPoints);
}

Arc< Texture > RenderTarget::Attachment(eAttachmentPoint attachmentPoint) const
{
    return m_pAttachments[attachmentPoint];
}

const std::vector< Arc< Texture > >& RenderTarget::GetAttachments() const
{
    return m_pAttachments;
}

D3D12_VIEWPORT RenderTarget::GetViewport(float2 scale, float2 bias, float minDepth, float maxDepth) const
{
    u32 width  = m_pAttachments[0]->GetWidth();
    u32 height = m_pAttachments[0]->GetHeight();

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
    u32 width  = m_pAttachments[0]->GetWidth();
    u32 height = m_pAttachments[0]->GetHeight();

    D3D12_RECT rect = {};
    rect.left = 0;
    rect.top = 0;
    rect.right = width;
    rect.bottom = height;

    return rect;
}

}