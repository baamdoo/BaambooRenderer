#include "RendererPch.h"
#include "Dx12RenderDevice.h"
#include "Dx12CommandQueue.h"
#include "Dx12CommandContext.h"
#include "Dx12DescriptorPool.h"
#include "Dx12DescriptorAllocation.h"
#include "Dx12RootSignature.h"
#include "Dx12CommandSignature.h"
#include "Dx12RenderPipeline.h"
#include "Dx12ResourceManager.h"

#include "RenderResource/Dx12Resource.h"
#include "RenderResource/Dx12Sampler.h"
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

	m_pRtvDescriptorPool =
		MakeBox< DescriptorPool >(*this, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, kMaxNumDescriptorPerPool[D3D12_DESCRIPTOR_HEAP_TYPE_RTV]);
	m_pDsvDescriptorPool =
		MakeBox< DescriptorPool >(*this, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, kMaxNumDescriptorPerPool[D3D12_DESCRIPTOR_HEAP_TYPE_DSV]);

	m_pGlobalDescriptorHeap
		= MakeArc< DescriptorPool >(*this, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, kMaxGlobalDescriptors);

	m_pGlobalRootSignature =
		MakeArc< Dx12RootSignature >(
			*this,
			"GlobalRootSignature",
			D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT | D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED
		);

	static_assert(1u + render::kSlangPushBlockBytes / 4u <= D3D12_MAX_ROOT_COST, "Root signature exceeds D3D12_MAX_ROOT_COST");
	m_pGlobalRootSignature->AddConstants(render::kSlangDrawIDRegister, render::kSlangDrawIDSpace, 1u);
	m_pGlobalRootSignature->AddConstants(render::kSlangPushRegister, render::kSlangPushSpace, render::kSlangPushBlockBytes / 4u);

	// Samplers
	m_pGlobalRootSignature->AddSamplerPreset(static_cast<u32>(render::eSamplerIndex::PointWrap));
	m_pGlobalRootSignature->AddSamplerPreset(static_cast<u32>(render::eSamplerIndex::PointClamp));
	m_pGlobalRootSignature->AddSamplerPreset(static_cast<u32>(render::eSamplerIndex::LinearWrap));
	m_pGlobalRootSignature->AddSamplerPreset(static_cast<u32>(render::eSamplerIndex::LinearClamp));
	m_pGlobalRootSignature->AddSamplerPreset(static_cast<u32>(render::eSamplerIndex::TrilinearWrap));
	m_pGlobalRootSignature->AddSamplerPreset(static_cast<u32>(render::eSamplerIndex::AnisotropicWrap));
	m_pGlobalRootSignature->AddSamplerPreset(static_cast<u32>(render::eSamplerIndex::ShadowCmpLessEqual));
	m_pGlobalRootSignature->AddSamplerPreset(static_cast<u32>(render::eSamplerIndex::PointClampMin));
	m_pGlobalRootSignature->AddSamplerPreset(static_cast<u32>(render::eSamplerIndex::LinearClampMin));

	m_pGlobalRootSignature->Build();

	m_pDispatchSignature = MakeBox< CommandSignature >(
		*this,
		CommandSignatureDesc(1, sizeof(D3D12_DISPATCH_ARGUMENTS))
			.AddDispatch(),
		nullptr);

	if (m_Settings.bMeshShader)
	{
		m_pMeshDispatchSignature = MakeBox< CommandSignature >(
			*this,
			CommandSignatureDesc(2, sizeof(IndirectCommandData))
				.AddConstant(0, 0, 1)
				.AddDispatchMesh(),
			m_pGlobalRootSignature->GetD3D12RootSignature());
	}

	m_pResourceManager = new Dx12ResourceManager(*this);
}

Dx12RenderDevice::~Dx12RenderDevice()
{
	RELEASE(m_pResourceManager);

	RELEASE(m_pCopyQueue);
	RELEASE(m_pComputeQueue);
	RELEASE(m_pGraphicsQueue);

	m_pMeshDispatchSignature.reset();
	m_pDispatchSignature.reset();
	m_pGlobalRootSignature.reset();

	FlushDeferredReleases(); // slots are managed by pools so resources should be release before pool

	m_pGlobalDescriptorHeap.reset();
	m_pDsvDescriptorPool.reset();
	m_pRtvDescriptorPool.reset();

	COM_RELEASE(m_dmaAllocator);
	if (m_d3d12InfoQueue)
	{
		m_d3d12InfoQueue->UnregisterMessageCallback(m_ValidationCallbackCookie);
		COM_RELEASE(m_d3d12InfoQueue);
	}
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
		::UpdateSubresources(pContext->GetD3D12CommandList(), pResource->GetD3D12Resource(), d3d12UploadBuffer, 0, 0, numSubresources, &pSrcData[0]);
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

DescriptorAllocation Dx12RenderDevice::AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE type, u32 numDescriptors)
{
	switch (type)
	{
	case D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV:
		return m_pGlobalDescriptorHeap->Allocate(numDescriptors);
	case D3D12_DESCRIPTOR_HEAP_TYPE_RTV:
		return m_pRtvDescriptorPool->Allocate(numDescriptors);
	case D3D12_DESCRIPTOR_HEAP_TYPE_DSV:
		return m_pDsvDescriptorPool->Allocate(numDescriptors);

	default:
		__debugbreak();
		assert(false && "Invalid entry!");
	}

	return DescriptorAllocation();
}

Arc< Dx12RootSignature > Dx12RenderDevice::GetGlobalRootSignature() const
{
	return m_pGlobalRootSignature;
}

void Dx12RenderDevice::DeferRelease(IUnknown* d3d12Object)
{
	if (d3d12Object)
		DeferRelease([d3d12Object] { d3d12Object->Release(); });
}

Arc< DescriptorPool > Dx12RenderDevice::GetGlobalDescriptorHeap() const
{
	return m_pGlobalDescriptorHeap;
}

ID3D12CommandSignature* Dx12RenderDevice::GetDispatchD3D12CommandSignature() const
{
	return m_pDispatchSignature->GetD3D12CommandSignature();
}

ID3D12CommandSignature* Dx12RenderDevice::GetMeshDispatchD3D12CommandSignature() const
{
	return m_pMeshDispatchSignature->GetD3D12CommandSignature();
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

	const DXGI_FORMAT format = pTex->GetFormat();
	if (format != DXGI_FORMAT_R32G32B32A32_FLOAT
	 && format != DXGI_FORMAT_R16G16B16A16_FLOAT
	 && format != DXGI_FORMAT_R8G8B8A8_UNORM)
	{
		printf("[SaveTextureToEXR] '%s': unsupported format %d\n", path, static_cast< int >(format));
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

	auto HalfToFloat = [](u16 h) -> float
	{
		const u32 sign = static_cast< u32 >(h & 0x8000u) << 16;
		u32 exp = (h >> 10) & 0x1Fu;
		u32 man = h & 0x3FFu;

		u32 bits;
		if (exp == 0)
		{
			if (man == 0)
			{
				bits = sign;
			}
			else
			{
				exp = 127 - 15 + 1;
				while ((man & 0x400u) == 0) { man <<= 1; --exp; }
				man &= 0x3FFu;
				bits = sign | (exp << 23) | (man << 13);
			}
		}
		else if (exp == 31)
		{
			bits = sign | 0x7F800000u | (man << 13);
		}
		else
		{
			bits = sign | ((exp - 15 + 127) << 23) | (man << 13);
		}

		float f;
		std::memcpy(&f, &bits, sizeof(f));
		return f;
	};

	const u32 rowPitch = footprint.Footprint.RowPitch;
	std::vector< float > image(static_cast< size_t >(width) * height * 3u);
	for (u32 y = 0; y < height; ++y)
	{
		const u8* pRow    = pMapped + static_cast< size_t >(y) * rowPitch;
		float*    pDstRow = image.data() + static_cast< size_t >(y) * width * 3u;
		switch (format)
		{
		case DXGI_FORMAT_R32G32B32A32_FLOAT:
		{
			const float* pSrc = reinterpret_cast< const float* >(pRow);
			for (u32 x = 0; x < width; ++x)
			{
				pDstRow[x * 3u + 0] = pSrc[x * 4u + 0];
				pDstRow[x * 3u + 1] = pSrc[x * 4u + 1];
				pDstRow[x * 3u + 2] = pSrc[x * 4u + 2];
			}
			break;
		}
		case DXGI_FORMAT_R16G16B16A16_FLOAT:
		{
			const u16* pSrc = reinterpret_cast< const u16* >(pRow);
			for (u32 x = 0; x < width; ++x)
			{
				pDstRow[x * 3u + 0] = HalfToFloat(pSrc[x * 4u + 0]);
				pDstRow[x * 3u + 1] = HalfToFloat(pSrc[x * 4u + 1]);
				pDstRow[x * 3u + 2] = HalfToFloat(pSrc[x * 4u + 2]);
			}
			break;
		}
		case DXGI_FORMAT_R8G8B8A8_UNORM:
		{
			const u8* pSrc = pRow;
			for (u32 x = 0; x < width; ++x)
			{
				pDstRow[x * 3u + 0] = pSrc[x * 4u + 0] * (1.0f / 255.0f);
				pDstRow[x * 3u + 1] = pSrc[x * 4u + 1] * (1.0f / 255.0f);
				pDstRow[x * 3u + 2] = pSrc[x * 4u + 2] * (1.0f / 255.0f);
			}
			break;
		}
		default:
			break;
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
		RegisterValidationCallback();

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

void Dx12RenderDevice::RegisterValidationCallback()
{
	// ID3D12InfoQueue1 only exists when the debug layer is active (Windows 11 / Agility SDK runtime).
	if (FAILED(m_d3d12Device->QueryInterface(IID_PPV_ARGS(&m_d3d12InfoQueue))))
	{
		m_d3d12InfoQueue = nullptr;
		fprintf(stderr, "D3D12 validation: ID3D12InfoQueue1 unavailable; validation errors are not counted\n");
		return;
	}

	if (FAILED(m_d3d12InfoQueue->RegisterMessageCallback(
		&Dx12RenderDevice::OnValidationMessage, D3D12_MESSAGE_CALLBACK_FLAG_NONE, this, &m_ValidationCallbackCookie)))
	{
		fprintf(stderr, "D3D12 validation: RegisterMessageCallback failed; validation errors are not counted\n");
		COM_RELEASE(m_d3d12InfoQueue);
	}
}

void CALLBACK Dx12RenderDevice::OnValidationMessage(
	D3D12_MESSAGE_CATEGORY category, D3D12_MESSAGE_SEVERITY severity, D3D12_MESSAGE_ID id, LPCSTR description, void* pContext)
{
	UNUSED(category);
	if (severity != D3D12_MESSAGE_SEVERITY_CORRUPTION && severity != D3D12_MESSAGE_SEVERITY_ERROR)
		return;

	auto* pDevice = static_cast< Dx12RenderDevice* >(pContext);
	pDevice->m_ValidationErrorCount.fetch_add(1, std::memory_order_relaxed);
	fprintf(stderr, "D3D12 validation error (id %d): %s\n", static_cast< i32 >(id), description ? description : "");
}

void SyncObject::Wait()
{
	m_CommandQueue.WaitForFenceValue(m_FenceValue);
}

}