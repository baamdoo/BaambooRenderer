#include "RendererPch.h"
#include "Dx12RenderDevice.h"
#include "Dx12CommandQueue.h"
#include "Dx12CommandContext.h"
#include "Dx12DescriptorPool.h"
#include "Dx12DescriptorAllocation.h"
#include "Dx12RenderPipeline.h"
#include "Dx12ResourceManager.h"
#include "RenderResource/Dx12Resource.h"
#include "RenderResource/Dx12SceneResource.h"
#include "RenderResource/Dx12RenderTarget.h"
#include "RenderResource/Dx12Shader.h"

#include "D3D12MemAlloc.h"

namespace dx12
{

Dx12RenderDevice::Dx12RenderDevice(bool bEnableGBV)
{
	CreateDevice(bEnableGBV);

	m_pGraphicsQueue = new Dx12CommandQueue(*this, D3D12_COMMAND_LIST_TYPE_DIRECT);
	m_pComputeQueue  = new Dx12CommandQueue(*this, D3D12_COMMAND_LIST_TYPE_COMPUTE);
	m_pCopyQueue     = new Dx12CommandQueue(*this, D3D12_COMMAND_LIST_TYPE_COPY);

	m_pResourceManager = new Dx12ResourceManager(*this); 

	m_SRVDescriptorSize = m_d3d12Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	m_RTVDescriptorSize = m_d3d12Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	m_DSVDescriptorSize = m_d3d12Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

	{
		D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData;
		featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
		if (FAILED(m_d3d12Device->CheckFeatureSupport(
			D3D12_FEATURE_ROOT_SIGNATURE, 
			&featureData, 
			sizeof(D3D12_FEATURE_DATA_ROOT_SIGNATURE)
		)))
		{
			featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
		}
		m_HighestRootSignatureVersion = featureData.HighestVersion;
	}
}

Dx12RenderDevice::~Dx12RenderDevice()
{
	RELEASE(m_pResourceManager);

	RELEASE(m_pCopyQueue);
	RELEASE(m_pComputeQueue);
	RELEASE(m_pGraphicsQueue);

	COM_RELEASE(m_dmaAllocator);
	COM_RELEASE(m_d3d12Device);
}

void Dx12RenderDevice::Flush()
{
	if (m_pGraphicsQueue)
		m_pGraphicsQueue->Flush();

	if (m_pComputeQueue)
		m_pComputeQueue->Flush();

	if (m_pCopyQueue)
		m_pCopyQueue->Flush();
}

Arc< render::Buffer > Dx12RenderDevice::CreateBuffer(const std::string& name, render::Buffer::CreationInfo&& desc)
{
	return Dx12Buffer::Create(*this, name, std::move(desc));
}

Arc< render::Buffer > Dx12RenderDevice::CreateEmptyBuffer(const std::string& name)
{
	return Dx12Buffer::CreateEmpty(*this, name);
}

Arc< render::Texture > Dx12RenderDevice::CreateTexture(const std::string& name, render::Texture::CreationInfo&& desc)
{
	return Dx12Texture::Create(*this, name, std::move(desc));
}

Arc< render::Texture > Dx12RenderDevice::CreateEmptyTexture(const std::string& name)
{
	return Dx12Texture::CreateEmpty(*this, name);
}

Arc< render::RenderTarget > Dx12RenderDevice::CreateEmptyRenderTarget(const std::string& name)
{
	return MakeArc< Dx12RenderTarget >(name);
}

Arc< render::Sampler > Dx12RenderDevice::CreateSampler(const std::string& name, render::Sampler::CreationInfo&& info)
{
	return Dx12Sampler::Create(*this, name, std::move(info));
}

Arc< render::Shader > Dx12RenderDevice::CreateShader(const std::string& name, render::Shader::CreationInfo&& info)
{
	return Dx12Shader::Create(*this, name, std::move(info));
}

Box< render::ComputePipeline > Dx12RenderDevice::CreateComputePipeline(const std::string& name)
{
	return MakeBox< Dx12ComputePipeline >(*this, name);
}

Box< render::GraphicsPipeline > Dx12RenderDevice::CreateGraphicsPipeline(const std::string& name)
{
	return MakeBox< Dx12GraphicsPipeline >(*this, name);
}

u32 Dx12RenderDevice::Swap()
{
	u8 nextContextIndex = (m_ContextIndex + 1) % NUM_FRAMES_IN_FLIGHT;
	m_ContextIndex = nextContextIndex;

	return nextContextIndex;
}

Box< render::SceneResource > Dx12RenderDevice::CreateSceneResource()
{
	return MakeBox< Dx12SceneResource >(*this);
}

void Dx12RenderDevice::UpdateSubresources(Dx12Resource* pResource, u32 firstSubresource, u32 numSubresources, const D3D12_SUBRESOURCE_DATA* pSrcData)
{
	u64 uploadBufferSize    = GetRequiredIntermediateSize(pResource->GetD3D12Resource(), firstSubresource, numSubresources);

	auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
	auto desc      = CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize);

	ID3D12Resource* d3d12UploadBuffer = nullptr;
	ThrowIfFailed(m_d3d12Device->CreateCommittedResource(
		&heapProps,
		D3D12_HEAP_FLAG_NONE,
		&desc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&d3d12UploadBuffer))
	);

	auto pContext = BeginCommand(D3D12_COMMAND_LIST_TYPE_COPY);
	{
		// All resource state should be common in copy queue
		//pContext->TransitionBarrier(pResource, D3D12_RESOURCE_STATE_COPY_DEST, ALL_SUBRESOURCES, true);
		::UpdateSubresources(pContext->GetD3D12CommandList(), pResource->GetD3D12Resource(), d3d12UploadBuffer, 0, 0, numSubresources, &pSrcData[0]);
		//pContext->TransitionBarrier(pResource, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
		pContext->Close();
	}
	ExecuteCommand(std::move(pContext)).Wait();

	COM_RELEASE(d3d12UploadBuffer);
}

ID3D12Resource* Dx12RenderDevice::CreateRHIResource(const D3D12_RESOURCE_DESC& desc, D3D12_RESOURCE_STATES initialState, D3D12_HEAP_PROPERTIES heapProperties, const D3D12_CLEAR_VALUE* pClearValue)
{
	ID3D12Resource* d3d12Resource = nullptr;

	ThrowIfFailed(m_d3d12Device->CreateCommittedResource(
		&heapProperties, D3D12_HEAP_FLAG_NONE,
		&desc, initialState,
		pClearValue, IID_PPV_ARGS(&d3d12Resource)));

	return d3d12Resource;
}

Arc< Dx12CommandContext > Dx12RenderDevice::BeginCommand(D3D12_COMMAND_LIST_TYPE commandType)
{
	switch (commandType)
	{
	case D3D12_COMMAND_LIST_TYPE_DIRECT:
		return m_pGraphicsQueue->Allocate();
	case D3D12_COMMAND_LIST_TYPE_COMPUTE:
		return m_pComputeQueue->Allocate();
	case D3D12_COMMAND_LIST_TYPE_COPY:
		return m_pCopyQueue->Allocate();

	default:
		__debugbreak();
		break;
	}

	assert(false && "Invalid entry!");
	return m_pGraphicsQueue->Allocate();
}

SyncObject Dx12RenderDevice::ExecuteCommand(Arc< Dx12CommandContext >&& pContext)
{
	switch (pContext->GetCommandListType())
	{
	case D3D12_COMMAND_LIST_TYPE_DIRECT:
		return { GraphicsQueue().ExecuteCommandList(pContext), GraphicsQueue() };
	case D3D12_COMMAND_LIST_TYPE_COMPUTE:
		return { ComputeQueue().ExecuteCommandList(pContext), ComputeQueue() };
	case D3D12_COMMAND_LIST_TYPE_COPY:
		return { CopyQueue().ExecuteCommandList(pContext), CopyQueue() };
	}

	__debugbreak();
	assert(false && "Invalid command list execution!");
	return { 0, GraphicsQueue() };
}

render::ResourceManager& Dx12RenderDevice::GetResourceManager() const
{
	return *m_pResourceManager;
}

DXGI_SAMPLE_DESC Dx12RenderDevice::GetMultisampleQualityLevels(DXGI_FORMAT format, D3D12_MULTISAMPLE_QUALITY_LEVEL_FLAGS flags) const
{
	DXGI_SAMPLE_DESC sampleDesc = { 1, 0 };

	D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS qualityLevels;
	qualityLevels.Format = format;
	qualityLevels.SampleCount = 1;
	qualityLevels.Flags = flags;
	qualityLevels.NumQualityLevels = 0;

	while (qualityLevels.SampleCount <= NUM_SAMPLING
		&& SUCCEEDED(m_d3d12Device->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &qualityLevels, sizeof(D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS))) 
		&& qualityLevels.NumQualityLevels > 0)
	{
		sampleDesc.Count = qualityLevels.SampleCount;
		sampleDesc.Quality = qualityLevels.NumQualityLevels - 1;

		qualityLevels.SampleCount *= 2;
	}

	return sampleDesc;
}

void Dx12RenderDevice::CreateDevice(bool bEnableGBV)
{
	ID3D12Debug* d3d12DebugController = nullptr;
	IDXGIFactory6* dxgiFactory = nullptr;
	IDXGIAdapter1* dxgiAdapter = nullptr;
	DXGI_ADAPTER_DESC1 AdapterDesc = {};

	DWORD dwCreateFactoryFlags = 0;

#if defined(_DEBUG)
	/*ID3D12DeviceRemovedExtendedDataSettings2* pDredSettings;
	DX_CHECK(D3D12GetDebugInterface(IID_PPV_ARGS(&pDredSettings)));

	pDredSettings->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
	pDredSettings->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);*/

	if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&d3d12DebugController))))
	{
		d3d12DebugController->EnableDebugLayer();
	}
	dwCreateFactoryFlags = DXGI_CREATE_FACTORY_DEBUG;
	if (bEnableGBV)
	{
		ID3D12Debug5* d3d12DebugController5 = nullptr;
		if (S_OK == d3d12DebugController->QueryInterface(IID_PPV_ARGS(&d3d12DebugController5)))
		{
			d3d12DebugController5->SetEnableGPUBasedValidation(TRUE);
			d3d12DebugController5->SetGPUBasedValidationFlags(
				D3D12_GPU_BASED_VALIDATION_FLAGS_DISABLE_STATE_TRACKING
			);
			d3d12DebugController5->SetEnableAutoName(TRUE);
			d3d12DebugController5->Release();
		}
	}
#endif

	CreateDXGIFactory2(dwCreateFactoryFlags, IID_PPV_ARGS(&dxgiFactory));

	D3D_FEATURE_LEVEL	featureLevels[] =
	{
		D3D_FEATURE_LEVEL_12_2,
		D3D_FEATURE_LEVEL_12_1,
		D3D_FEATURE_LEVEL_12_0,
		D3D_FEATURE_LEVEL_11_1,
		D3D_FEATURE_LEVEL_11_0
	};

	DWORD FeatureLevelNum = _countof(featureLevels);
	for (DWORD featerLevelIndex = 0; featerLevelIndex < FeatureLevelNum; featerLevelIndex++)
	{
		u32 adapterIndex = 0;
		while (DXGI_ERROR_NOT_FOUND != dxgiFactory->EnumAdapterByGpuPreference(adapterIndex, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&dxgiAdapter)))
		{
			dxgiAdapter->GetDesc1(&AdapterDesc);
			if (SUCCEEDED(D3D12CreateDevice(dxgiAdapter, featureLevels[featerLevelIndex], IID_PPV_ARGS(&m_d3d12Device))))
			{
				using namespace D3D12MA;

				ALLOCATOR_DESC allocatorDesc = {};
				allocatorDesc.pDevice = m_d3d12Device;
				allocatorDesc.pAdapter = dxgiAdapter;
				allocatorDesc.Flags = D3D12MA_RECOMMENDED_ALLOCATOR_FLAGS;
				CreateAllocator(&allocatorDesc, &m_dmaAllocator);

				goto lb_exit;
			}

			dxgiAdapter->Release();
			dxgiAdapter = nullptr;
			adapterIndex++;
		}
	}
lb_exit:

	if (!m_d3d12Device)
	{
		__debugbreak();
	}

	if (d3d12DebugController)
	{
		d3d12DebugController->Release();
		d3d12DebugController = nullptr;
	}

	if (dxgiAdapter)
	{
		dxgiAdapter->Release();
		dxgiAdapter = nullptr;
	}

	if (dxgiFactory)
	{
		dxgiFactory->Release();
		dxgiFactory = nullptr;
	}
}

void SyncObject::Wait()
{
	m_CommandQueue.WaitForFenceValue(m_FenceValue);
}

}