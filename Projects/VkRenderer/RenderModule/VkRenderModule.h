#pragma once

namespace vk
{

class CommandBuffer;

class RenderModule
{
public:
	RenderModule(RenderContext& context) : m_RenderContext(context) {}
	virtual ~RenderModule() = default;

	virtual void Apply(CommandBuffer& cmdBuffer) { UNUSED(cmdBuffer); }
	virtual void Resize(u32 width, u32 height, u32 depth = 1) { UNUSED(width); UNUSED(height); UNUSED(depth); }

protected:
	RenderContext& m_RenderContext;
};

} // namespace vk