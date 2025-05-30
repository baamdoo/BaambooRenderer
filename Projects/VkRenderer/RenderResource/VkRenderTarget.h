#pragma once
#include "BaambooCore/ResourceHandle.h"

namespace vk
{

class Texture;

enum eAttachmentPoint : u8
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
    NumColorAttachments = DepthStencil,
    NumAttachmentPoints = All,
};

class RenderTarget
{

public:
    RenderTarget(RenderContext& context);
    ~RenderTarget();

    RenderTarget& AttachTexture(eAttachmentPoint attachmentPoint, baamboo::ResourceHandle< Texture > tex);
    RenderTarget& SetLoadAttachment(eAttachmentPoint attachmentPoint);
    void Build();

    void Resize(u32 width, u32 height, u32 depth);
    void Reset();
    void InvalidateImageLayout();
    
    [[nodiscard]]
    const VkRenderPassBeginInfo& GetBeginInfo() const { return m_BeginInfo; }
    [[nodiscard]]
    VkViewport GetViewport(float2 scale = { 1.0f, 1.0f }, float2 bias = { 0.0f, 0.0f }, f32 minDepth = 0.0f, f32 maxDepth = 1.0f) const;
    [[nodiscard]]
    VkRect2D GetScissorRect() const;

    [[nodiscard]]
    inline VkRenderPass vkRenderPass() const { return m_vkRenderPass; }
    [[nodiscard]]
    inline VkFramebuffer vkFramebuffer() const { return m_vkFramebuffer; }

    [[nodiscard]]
    baamboo::ResourceHandle< Texture > Attachment(eAttachmentPoint attachment) const { return m_Attachments[attachment]; }
    [[nodiscard]]
    inline u32 GetNumColors() const { return m_NumColors; }

private:
    [[nodiscard]]
    bool IsDepthOnly() const;

private:
    RenderContext& m_RenderContext;

    VkRenderPass          m_vkRenderPass = VK_NULL_HANDLE;
    VkFramebuffer         m_vkFramebuffer = VK_NULL_HANDLE;
    VkRenderPassBeginInfo m_BeginInfo = {};

    std::vector< baamboo::ResourceHandle< Texture > > m_Attachments;
    std::vector< VkClearValue >                       m_ClearValues;
    VkAttachmentDescription                           m_AttachmentDesc;

    u32 m_NumColors = 0;
    u32 m_bLoadAttachmentBits = 0;
};

} // namespace vk