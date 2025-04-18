#pragma once
#include "RenderResource/Dx12RenderTarget.h"

namespace dx12
{

class CommandList;

namespace ForwardPass
{

void Initialize(RenderContext& renderContext);
void Apply(CommandList& cmdList, float4 testColor);
void Destroy();

void Resize(u32 width, u32 height, u32 depth = 1);

[[nodiscard]]
Texture* GetRenderedTexture(eAttachmentPoint attachment);

};

} // namespace dx12