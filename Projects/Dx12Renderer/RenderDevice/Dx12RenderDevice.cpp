#include "RendererPch.h"
#include "Dx12RenderDevice.h"
#include "Dx12CommandQueue.h"
#include "Dx12CommandContext.h"
#include "Dx12DescriptorPool.h"
#include "Dx12DescriptorAllocation.h"
#include "Dx12ResourceManager.h"
#include "RenderResource/Dx12Resource.h"
#include "RenderResource/Dx12SceneResource.h"

#include "D3D12MemAlloc.h"

namespace dx12
{

RenderDevice::RenderDevice(bool bEnableGBV)
{
	CreateDevice(bEnableGBV);

	m_pGraphicsCommandQueue = new CommandQueue(*this, D3D12_COMMAND_LIST_TYPE_DIRECT);
	m_pComputeCommandQueue  = new CommandQueue(*this, D3D12_COMMAND_LIST_TYPE_COMPUTE);
	m_pCopyCommandQueue     = new CommandQueue(*this, D3D12_COMMAND_LIST_TYPE_COPY);

	m_pResourceManager = new ResourceManager(*this); 

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

RenderDevice::~RenderDevice()
{
	RELEASE(m_pResourceManager);

	RELEASE(m_pCopyCommandQueue);
	RELEASE(m_pComputeCommandQueue);
	RELEASE(m_pGraphicsCommandQueue);

	COM_RELEASE(m_dmaAllocator);
	COM_RELEASE(m_d3d12Device);
}

void RenderDevice::Flush()
{
	if (m_pGraphicsCommandQueue)
		m_pGraphicsCommandQueue->Flush();

	if (m_pComputeCommandQueue)
		m_pComputeCommandQueue->Flush();

	if (m_pCopyCommandQueue)
		m_pCopyCommandQueue->Flush();
}

u32 RenderDevice::Swap()
{
	u8 nextContextIndex = (m_FrameIndex + 1) % NUM_FRAMES_IN_FLIGHT;
	m_FrameIndex = nextContextIndex;

	return nextContextIndex;
}

void RenderDevice::UpdateSubresources(Arc< Resource > pResource, u32 firstSubresource, u32 numSubresources, const D3D12_SUBRESOURCE_DATA* pSrcData)
{
	auto& d3d12CommandQueue = GraphicsQueue();
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

	auto& commandList = d3d12CommandQueue.Allocate();
	{
		commandList.TransitionBarrier(pResource, D3D12_RESOURCE_STATE_COPY_DEST);
		::UpdateSubresources(commandList.GetD3D12CommandList(), pResource->GetD3D12Resource(), d3d12UploadBuffer, 0, 0, numSubresources, &pSrcData[0]);
		commandList.TransitionBarrier(pResource, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
	}
	commandList.Close();

	auto fenceValue = d3d12CommandQueue.ExecuteCommandList(&commandList);
	d3d12CommandQueue.WaitForFenceValue(fenceValue);

	COM_RELEASE(d3d12UploadBuffer);
}

ID3D12Resource* RenderDevice::CreateRHIResource(const D3D12_RESOURCE_DESC& desc, D3D12_RESOURCE_STATES initialState, D3D12_HEAP_PROPERTIES heapProperties, const D3D12_CLEAR_VALUE* pClearValue)
{
	ID3D12Resource* d3d12Resource = nullptr;

	ThrowIfFailed(m_d3d12Device->CreateCommittedResource(
		&heapProperties, D3D12_HEAP_FLAG_NONE,
		&desc, initialState,
		pClearValue, IID_PPV_ARGS(&d3d12Resource)));

	return d3d12Resource;
}

CommandContext& RenderDevice::BeginCommand(D3D12_COMMAND_LIST_TYPE commandType) const
{
	switch(commandType)
	{
	case D3D12_COMMAND_LIST_TYPE_DIRECT:
		return m_pGraphicsCommandQueue->Allocate();
	case D3D12_COMMAND_LIST_TYPE_COMPUTE:
		return m_pComputeCommandQueue->Allocate();
	case D3D12_COMMAND_LIST_TYPE_COPY:
		return m_pCopyCommandQueue->Allocate();

	default:
		break;
	}

	assert(false && "Invalid entry!");
}

DXGI_SAMPLE_DESC RenderDevice::GetMultisampleQualityLevels(DXGI_FORMAT format, D3D12_MULTISAMPLE_QUALITY_LEVEL_FLAGS flags) const
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

void RenderDevice::CreateDevice(bool bEnableGBV)
{
	ID3D12Debug* d3d12DebugController = nullptr;
	IDXGIFactory6* dxgiFactory = nullptr;
	IDXGIAdapter1* dxgiAdapter = nullptr;
	DXGI_ADAPTER_DESC1 AdapterDesc = {};

	DWORD dwCreateFactoryFlags = 0;

#if defined(_DEBUG)
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