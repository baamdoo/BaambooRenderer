#pragma once

namespace baamboo
{
	class Window;
}

namespace dx12
{

class Dx12Texture;

class Dx12SwapChain
{
public:
	Dx12SwapChain(Dx12RenderDevice& rd, baamboo::Window& window);
	~Dx12SwapChain();

	HRESULT Present();

	void ResizeViewport(u32 width, u32 height);

public:
	[[nodiscard]]
	inline Arc< Dx12Texture > GetBackImage() const { return m_pBackImages[m_ImageIndex]; }

protected:
	void CreateSwapChainResources();

private:
	Dx12RenderDevice& m_RenderDevice;
	baamboo::Window&  m_Window;

	IDXGISwapChain3* m_dxgiSwapChain = nullptr;

	u32  m_ImageIndex = 0;
	bool m_vSync = true;

	Arc< Dx12Texture > m_pBackImages[NUM_FRAMES_IN_FLIGHT];
};

}