#pragma once
#include "RenderCommon/RendererAPI.h"
#include "SpikeCapture.h"

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
	explicit Renderer(baamboo::Window* pWindow, const render::DeviceSettings& ds, ImGuiContext* pImGuiContext);
	virtual ~Renderer() override;

	virtual void NewFrame() override;

	virtual Arc< render::CommandContext > BeginFrame() override;
	virtual void EndFrame(Arc< render::CommandContext >&& pContext, Arc< render::Texture > pScene, bool bDrawUI) override;

	virtual void WaitIdle() override;
	virtual void Resize(i32 width, i32 height) override;

	virtual render::RenderDevice* GetDevice() override { return m_pRenderDevice; }
	virtual eRendererAPI GetAPIType() const override { return eRendererAPI::D3D12; }

	// PIX programmatic GPU capture, driven by GPU frame-time spikes.
	virtual void RecordFrameTime(double gpuFrameMs) override { m_SpikeCapture.Update(gpuFrameMs); }
	virtual void ForceCaptureNextFrame() override { m_SpikeCapture.ForceCaptureNextFrame(); }

private:
	class Dx12RenderDevice* m_pRenderDevice = nullptr;
	class Dx12SwapChain*    m_pSwapChain    = nullptr;

	u64 m_FrameFenceValue[kMaxFramesInFlight] = {};

	Box< class ImGuiModule > m_pImGuiModule;

	SpikeCapture m_SpikeCapture;
};

} // namespace dx12