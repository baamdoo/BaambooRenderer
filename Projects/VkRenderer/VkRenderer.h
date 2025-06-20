#pragma once
#include "BaambooCore/BackendAPI.h"

struct ImGuiContext;

namespace baamboo
{
	class Window;
}

namespace vk
{

class CommandContext;
class RenderModule;

class Renderer : public RendererAPI
{
public:
	explicit Renderer(baamboo::Window* pWindow, ImGuiContext* pImGuiContext);
	virtual ~Renderer() override;

	virtual void Render(const SceneRenderView& renderView) override;
	virtual void NewFrame() override;

	virtual void OnWindowResized(i32 width, i32 height) override;

	virtual void SetRendererType(eRendererType type) override;

private:
	class RenderDevice* m_pRenderDevice = nullptr;
	class SwapChain*    m_pSwapChain = nullptr;
	class FrameManager* m_pFrameManager = nullptr;

	std::vector< RenderModule* > m_pRenderModules;

	eRendererType m_Type = eRendererType::Deferred;
};

} // namespace vk