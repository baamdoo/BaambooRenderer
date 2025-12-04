#pragma once

namespace dx12
{

class Dx12RenderTarget : public render::RenderTarget
{
public:
    Dx12RenderTarget(const char* name);
    ~Dx12RenderTarget();

    void ClearTexture(Dx12CommandContext& context, render::eAttachmentPoint attachmentPoint);

    virtual void Build() override {}
    virtual void Resize(u32 width, u32 height, u32 depth) override;
    virtual void Reset() override;

    D3D12_VIEWPORT GetViewport(float2 scale = { 1.0f, 1.0f }, float2 bias = { 0.0f, 0.0f }, float minDepth = 0.0f, float maxDepth = 1.0f) const;
    D3D12_RECT GetScissorRect() const;
};

} // namespace dx12