#pragma once

namespace dx12
{

class Texture;

enum eAttachmentPoint
{
    Color0,
    Color1,
    Color2,
    Color3,
    Color4,
    Color5,
    Color6,
    Color7,
    DepthStencil,

    All,
    NumAttachmentPoints = All,
};

class RenderTarget
{
public:
    RenderTarget();
    ~RenderTarget();

    RenderTarget& AttachTexture(eAttachmentPoint attachmentPoint, Arc< Texture > pTexture);
    void ClearTexture(CommandContext& context, eAttachmentPoint attachmentPoint);

    void Resize(u32 width, u32 height);
    void Reset();

    Arc< Texture > Attachment(eAttachmentPoint attachmentPoint) const;
    const std::vector< Arc< Texture > >& GetAttachments() const;

    D3D12_VIEWPORT GetViewport(float2 scale = { 1.0f, 1.0f }, float2 bias = { 0.0f, 0.0f }, float minDepth = 0.0f, float maxDepth = 1.0f) const;
    D3D12_RECT GetScissorRect() const;

private:
    std::vector< Arc< Texture > > m_pAttachments;
};

} // namespace dx12