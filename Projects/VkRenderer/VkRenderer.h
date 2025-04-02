#pragma once
#include "BaambooCore/RendererAPI.h"

struct ImGuiContext;

namespace baamboo
{
	class Window;
}

namespace vk
{

class Renderer : public RendererAPI
{
public:
	explicit Renderer(baamboo::Window* pWindow, ImGuiContext* pImGuiContext);
	virtual ~Renderer() override;

	virtual void Render() override;
	virtual void NewFrame() override;

	virtual void SetRendererType(eRendererType type) override;

	virtual void OnWindowResized(i32 width, i32 height) override;

private:
	class CommandBuffer& BeginFrame();
	void RenderFrame(CommandBuffer& cmdBuffer);
	void EndFrame(CommandBuffer& cmdBuffer);

private:
	class RenderContext* m_pRenderContext = nullptr;
	class SwapChain*     m_pSwapChain = nullptr;

	eRendererType m_type = eRendererType::Deferred;
};

} // namespace vk