#pragma once
#include "BaambooCore/BackendAPI.h"

struct ImGuiContext;

namespace baamboo
{
	class Window;
}

namespace dx12
{

class Renderer : public RendererAPI
{
public:
	explicit Renderer(baamboo::Window* pWindow, ImGuiContext* pImGuiContext);
	virtual ~Renderer() override;

	virtual void NewFrame() override;
	virtual void Render(const baamboo::SceneRenderView& renderView) override;

	virtual void OnWindowResized(i32 width, i32 height) override;
	virtual void SetRendererType(eRendererType type) override;

	[[nodiscard]]
	virtual ResourceManagerAPI& GetResourceManager() override;

private:
	class CommandList& BeginFrame();
	void RenderFrame(CommandList& cmdList);
	void EndFrame(CommandList& cmdList);

private:
	class RenderContext* m_pRenderContext = nullptr;
	class SwapChain*     m_pSwapChain = nullptr;

	u64 m_ContextFenceValue[NUM_FRAMES] = {};

	eRendererType m_type = eRendererType::Deferred;
};

} // namespace dx12