#pragma once
#include "RenderCommon/RendererAPI.h"

namespace vk
{

class VkRenderer : public render::Renderer
{
public:
	VkRenderer(baamboo::Window* pWindow, ImGuiContext* pImGuiContext);
	~VkRenderer();

	virtual Arc< render::CommandContext > BeginFrame() override;
	virtual void EndFrame(Arc< render::CommandContext >&& context, Arc< render::Texture > scene, bool bDrawUI) override;

	virtual void NewFrame() override;

	virtual void WaitIdle() override;
	virtual void Resize(i32 width, i32 height) override;

	virtual render::RenderDevice* GetDevice() override { return m_pRenderDevice; }

	eRendererAPI GetAPIType() const override { return eRendererAPI::Vulkan; }

private:
	class VkRenderDevice* m_pRenderDevice = nullptr;
	class SwapChain*      m_pSwapChain    = nullptr;
	class FrameManager*   m_pFrameManager = nullptr;

	Box< class ImGuiModule > m_ImGuiModule;
};

} // namespace vk