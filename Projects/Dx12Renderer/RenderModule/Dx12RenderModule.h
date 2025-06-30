#pragma once

namespace dx12
{

class CommandContext;
class RootSignature;
class GraphicsPipeline;
class ComputePipeline;
class Texture;
class Sampler;

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

} // namespace dx12