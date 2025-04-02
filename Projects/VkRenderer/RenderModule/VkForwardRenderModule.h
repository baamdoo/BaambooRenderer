#pragma once
#include "RenderResource/VkRenderTarget.h"

namespace vk
{

class CommandBuffer;

namespace ForwardPass
{

void Initialize(RenderContext& renderContext);
void Apply(CommandBuffer& cmdBuffer);
void Destroy();

void Resize(u32 width, u32 height, u32 depth = 1);

[[nodiscard]]
baamboo::ResourceHandle< Texture > GetRenderedTexture(eAttachmentPoint attachment);

}

} // namespace vk