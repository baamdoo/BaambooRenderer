#pragma once

namespace vk
{

class CommandContext;
class Texture;
class Sampler;
class RenderTarget;
class GraphicsPipeline;
class ComputePipeline;

enum eAttachmentPoint : u8;

class RenderModule
{
public:
	RenderModule(RenderDevice& device) : m_RenderDevice(device) {}
	virtual ~RenderModule() = default;

	virtual void Apply(CommandContext& context) { UNUSED(context); }
	virtual void Resize(u32 width, u32 height, u32 depth = 1) { UNUSED(width); UNUSED(height); UNUSED(depth); }

protected:
	RenderDevice& m_RenderDevice;
};

} // namespace vk