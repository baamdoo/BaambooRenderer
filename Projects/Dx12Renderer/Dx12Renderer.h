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

class Renderer : public render::Renderer
{
public:
	explicit Renderer(baamboo::Window* pWindow, ImGuiContext* pImGuiContext);
	virtual ~Renderer() override;

	virtual void NewFrame() override;

	virtual Arc< render::CommandContext > BeginFrame() override;
	virtual void EndFrame(Arc< render::CommandContext >&& pContext, Arc< render::Texture > pScene, bool bDrawUI) override;

	virtual void WaitIdle() override;
	virtual void Resize(i32 width, i32 height) override;

	virtual render::RenderDevice* GetDevice() override { return m_pRenderDevice; }
	virtual eRendererAPI GetAPIType() const override { return eRendererAPI::D3D12; }

private:
	class Dx12RenderDevice* m_pRenderDevice = nullptr;
	class Dx12SwapChain*    m_pSwapChain    = nullptr;

	u64 m_FrameFenceValue[NUM_FRAMES_IN_FLIGHT] = {};

	Box< class ImGuiModule > m_pImGuiModule;
};

} // namespace dx12