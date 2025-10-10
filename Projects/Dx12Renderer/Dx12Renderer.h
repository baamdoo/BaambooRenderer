#pragma once
#include "RenderCommon/RendererAPI.h"

struct ImGuiContext;

namespace baamboo
{
	class Window;
}

namespace dx12
{

class RenderModule;

class Renderer : public render::RendererAPI
{
public:
	explicit Renderer(baamboo::Window* pWindow, ImGuiContext* pImGuiContext);
	virtual ~Renderer() override;

	virtual void NewFrame() override;
	virtual void Render(SceneRenderView&& renderView) override;

	virtual void OnWindowResized(i32 width, i32 height) override;

	virtual void SetRendererType(eRendererType type) override;

private:
	class CommandContext& BeginFrame();
	void EndFrame(CommandContext& context);

private:
	class RenderDevice* m_pRenderDevice = nullptr;
	class SwapChain*     m_pSwapChain = nullptr;

	u64 m_FrameFenceValue[NUM_FRAMES_IN_FLIGHT] = {};

	std::vector< RenderModule* > m_pRenderModules;

	eRendererType m_type = eRendererType::Deferred;
};

} // namespace dx12