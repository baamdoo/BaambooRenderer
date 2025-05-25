#pragma once
#include <BaambooCore/ResourceHandle.h>

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
	SwapChain(RenderContext& context, baamboo::Window& window);
	~SwapChain();

	void Present();

	void ResizeViewport(u32 width, u32 height);

public:
	[[nodiscard]]
	inline baamboo::ResourceHandle< Texture > GetImageToPresent() const { return m_textures[m_imageIndex]; }

protected:
	void CreateSwapChainResources();

private:
	RenderContext&   m_RenderContext;
	baamboo::Window& m_window;

	IDXGISwapChain3* m_dxgiSwapChain = nullptr;

	u32  m_imageIndex = 0;
	bool m_vSync = true;

	baamboo::ResourceHandle< Texture > m_textures[NUM_FRAMES_IN_FLIGHT];
};

}