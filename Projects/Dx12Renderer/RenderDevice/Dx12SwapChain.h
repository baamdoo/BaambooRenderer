#pragma once

namespace baamboo
{
	class Window;
}

namespace dx12
{

class Texture;

class SwapChain
{
public:
	SwapChain(RenderDevice& device, baamboo::Window& window);
	~SwapChain();

	HRESULT Present();

	void ResizeViewport(u32 width, u32 height);

public:
	[[nodiscard]]
	inline Arc< Texture > GetBackImage() const { return m_pBackImages[m_ImageIndex]; }

protected:
	void CreateSwapChainResources();

private:
	RenderDevice&   m_RenderDevice;
	baamboo::Window& m_Window;

	IDXGISwapChain3* m_dxgiSwapChain = nullptr;

	u32  m_ImageIndex = 0;
	bool m_vSync = true;

	Arc< Texture > m_pBackImages[NUM_FRAMES_IN_FLIGHT];
};

}