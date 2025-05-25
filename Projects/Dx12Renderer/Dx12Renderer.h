#pragma once
#include "BaambooCore/BackendAPI.h"

struct ImGuiContext;

namespace baamboo
{
	class Window;
}

namespace dx12
{

class RenderModule;

class Renderer : public RendererAPI
{
public:
	explicit Renderer(baamboo::Window* pWindow, ImGuiContext* pImGuiContext);
	virtual ~Renderer() override;

	virtual void NewFrame() override;
	virtual void Render(SceneRenderView&& renderView) override;

	virtual void OnWindowResized(i32 width, i32 height) override;

	virtual void SetRendererType(eRendererType type) override;

private:
	class CommandList& BeginFrame();
	void EndFrame(CommandList& cmdList);

private:
	class RenderContext* m_pRenderContext = nullptr;
	class SwapChain*     m_pSwapChain = nullptr;

	u64 m_ContextFenceValue[NUM_FRAMES_IN_FLIGHT] = {};

	std::vector< RenderModule* > m_pRenderModules;

	eRendererType m_type = eRendererType::Deferred;
};

} // namespace dx12