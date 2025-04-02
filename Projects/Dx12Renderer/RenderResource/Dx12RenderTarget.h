#pragma once
#include "BaambooCore/ResourceHandle.h"

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

namespace dx12
{

class Texture;

class RenderTarget
{
public:
    RenderTarget();

    RenderTarget(const RenderTarget& copy) = default;
    RenderTarget(RenderTarget&& copy) = default;

    RenderTarget& operator=(const RenderTarget& other) = default;
    RenderTarget& operator=(RenderTarget&& other) = default;

    void AttachTexture(eAttachmentPoint attachmentPoint, Texture* texture);
    void ClearTexture(CommandList& commandList, eAttachmentPoint attachmentPoint);

    void Resize(u32 width, u32 height);
    void Reset();

public:
    inline Texture* Attachment(eAttachmentPoint attachmentPoint) const { return m_pTextures[(u32)attachmentPoint]; }
    inline const std::vector< Texture* >& GetTextures() const { return m_pTextures; }

    D3D12_VIEWPORT GetViewport(float2 scale = { 1.0f, 1.0f }, float2 bias = { 0.0f, 0.0f }, float minDepth = 0.0f, float maxDepth = 1.0f) const;
    D3D12_RECT GetScissorRect() const;

private:
    std::vector< Texture* > m_pTextures;
};

}