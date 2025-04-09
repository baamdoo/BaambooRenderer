#include "RendererPch.h"
#include "Dx12SwapChain.h"
#include "Dx12CommandQueue.h"
#include "Dx12ResourceManager.h"
#include "RenderResource/Dx12RenderTarget.h"

#include <BaambooCore/Window.h>

namespace dx12
{

SwapChain::SwapChain(RenderContext& context, baamboo::Window& window)
	: m_RenderContext(context)
	, m_window(window)
{
	auto d3d12CommandQueue = m_RenderContext.GetCommandQueue(D3D12_COMMAND_LIST_TYPE_DIRECT).GetD3D12CommandQueue();
	IDXGIFactory4* dxgiFactory = nullptr;

	DWORD dwCreateFactoryFlags = 0;

#if defined(_DEBUG)
	dwCreateFactoryFlags = DXGI_CREATE_FACTORY_DEBUG;
#endif

	CreateDXGIFactory2(dwCreateFactoryFlags, IID_PPV_ARGS(&dxgiFactory));

	// SwapChain
	{
		DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
		swapChainDesc.Width = m_window.Width();
		swapChainDesc.Height = m_window.Height();
		swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		//swapChainDesc.BufferDesc.RefreshRate.Numerator = m_uiRefreshRate;
		//swapChainDesc.BufferDesc.RefreshRate.Denominator = 1;
		swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		swapChainDesc.BufferCount = NUM_FRAMES;
		swapChainDesc.SampleDesc.Count = 1;
		swapChainDesc.SampleDesc.Quality = 0;
		swapChainDesc.Scaling = DXGI_SCALING_NONE;
		swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
		swapChainDesc.Flags |= m_window.Desc().bVSync ? 0 : DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;

		DXGI_SWAP_CHAIN_FULLSCREEN_DESC fsSwapChainDesc = {};
		fsSwapChainDesc.Windowed = TRUE;

		IDXGISwapChain1* pSwapChain1 = nullptr;
		ThrowIfFailed(
			dxgiFactory->CreateSwapChainForHwnd(d3d12CommandQueue, m_window.WinHandle(), &swapChainDesc, &fsSwapChainDesc, nullptr, &pSwapChain1)
		);
		pSwapChain1->QueryInterface(IID_PPV_ARGS(&m_dxgiSwapChain));
		pSwapChain1->Release();
		pSwapChain1 = nullptr;
		m_imageIndex = m_dxgiSwapChain->GetCurrentBackBufferIndex();

		CreateSwapChainResources();
	}
	COM_RELEASE(dxgiFactory);

	// update values in render-context to easily be referenced by other dx12-components
	m_RenderContext.SetViewportWidth(m_window.Width());
	m_RenderContext.SetViewportHeight(m_window.Height());
}

SwapChain::~SwapChain()
{
	COM_RELEASE(m_dxgiSwapChain);
}

void SwapChain::Present()
{
	HRESULT hr = m_dxgiSwapChain->Present(m_window.Desc().bVSync, m_window.Desc().bVSync ? 0 : DXGI_PRESENT_ALLOW_TEARING);
	if (DXGI_ERROR_DEVICE_REMOVED == hr)
	{
		__debugbreak();
	}

	m_imageIndex = m_dxgiSwapChain->GetCurrentBackBufferIndex();
}

void SwapChain::ResizeViewport(u32 width, u32 height)
{
	if (!m_dxgiSwapChain)
		return;

	auto& rm = m_RenderContext.GetResourceManager();

	// need to release all references before resize buffers
	for (u32 i = 0; i < NUM_FRAMES; ++i)
	{
		auto pTex = rm.Get(m_textures[i]);
		assert(pTex);

		pTex->Release();
	}

	DXGI_SWAP_CHAIN_DESC1 desc = {};
	ThrowIfFailed(m_dxgiSwapChain->GetDesc1(&desc));
	ThrowIfFailed(m_dxgiSwapChain->ResizeBuffers(NUM_FRAMES, width, height, desc.Format, desc.Flags));
	for (u32 i = 0; i < NUM_FRAMES; ++i)
	{
		auto pTex = rm.Get(m_textures[i]);
		assert(pTex);

		ID3D12Resource* d3d12Resource = nullptr;
		m_dxgiSwapChain->GetBuffer(i, IID_PPV_ARGS(&d3d12Resource));

		pTex->SetD3D12Resource(d3d12Resource);
	}

	m_imageIndex = m_dxgiSwapChain->GetCurrentBackBufferIndex();
}

void SwapChain::CreateSwapChainResources()
{
	auto& rm = m_RenderContext.GetResourceManager();
	for (u32 i = 0; i < NUM_FRAMES; ++i)
	{
		ID3D12Resource* d3d12Resource = nullptr;
		m_dxgiSwapChain->GetBuffer(i, IID_PPV_ARGS(&d3d12Resource));

		auto pTex = rm.CreateEmpty< Texture >(L"SwapChain::RTV_" + std::to_wstring(i));
		pTex->SetD3D12Resource(d3d12Resource);

		auto tex = rm.Add< Texture >(pTex);
		m_textures[i] = tex;
	}
}

}