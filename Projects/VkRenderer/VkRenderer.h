#pragma once
#include "BaambooCore/BackendAPI.h"
#include "BaambooCore/ResourceHandle.h"

struct ImGuiContext;

namespace baamboo
{
	class Window;
}

namespace vk
{

class CommandBuffer;
class RenderModule;

class Renderer : public RendererAPI
{
public:
	explicit Renderer(baamboo::Window* pWindow, ImGuiContext* pImGuiContext);
	virtual ~Renderer() override;

	virtual void Render(SceneRenderView&& renderView) override;
	virtual void NewFrame() override;

	virtual void OnWindowResized(i32 width, i32 height) override;

	virtual void SetRendererType(eRendererType type) override;

private:
	void CreateDefaultResources();

	CommandBuffer& BeginFrame();
	void EndFrame(CommandBuffer& cmdBuffer);

private:
	class RenderContext* m_pRenderContext = nullptr;
	class SwapChain*     m_pSwapChain = nullptr;

	std::vector< RenderModule* > m_pRenderModules;

	eRendererType m_Type = eRendererType::Deferred;
};

} // namespace vk