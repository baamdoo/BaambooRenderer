#include "RendererPch.h"
#include "Dx12SwapChain.h"
#include "Dx12CommandQueue.h"
#include "RenderResource/Dx12Texture.h"
#include "RenderResource/Dx12RenderTarget.h"

#include <BaambooCore/Window.h>

namespace dx12
{

Dx12SwapChain::Dx12SwapChain(Dx12RenderDevice& rd, baamboo::Window& window)
	: m_RenderDevice(rd)
	, m_Window(window)
{
	auto d3d12CommandQueue = m_RenderDevice.GraphicsQueue().GetD3D12CommandQueue();
	IDXGIFactory4* dxgiFactory = nullptr;

	DWORD dwCreateFactoryFlags = 0;

#if defined(_DEBUG)
	dwCreateFactoryFlags = DXGI_CREATE_FACTORY_DEBUG;
#endif

	CreateDXGIFactory2(dwCreateFactoryFlags, IID_PPV_ARGS(&dxgiFactory));

	// SwapChain
	{
		DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
		swapChainDesc.Width              = m_Window.Width();
		swapChainDesc.Height             = m_Window.Height();
		swapChainDesc.Format             = DXGI_FORMAT_R8G8B8A8_UNORM;
		//swapChainDesc.BufferDesc.RefreshRate.Numerator = m_uiRefreshRate;
		//swapChainDesc.BufferDesc.RefreshRate.Denominator = 1;
		swapChainDesc.BufferUsage        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		swapChainDesc.BufferCount        = NUM_FRAMES_IN_FLIGHT;
		swapChainDesc.SampleDesc.Count   = 1;
		swapChainDesc.SampleDesc.Quality = 0;
		swapChainDesc.Scaling            = DXGI_SCALING_NONE;
		swapChainDesc.SwapEffect         = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		swapChainDesc.AlphaMode          = DXGI_ALPHA_MODE_IGNORE;
		swapChainDesc.Flags             |= m_Window.Desc().bVSync ? 0 : DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;

		DXGI_SWAP_CHAIN_FULLSCREEN_DESC fsSwapChainDesc = {};
		fsSwapChainDesc.Windowed = TRUE;

		IDXGISwapChain1* pSwapChain1 = nullptr;
		ThrowIfFailed(
			dxgiFactory->CreateSwapChainForHwnd(d3d12CommandQueue, m_Window.WinHandle(), &swapChainDesc, &fsSwapChainDesc, nullptr, &pSwapChain1)
		);
		pSwapChain1->QueryInterface(IID_PPV_ARGS(&m_dxgiSwapChain));
		pSwapChain1->Release();
		pSwapChain1 = nullptr;

		m_ImageIndex = m_dxgiSwapChain->GetCurrentBackBufferIndex();

		CreateSwapChainResources();
	}
	COM_RELEASE(dxgiFactory);

	// update values in render-device to easily be referenced by other dx12-components
	m_RenderDevice.SetWindowWidth(m_Window.Width());
	m_RenderDevice.SetWindowHeight(m_Window.Height());
}

Dx12SwapChain::~Dx12SwapChain()
{
	COM_RELEASE(m_dxgiSwapChain);
}

HRESULT Dx12SwapChain::Present()
{
	auto hr = m_dxgiSwapChain->Present(m_Window.Desc().bVSync, m_Window.Desc().bVSync ? 0 : DXGI_PRESENT_ALLOW_TEARING);

	m_ImageIndex = m_dxgiSwapChain->GetCurrentBackBufferIndex();

	return hr;
}

void Dx12SwapChain::ResizeViewport(u32 width, u32 height)
{
	if (!m_dxgiSwapChain)
		return;

	// need to release all references before resize buffers
	for (auto pTex : m_pBackImages)
	{
		assert(pTex);
		pTex->Release();
	}

	DXGI_SWAP_CHAIN_DESC1 desc = {};
	DX_CHECK(m_dxgiSwapChain->GetDesc1(&desc));
	DX_CHECK(m_dxgiSwapChain->ResizeBuffers(NUM_FRAMES_IN_FLIGHT, width, height, desc.Format, desc.Flags));
	for (u32 i = 0; i < NUM_FRAMES_IN_FLIGHT; ++i)
	{
		auto pTex = m_pBackImages[i];
		assert(pTex);

		ID3D12Resource* d3d12Resource = nullptr;
		m_dxgiSwapChain->GetBuffer(i, IID_PPV_ARGS(&d3d12Resource));

		pTex->SetD3D12Resource(d3d12Resource);
	}

	m_ImageIndex = m_dxgiSwapChain->GetCurrentBackBufferIndex();
}

void Dx12SwapChain::CreateSwapChainResources()
{
	for (u32 i = 0; i < NUM_FRAMES_IN_FLIGHT; ++i)
	{
		ID3D12Resource* d3d12Resource = nullptr;
		m_dxgiSwapChain->GetBuffer(i, IID_PPV_ARGS(&d3d12Resource));

		auto pTex = MakeArc< Dx12Texture >(m_RenderDevice, "SwapChain::RTV_" + std::to_string(i));
		pTex->SetD3D12Resource(d3d12Resource);

		m_pBackImages[i] = pTex;
	}
}

}