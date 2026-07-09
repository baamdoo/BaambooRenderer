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
#include "RenderResource/Dx12ShaderBindingTable.h"
#include "RenderResource/Dx12AccelerationStructure.h"
#include "RenderResource/Dx12Texture.h"
#include "RenderResource/Dx12Buffer.h"

#include "D3D12MemAlloc.h"

#pragma warning(push, 0)
#include <tinyexr/tinyexr.h> // declarations only; TINYEXR_IMPLEMENTATION lives in Dx12EXRSupport.cpp
#pragma warning(pop)

namespace dx12
{

Dx12RenderDevice::Dx12RenderDevice(const render::DeviceSettings& ds, bool bEnableGBV)
	: Super(ds)
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
	{
		D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5 = {};
		if (SUCCEEDED(m_d3d12Device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options5, sizeof(options5))))
		{
			bool bRaytracing = options5.RaytracingTier >= D3D12_RAYTRACING_TIER_1_1;
			if (bRaytracing)
			{
				printf("D3D12Raytracing supports!\n");
			}
			else
			{
				m_Settings.bRaytracing = false;

				printf("D3D12Raytracing doesn't support!\n");
				__debugbreak();
			}
		}

		D3D12_FEATURE_DATA_D3D12_OPTIONS7 options7 = {};
		if (SUCCEEDED(m_d3d12Device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &options7, sizeof(options7))))
		{
			bool bMeshShader = options7.MeshShaderTier >= D3D12_MESH_SHADER_TIER_1;
			if (bMeshShader)
			{
				printf("D3D12MeshShader supports!\n");
			}
			else
			{
				m_Settings.bMeshShader = false;

				printf("D3D12MeshShader doesn't support!\n");
				__debugbreak();
			}
		}

		D3D12_FEATURE_DATA_D3D12_OPTIONS12 options12 = {};
		if (SUCCEEDED(m_d3d12Device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS12, &options12, sizeof(options12))))
		{
			if (!options12.EnhancedBarriersSupported)
			{
				printf("D3D12EnhancedBarriers doesn't support!\n");
				__debugbreak();
			}
		}
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

Arc< render::Buffer > Dx12RenderDevice::CreateBuffer(const char* name, render::Buffer::CreationInfo&& desc)
{
	return Dx12Buffer::Create(*this, name, std::move(desc));
}

Arc< render::Buffer > Dx12RenderDevice::CreateEmptyBuffer(const char* name)
{
	return Dx12Buffer::CreateEmpty(*this, name);
}

Arc< render::Texture > Dx12RenderDevice::CreateTexture(const char* name, render::Texture::CreationInfo&& desc)
{
	return Dx12Texture::Create(*this, name, std::move(desc));
}

Arc< render::Texture > Dx12RenderDevice::CreateEmptyTexture(const char* name)
{
	return Dx12Texture::CreateEmpty(*this, name);
}

Arc< render::RenderTarget > Dx12RenderDevice::CreateEmptyRenderTarget(const char* name)
{
	return MakeArc< Dx12RenderTarget >(name);
}

Arc< render::Sampler > Dx12RenderDevice::CreateSampler(const char* name, render::Sampler::CreationInfo&& info)
{
	return Dx12Sampler::Create(*this, name, std::move(info));
}

Arc< render::Shader > Dx12RenderDevice::CreateShader(const char* name, render::Shader::CreationInfo&& info)
{
	return Dx12Shader::Create(*this, name, std::move(info));
}

Arc< render::ShaderBindingTable > Dx12RenderDevice::CreateSBT(const char* name)
{
	return Dx12ShaderBindingTable::Create(*this, name);
}

Arc< render::BottomLevelAccelerationStructure > Dx12RenderDevice::CreateBLAS(const char* name)
{
	return Dx12BottomLevelAS::Create(*this, name);
}

Arc< render::TopLevelAccelerationStructure > Dx12RenderDevice::CreateTLAS(const char* name)
{
	return Dx12TopLevelAS::Create(*this, name);
}

Box< render::GraphicsPipeline > Dx12RenderDevice::CreateGraphicsPipeline(const char* name)
{
	return MakeBox< Dx12GraphicsPipeline >(*this, name);
}

Box< render::ComputePipeline > Dx12RenderDevice::CreateComputePipeline(const char* name)
{
	return MakeBox< Dx12ComputePipeline >(*this, name);
}

Box< render::RaytracingPipeline > Dx12RenderDevice::CreateRaytracingPipeline(const char* name)
{
	return MakeBox< Dx12RaytracingPipeline >(*this, name);
}

u32 Dx12RenderDevice::Swap()
{
	m_ContextIndex = (m_ContextIndex + 1) % kMaxFramesInFlight;

	return m_ContextIndex;
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

ID3D12Resource2* Dx12RenderDevice::CreateRHIResource(const D3D12_RESOURCE_DESC1& desc, D3D12_BARRIER_LAYOUT initialLayout, D3D12_HEAP_PROPERTIES heapProperties, const D3D12_CLEAR_VALUE* pClearValue)
{
	ID3D12Resource2* d3d12Resource = nullptr;

	ThrowIfFailed(m_d3d12Device->CreateCommittedResource3(
		&heapProperties,
		D3D12_HEAP_FLAG_NONE,
		&desc,
		initialLayout,
		pClearValue,
		nullptr,
		0, nullptr,
		IID_PPV_ARGS(&d3d12Resource))
	);

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

bool Dx12RenderDevice::SaveTextureToEXR(const Arc< render::Texture >& pTexture, const char* path)
{
	if (!pTexture || !path)
		return false;

	auto pTex = StaticCast< Dx12Texture >(pTexture);

	ID3D12Resource2* pSrcResource = pTex->GetD3D12Resource();
	if (!pSrcResource)
		return false;

	const u32 width  = pTex->GetWidth();
	const u32 height = pTex->GetHeight();

	// Part 1 only dumps RGBA32_FLOAT AOVs (albedo / normal / depth). Keep the readback path
	// trivial (straight float copy, no half->float). Extend here if other formats are needed.
	const DXGI_FORMAT format = pTex->GetFormat();
	if (format != DXGI_FORMAT_R32G32B32A32_FLOAT)
	{
		printf("[SaveTextureToEXR] '%s': unsupported format %d (expected RGBA32_FLOAT)\n", path, static_cast< int >(format));
		return false;
	}

	// GetCopyableFootprints wants a D3D12_RESOURCE_DESC; the texture exposes a DESC1.
	const D3D12_RESOURCE_DESC1 desc1 = pTex->Desc();
	D3D12_RESOURCE_DESC desc = {};
	desc.Dimension        = desc1.Dimension;
	desc.Alignment        = desc1.Alignment;
	desc.Width            = desc1.Width;
	desc.Height           = desc1.Height;
	desc.DepthOrArraySize = desc1.DepthOrArraySize;
	desc.MipLevels        = desc1.MipLevels;
	desc.Format           = desc1.Format;
	desc.SampleDesc       = desc1.SampleDesc;
	desc.Layout           = desc1.Layout;
	desc.Flags            = desc1.Flags;

	D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
	UINT   numRows        = 0;
	UINT64 rowSizeInBytes = 0;
	UINT64 totalBytes     = 0;
	m_d3d12Device->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, &numRows, &rowSizeInBytes, &totalBytes);

	// Read-back buffer is auto-mapped on creation (mapDirection == 2 -> READBACK heap).
	render::Buffer::CreationInfo ci = {};
	ci.count              = static_cast< u32 >(totalBytes);
	ci.elementSizeInBytes = 1;
	ci.mapDirection       = 2; // read-back
	ci.bufferUsage        = render::eBufferUsage_TransferDest;
	auto pReadback   = render::Buffer::Create(*this, "EXRReadback", std::move(ci));
	auto pReadbackDx = StaticCast< Dx12Buffer >(pReadback);

	// Copy texture -> read-back buffer, then block on the GPU (one-shot DIRECT submit).
	auto pContext = BeginCommand(D3D12_COMMAND_LIST_TYPE_DIRECT);
	pContext->TransitionBarrier(pTex.get(), BarrierStates::CopySource);

	D3D12_TEXTURE_COPY_LOCATION dst = {};
	dst.pResource       = pReadbackDx->GetD3D12Resource();
	dst.Type            = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
	dst.PlacedFootprint = footprint;

	D3D12_TEXTURE_COPY_LOCATION src = {};
	src.pResource        = pSrcResource;
	src.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	src.SubresourceIndex = 0;

	pContext->GetD3D12CommandList()->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
	pContext->Close();
	ExecuteCommand(std::move(pContext)).Wait();

	// De-pad rows (RowPitch is 256B-aligned) into tightly packed RGB floats (alpha dropped).
	const u8* pMapped = reinterpret_cast< const u8* >(pReadbackDx->MappedMemory());
	if (!pMapped)
		return false;

	const u32 rowPitch = footprint.Footprint.RowPitch;
	std::vector< float > image(static_cast< size_t >(width) * height * 3u);
	for (u32 y = 0; y < height; ++y)
	{
		const float* pRow = reinterpret_cast< const float* >(pMapped + static_cast< size_t >(y) * rowPitch);
		for (u32 x = 0; x < width; ++x)
		{
			const float* pSrcPixel = pRow + x * 4u; // RGBA32F
			float*       pDstPixel = image.data() + (static_cast< size_t >(y) * width + x) * 3u;
			pDstPixel[0] = pSrcPixel[0];
			pDstPixel[1] = pSrcPixel[1];
			pDstPixel[2] = pSrcPixel[2];
		}
	}

	const char* pErr = nullptr;
	const int   ret  = SaveEXR(image.data(), static_cast< int >(width), static_cast< int >(height), 3, /*save_as_fp16*/ 0, path, &pErr);
	if (ret != TINYEXR_SUCCESS)
	{
		printf("[SaveTextureToEXR] '%s': tinyexr error %d (%s)\n", path, ret, pErr ? pErr : "unknown");
		if (pErr)
			FreeEXRErrorMessage(pErr);
		return false;
	}

	printf("[SaveTextureToEXR] wrote '%s' (%ux%u RGB)\n", path, width, height);
	return true;
}

DXGI_SAMPLE_DESC Dx12RenderDevice::GetMultisampleQualityLevels(DXGI_FORMAT format, D3D12_MULTISAMPLE_QUALITY_LEVEL_FLAGS flags) const
{
	DXGI_SAMPLE_DESC sampleDesc = { 1, 0 };

	D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS qualityLevels;
	qualityLevels.Format = format;
	qualityLevels.SampleCount = 1;
	qualityLevels.Flags = flags;
	qualityLevels.NumQualityLevels = 0;

	while (qualityLevels.SampleCount <= kNumSampling
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
	//dwCreateFactoryFlags = DXGI_CREATE_FACTORY_DEBUG;
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