#pragma once

namespace vk
{

class VulkanTexture;

class VulkanRenderTarget : public render::RenderTarget
{
public:
    VulkanRenderTarget(VkRenderDevice& rd, const std::string& name);
    ~VulkanRenderTarget();

    VulkanRenderTarget& AttachTexture(render::eAttachmentPoint attachmentPoint, Arc< render::Texture > tex);
    VulkanRenderTarget& SetLoadAttachment(render::eAttachmentPoint attachmentPoint);
    virtual void Build() override;

    virtual void Resize(u32 width, u32 height, u32 depth) override;
    virtual void Reset() override;
    virtual void InvalidateImageLayout() override;
    
    [[nodiscard]]
    const VkRenderPassBeginInfo& GetBeginInfo() const { return m_BeginInfo; }
    [[nodiscard]]
    VkViewport GetViewport(float2 scale = { 1.0f, 1.0f }, float2 bias = { 0.0f, 0.0f }, f32 minDepth = 0.0f, f32 maxDepth = 1.0f) const;
    [[nodiscard]]
    VkRect2D GetScissorRect() const;

    inline VkRenderPass vkRenderPass() const { return m_vkRenderPass; }
    inline VkFramebuffer vkFramebuffer() const { return m_vkFramebuffer; }

    inline u32 GetNumColors() const { return m_NumColors; }

private:
    [[nodiscard]]
    bool IsDepthOnly() const;

private:
    VkRenderDevice& m_RenderDevice;

    VkRenderPass          m_vkRenderPass = VK_NULL_HANDLE;
    VkFramebuffer         m_vkFramebuffer = VK_NULL_HANDLE;
    VkRenderPassBeginInfo m_BeginInfo = {};

    std::vector< VkClearValue >         m_ClearValues;
    VkAttachmentDescription             m_AttachmentDesc;

    u32 m_NumColors = 0;
    u32 m_bLoadAttachmentBits = 0;
};

} // namespace vk