#include "RendererPch.h"
#include "Dx12CommandContext.h"
#include "Dx12RenderDevice.h"
#include "Dx12CommandQueue.h"
#include "Dx12BufferAllocator.h"
#include "Dx12RootSignature.h"
#include "Dx12RenderPipeline.h"
#include "Dx12DescriptorHeap.h"
#include "RenderResource/Dx12Buffer.h"
#include "RenderResource/Dx12Texture.h"
#include "RenderResource/Dx12RenderTarget.h"
#include "RenderResource/Dx12SceneResource.h"
#include "RenderResource/Dx12RenderTarget.h"

namespace dx12
{
	
//-------------------------------------------------------------------------
// Impl
//-------------------------------------------------------------------------
class Dx12CommandContext::Impl
{
public:
	Impl(Dx12RenderDevice& rd, D3D12_COMMAND_LIST_TYPE type);
	~Impl();

	void Open();
	void Close();

	void TransitionBarrier(Dx12Resource* pResource, D3D12_RESOURCE_STATES stateAfter, u32 subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, bool bFlushImmediate = true);
	void UAVBarrier(Dx12Resource* pResource, bool bFlushImmediate = false);
	void AliasingBarrier(Dx12Resource* pResourceBefore, Dx12Resource* pResourceAfter, bool bFlushImmediate = false);

	void CopyBuffer(const Arc< Dx12Buffer >& pDstBuffer, const Arc< Dx12Buffer >& pSrcBuffer, size_t sizeInBytes, size_t offsetInBytes);
	void CopyBuffer(ID3D12Resource* d3d12DstBuffer, ID3D12Resource* d3d12SrcBuffer, SIZE_T sizeInBytes, SIZE_T dstOffsetInBytes);
	void CopyTexture(const Arc< Dx12Texture >& pDstTexture, const Arc< Dx12Texture >& pSrcTexture);
	void ResolveSubresource(Dx12Resource* pDstResource, Dx12Resource* pSrcResource, u32 dstSubresource = 0, u32 srcSubresource = 0);

	void SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY primitiveTopology);

	void ClearTexture(const Arc< Dx12Texture >& pTexture);
	void ClearDepthStencilTexture(const Arc< Dx12Texture >& pTexture, D3D12_CLEAR_FLAGS clearFlags);

	void SetViewport(const D3D12_VIEWPORT& viewport);
	void SetViewports(const std::vector< D3D12_VIEWPORT >& viewports);

	void SetScissorRect(const D3D12_RECT& scissorRect);
	void SetScissorRects(const std::vector< D3D12_RECT >& scissorRects);

	void SetRenderPipeline(Dx12GraphicsPipeline* pGraphicsPipelineState);
	void SetRenderPipeline(Dx12ComputePipeline* pComputePipelineState);

	void SetGraphicsRootSignature(Arc< Dx12RootSignature > pRootSignature);
	void SetComputeRootSignature(Arc< Dx12RootSignature > pRootSignature);

	void SetDescriptorHeaps(const std::vector< ID3D12DescriptorHeap* >& d3d12DescriptorHeaps);

	void SetRenderTarget(u32 numRenderTargets, D3D12_CPU_DESCRIPTOR_HANDLE rtv, D3D12_CPU_DESCRIPTOR_HANDLE dsv = D3D12_CPU_DESCRIPTOR_HANDLE());
	void BeginRenderPass(Arc< Dx12RenderTarget > pRenderTarget);

	void SetGraphicsRootConstant(u32 srcValue, u32 dstOffset = 0);
	void SetGraphicsRootConstants(u32 srcSizeInBytes, const void* pSrcData, u32 dstOffsetInBytes = 0);
	void SetComputeRootConstant(u32 srcValue, u32 dstOffset = 0);
	void SetComputeRootConstants(u32 srcSizeInBytes, const void* pSrcData, u32 dstOffsetInBytes = 0);

	void SetGraphicsDynamicConstantBuffer(const std::string& name, size_t sizeInBytes, const void* pData);
	template< typename T >
	void SetGraphicsDynamicConstantBuffer(const std::string& name, const T& data)
	{
		SetGraphicsDynamicConstantBuffer(name, sizeof(T), &data);
	}
	void SetComputeDynamicConstantBuffer(const std::string& name, size_t sizeInBytes, const void* pData);
	template< typename T >
	void SetComputeDynamicConstantBuffer(const std::string& name, const T& data)
	{
		SetComputeDynamicConstantBuffer(name, sizeof(T), &data);
	}

	void SetGraphicsConstantBufferView(const std::string& name, D3D12_GPU_VIRTUAL_ADDRESS gpuHandle);
	void SetGraphicsShaderResourceView(const std::string& name, D3D12_GPU_VIRTUAL_ADDRESS gpuHandle);
	void SetComputeConstantBufferView(const std::string& name, D3D12_GPU_VIRTUAL_ADDRESS gpuHandle);
	void SetComputeShaderResourceView(const std::string& name, D3D12_GPU_VIRTUAL_ADDRESS gpuHandle);
	void SetComputeUnorderedAccessView(const std::string& name, D3D12_GPU_VIRTUAL_ADDRESS gpuHandle);

	void StageDescriptor(
		const std::string& name,
		Arc< Dx12StructuredBuffer > pBuffer,
		D3D12_DESCRIPTOR_HEAP_TYPE heapType = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	void StageDescriptor(
		const std::string& name,
		Arc< Dx12Texture > pTexture,
		D3D12_DESCRIPTOR_HEAP_TYPE heapType = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	void StageDescriptor(
		const std::string& name,
		D3D12_CPU_DESCRIPTOR_HANDLE srcHandle,
		D3D12_DESCRIPTOR_HEAP_TYPE heapType = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	void StageDescriptors(
		std::vector< std::pair< std::string, D3D12_CPU_DESCRIPTOR_HANDLE > >&& srcHandles,
		D3D12_DESCRIPTOR_HEAP_TYPE heapType = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	void StageDescriptors(
		u32 rootIndex,
		u32 offset,
		std::vector< D3D12_CPU_DESCRIPTOR_HANDLE >&& srcHandles,
		D3D12_DESCRIPTOR_HEAP_TYPE heapType = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	void Draw(u32 vertexCount, u32 instanceCount = 1, u32 startVertex = 0, u32 startInstance = 0);
	void DrawIndexed(u32 indexCount, u32 instanceCount = 1, u32 startIndex = 0, u32 baseVertex = 0, u32 startInstance = 0);
	void DrawScene(const Dx12SceneResource& sceneResource);
	void Dispatch(u32 numGroupsX, u32 numGroupsY, u32 numGroupsZ);

	bool IsComputeContext() const { return m_pComputePipeline != nullptr; }
	bool IsGraphicsContext() const { return m_pGraphicsPipeline != nullptr; }

private:
	void AddBarrier(const D3D12_RESOURCE_BARRIER& barrier, bool bFlushImmediate);
	void FlushResourceBarriers();

	void BindDescriptorHeaps();

public:
	D3D12_COMMAND_LIST_TYPE GetCommandListType() const { return m_Type; }
	ID3D12GraphicsCommandList2* GetD3D12CommandList() const { return m_d3d12CommandList2; }

	template< typename T >
	constexpr T RoundUpAndDivide(T Value, size_t Alignment)
	{
		return (T)((Value + Alignment - 1) / Alignment);
	}

private:
	Dx12RenderDevice& m_RenderDevice;
	D3D12_COMMAND_LIST_TYPE m_Type = {};

	DynamicBufferAllocator* m_pDynamicBufferAllocator = nullptr;

	ID3D12GraphicsCommandList2* m_d3d12CommandList2     = nullptr;
	ID3D12CommandAllocator*     m_d3d12CommandAllocator = nullptr;

	Arc< Dx12RootSignature > m_pRootSignature;

	Dx12GraphicsPipeline* m_pGraphicsPipeline = nullptr;
	Dx12ComputePipeline*  m_pComputePipeline  = nullptr;

	D3D_PRIMITIVE_TOPOLOGY m_PrimitiveTopology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;

	Dx12DescriptorHeap*   m_pDescriptorHeaps[NUM_RESOURCE_DESCRIPTOR_TYPE]               = {};
	ID3D12DescriptorHeap* m_CurrentDescriptorHeaps[D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES] = {};

	u32                    m_NumBarriersToFlush = 0;
	D3D12_RESOURCE_BARRIER m_ResourceBarriers[MAX_NUM_PENDING_BARRIERS] = {};
};

Dx12CommandContext::Impl::Impl(Dx12RenderDevice& rd, D3D12_COMMAND_LIST_TYPE type)
	: m_RenderDevice(rd)
	, m_Type(type)
{
	auto d3d12Device = m_RenderDevice.GetD3D12Device();

	ThrowIfFailed(d3d12Device->CreateCommandAllocator(m_Type, IID_PPV_ARGS(&m_d3d12CommandAllocator)));
	ThrowIfFailed(d3d12Device->CreateCommandList1(
		0, m_Type, D3D12_COMMAND_LIST_FLAG_NONE, IID_PPV_ARGS(&m_d3d12CommandList2)));

	m_pDynamicBufferAllocator = new DynamicBufferAllocator(m_RenderDevice);
	
	for (u32 i = 0; i < NUM_RESOURCE_DESCRIPTOR_TYPE; ++i)
	{
		m_pDescriptorHeaps[i] = 
			new Dx12DescriptorHeap(m_RenderDevice, (D3D12_DESCRIPTOR_HEAP_TYPE)i, MAX_NUM_DESCRIPTOR_PER_POOL[i]);
	}
}

Dx12CommandContext::Impl::~Impl()
{
	for (u32 i = 0; i < NUM_RESOURCE_DESCRIPTOR_TYPE; ++i)
	{
		RELEASE(m_pDescriptorHeaps[i]);
	}
	RELEASE(m_pDynamicBufferAllocator);

	COM_RELEASE(m_d3d12CommandList2);
	COM_RELEASE(m_d3d12CommandAllocator);
}

void Dx12CommandContext::Impl::Open()
{
	m_pGraphicsPipeline = nullptr;
	m_pComputePipeline  = nullptr;
	m_pRootSignature    = nullptr;
	m_PrimitiveTopology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;

	ThrowIfFailed(m_d3d12CommandAllocator->Reset());
	ThrowIfFailed(m_d3d12CommandList2->Reset(m_d3d12CommandAllocator, nullptr));

	if (m_Type != D3D12_COMMAND_LIST_TYPE_COPY)
	{
		m_pDynamicBufferAllocator->Reset();
		for (u32 i = 0; i < NUM_RESOURCE_DESCRIPTOR_TYPE; ++i)
		{
			m_pDescriptorHeaps[i]->Reset();
		}

		BindDescriptorHeaps();
	}
}

void Dx12CommandContext::Impl::Close()
{
	FlushResourceBarriers();
	m_d3d12CommandList2->Close();
}

void Dx12CommandContext::Impl::TransitionBarrier(Dx12Resource* pResource, D3D12_RESOURCE_STATES stateAfter, u32 subresource, bool bFlushImmediate)
{
	if (pResource)
	{
		const auto& stateBefore = pResource->GetCurrentState();
		if (stateBefore.GetSubresourceState(subresource) != stateAfter)
		{
			AddBarrier(
				CD3DX12_RESOURCE_BARRIER::Transition(
					pResource->GetD3D12Resource(), 
					stateBefore.GetSubresourceState(subresource), 
					stateAfter, 
					subresource
				), bFlushImmediate
			);

			pResource->SetCurrentState(stateAfter, subresource);
		}
	}
}

void Dx12CommandContext::Impl::UAVBarrier(Dx12Resource* pResource, bool bFlushImmediate)
{
	if (pResource)
	{
		AddBarrier(CD3DX12_RESOURCE_BARRIER::UAV(pResource->GetD3D12Resource()), bFlushImmediate);
	}
}

void Dx12CommandContext::Impl::AliasingBarrier(Dx12Resource* pResourceBefore, Dx12Resource* pResourceAfter, bool bFlushImmediate)
{
	if (pResourceBefore && pResourceAfter)
	{
		AddBarrier(
			CD3DX12_RESOURCE_BARRIER::Aliasing(
				pResourceBefore->GetD3D12Resource(),
				pResourceAfter->GetD3D12Resource()
			), bFlushImmediate
		);
	}
}

void Dx12CommandContext::Impl::CopyBuffer(const Arc< Dx12Buffer >& pDstBuffer, const Arc< Dx12Buffer >& pSrcBuffer, size_t sizeInBytes, size_t offsetInBytes)
{
	CopyBuffer(pDstBuffer->GetD3D12Resource(), pSrcBuffer->GetD3D12Resource(), sizeInBytes, offsetInBytes);
}

void Dx12CommandContext::Impl::CopyBuffer(ID3D12Resource* d3d12DstBuffer, ID3D12Resource* d3d12SrcBuffer, SIZE_T sizeInBytes, SIZE_T dstOffsetInBytes)
{
	m_d3d12CommandList2->CopyBufferRegion(d3d12DstBuffer, dstOffsetInBytes, d3d12SrcBuffer, 0, sizeInBytes);
}

void Dx12CommandContext::Impl::CopyTexture(const Arc< Dx12Texture >& pDstTexture, const Arc< Dx12Texture >& pSrcTexture)
{
	TransitionBarrier(pDstTexture.get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, false);
	TransitionBarrier(pSrcTexture.get(), D3D12_RESOURCE_STATE_COPY_SOURCE);

	D3D12_RESOURCE_DESC Desc = pDstTexture->Desc();
	for (u16 i = 0; i < Desc.MipLevels; i++)
	{
		D3D12_TEXTURE_COPY_LOCATION	dstLocation = {};
		dstLocation.pResource        = pDstTexture->GetD3D12Resource();
		dstLocation.SubresourceIndex = i;
		dstLocation.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;

		D3D12_TEXTURE_COPY_LOCATION	srcLocation = {};
		srcLocation.pResource        = pSrcTexture->GetD3D12Resource();
		srcLocation.SubresourceIndex = i;
		srcLocation.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;

		m_d3d12CommandList2->CopyTextureRegion(&dstLocation, 0, 0, 0, &srcLocation, nullptr);
	}
}

void Dx12CommandContext::Impl::ResolveSubresource(Dx12Resource* pDstResource, Dx12Resource* pSrcResource, u32 dstSubresource, u32 srcSubresource)
{
	if (pDstResource && pSrcResource)
	{
		TransitionBarrier(pDstResource, D3D12_RESOURCE_STATE_RESOLVE_DEST, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, false);
		TransitionBarrier(pSrcResource, D3D12_RESOURCE_STATE_RESOLVE_SOURCE, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, false);

		FlushResourceBarriers();

		m_d3d12CommandList2->ResolveSubresource(pDstResource->GetD3D12Resource(), dstSubresource,
			pSrcResource->GetD3D12Resource(), srcSubresource, pDstResource->Desc().Format);
	}
}

void Dx12CommandContext::Impl::SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY primitiveTopology)
{
	assert(m_pGraphicsPipeline);

	if (m_PrimitiveTopology != primitiveTopology)
	{
		m_PrimitiveTopology = primitiveTopology;
		m_d3d12CommandList2->IASetPrimitiveTopology(m_PrimitiveTopology);
	}
}

void Dx12CommandContext::Impl::ClearTexture(const Arc< Dx12Texture >& pTexture)
{
	assert(pTexture);
	float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	if (auto pClearValue = pTexture->GetClearValue())
	{
		memcpy(clearColor, pClearValue->Color, sizeof(clearColor));
	}

	TransitionBarrier(pTexture.get(), D3D12_RESOURCE_STATE_RENDER_TARGET);
	m_d3d12CommandList2->ClearRenderTargetView(pTexture->GetRenderTargetView(), clearColor, 0, nullptr);
}

void Dx12CommandContext::Impl::ClearDepthStencilTexture(const Arc< Dx12Texture >& pTexture, D3D12_CLEAR_FLAGS clearFlags)
{
	assert(pTexture);
	float clearDepth   = 1.0f;
	u8    clearStencil = 0;
	if (auto pClearValue = pTexture->GetClearValue())
	{
		clearDepth   = pTexture->GetClearValue()->DepthStencil.Depth;
		clearStencil = pTexture->GetClearValue()->DepthStencil.Stencil;
	}

	TransitionBarrier(pTexture.get(), D3D12_RESOURCE_STATE_DEPTH_WRITE);
	m_d3d12CommandList2->ClearDepthStencilView(pTexture->GetDepthStencilView(), clearFlags, clearDepth, clearStencil, 0, nullptr);
}

void Dx12CommandContext::Impl::SetViewport(const D3D12_VIEWPORT& viewport)
{
	SetViewports({ viewport });
}

void Dx12CommandContext::Impl::SetViewports(const std::vector< D3D12_VIEWPORT >& viewports)
{
	assert(viewports.size() < D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE);
	m_d3d12CommandList2->RSSetViewports(static_cast<u32>(viewports.size()), viewports.data());
}

void Dx12CommandContext::Impl::SetScissorRect(const D3D12_RECT& scissorRect)
{
	SetScissorRects({ scissorRect });
}

void Dx12CommandContext::Impl::SetScissorRects(const std::vector< D3D12_RECT >& scissorRects)
{
	assert(scissorRects.size() < D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE);
	m_d3d12CommandList2->RSSetScissorRects(static_cast<u32>(scissorRects.size()), scissorRects.data());
}

void Dx12CommandContext::Impl::SetRenderPipeline(Dx12GraphicsPipeline* pGraphicsPipeline)
{
	m_pComputePipeline = nullptr;
	if (m_pGraphicsPipeline != pGraphicsPipeline)
	{
		m_pGraphicsPipeline = pGraphicsPipeline;
		SetGraphicsRootSignature(m_pGraphicsPipeline->GetRootSignature());

		m_d3d12CommandList2->SetPipelineState(m_pGraphicsPipeline->GetD3D12PipelineState());
	}
}

void Dx12CommandContext::Impl::SetRenderPipeline(Dx12ComputePipeline* pComputePipeline)
{
	m_pGraphicsPipeline = nullptr;
	if (m_pComputePipeline != pComputePipeline)
	{
		m_pComputePipeline = pComputePipeline;
		SetComputeRootSignature(m_pComputePipeline->GetRootSignature());

		m_d3d12CommandList2->SetPipelineState(m_pComputePipeline->GetD3D12PipelineState());
	}
}

void Dx12CommandContext::Impl::SetGraphicsRootSignature(Arc< Dx12RootSignature > pRootSignature)
{
	assert(pRootSignature);

	if (m_pRootSignature != pRootSignature)
	{
		m_pRootSignature = pRootSignature;

		auto d3d12RootSignature = m_pRootSignature->GetD3D12RootSignature();
		for (u32 i = 0; i < NUM_RESOURCE_DESCRIPTOR_TYPE; ++i)
		{
			m_pDescriptorHeaps[i]->ParseRootSignature(m_pRootSignature);
		}

		m_d3d12CommandList2->SetGraphicsRootSignature(d3d12RootSignature);
	}
}

void Dx12CommandContext::Impl::SetComputeRootSignature(Arc< Dx12RootSignature > pRootSignature)
{
	assert(pRootSignature);

	if (m_pRootSignature != pRootSignature)
	{
		m_pRootSignature = pRootSignature;

		auto d3d12RootSignature = m_pRootSignature->GetD3D12RootSignature();
		for (u32 i = 0; i < NUM_RESOURCE_DESCRIPTOR_TYPE; ++i)
		{
			m_pDescriptorHeaps[i]->ParseRootSignature(m_pRootSignature);
		}

		m_d3d12CommandList2->SetComputeRootSignature(d3d12RootSignature);
	}
}

void Dx12CommandContext::Impl::SetDescriptorHeaps(const std::vector< ID3D12DescriptorHeap* >& d3d12DescriptorHeaps)
{
	m_d3d12CommandList2->SetDescriptorHeaps(static_cast<u32>(d3d12DescriptorHeaps.size()), d3d12DescriptorHeaps.data());
}

void Dx12CommandContext::Impl::SetRenderTarget(u32 numRenderTargets, D3D12_CPU_DESCRIPTOR_HANDLE rtv, D3D12_CPU_DESCRIPTOR_HANDLE dsv)
{
	if (dsv.ptr == 0)
	{
		m_d3d12CommandList2->OMSetRenderTargets(numRenderTargets, &rtv, FALSE, nullptr);
	}
	else
	{
		m_d3d12CommandList2->OMSetRenderTargets(numRenderTargets, &rtv, FALSE, &dsv);
	}
}

void Dx12CommandContext::Impl::BeginRenderPass(Arc< Dx12RenderTarget > pRenderTarget)
{
	using namespace render;
	std::vector< D3D12_CPU_DESCRIPTOR_HANDLE > d3d12RenderTargetDescriptors;
	d3d12RenderTargetDescriptors.reserve(eAttachmentPoint::NumAttachmentPoints);

	const auto& pTextures = pRenderTarget->GetAttachments();
	for (auto i = 0; i < eAttachmentPoint::DepthStencil; ++i)
	{
		auto rhiTexture = StaticCast<Dx12Texture>(pTextures[i]);
		if (rhiTexture)
		{
			TransitionBarrier(rhiTexture.get(), D3D12_RESOURCE_STATE_RENDER_TARGET);
			d3d12RenderTargetDescriptors.push_back(rhiTexture->GetRenderTargetView());
		}
	}

	CD3DX12_CPU_DESCRIPTOR_HANDLE depthStencilDescriptor(D3D12_DEFAULT);
	auto rhiDepthTexture = StaticCast<Dx12Texture>(pRenderTarget->Attachment(eAttachmentPoint::DepthStencil));
	if (rhiDepthTexture)
	{
		TransitionBarrier(rhiDepthTexture.get(), D3D12_RESOURCE_STATE_DEPTH_WRITE);
		depthStencilDescriptor = rhiDepthTexture->GetDepthStencilView();
	}

	D3D12_CPU_DESCRIPTOR_HANDLE* d3d12DSV = depthStencilDescriptor.ptr != 0 ? &depthStencilDescriptor : nullptr;

	SetViewport(pRenderTarget->GetViewport());
	SetScissorRect(pRenderTarget->GetScissorRect());
	m_d3d12CommandList2->OMSetRenderTargets(
		static_cast<u32>(d3d12RenderTargetDescriptors.size()),
		d3d12RenderTargetDescriptors.data(), 
		FALSE, 
		d3d12DSV
	);
}

void Dx12CommandContext::Impl::SetGraphicsRootConstant(u32 srcValue, u32 dstOffset)
{
	assert(IsGraphicsContext());
	
	auto rootIndex = m_pGraphicsPipeline->GetConstantRootIndex();
	m_d3d12CommandList2->SetGraphicsRoot32BitConstant(rootIndex, srcValue, dstOffset);
}

void Dx12CommandContext::Impl::SetGraphicsRootConstants(u32 srcSizeInBytes, const void* pSrcData, u32 dstOffsetInBytes)
{
	assert(IsGraphicsContext());
	u32 size      = srcSizeInBytes / 4;
	u32 dstOffset = dstOffsetInBytes / 4;

	auto rootIndex = m_pGraphicsPipeline->GetConstantRootIndex();
	m_d3d12CommandList2->SetGraphicsRoot32BitConstants(rootIndex, size, pSrcData, dstOffset);
}

void Dx12CommandContext::Impl::SetComputeRootConstant(u32 srcValue, u32 dstOffset)
{
	assert(IsComputeContext());

	auto rootIndex = m_pComputePipeline->GetConstantRootIndex();
	m_d3d12CommandList2->SetComputeRoot32BitConstant(rootIndex, srcValue, dstOffset);
}

void Dx12CommandContext::Impl::SetComputeRootConstants(u32 srcSizeInBytes, const void* pSrcData, u32 dstOffsetInBytes)
{
	assert(IsComputeContext());
	u32 size      = srcSizeInBytes / 4;
	u32 dstOffset = dstOffsetInBytes / 4;

	auto rootIndex = m_pComputePipeline->GetConstantRootIndex();
	m_d3d12CommandList2->SetComputeRoot32BitConstants(rootIndex, size, pSrcData, dstOffset);
}

void Dx12CommandContext::Impl::SetGraphicsDynamicConstantBuffer(const std::string& name, size_t sizeInBytes, const void* pData)
{
	assert(IsGraphicsContext());

	auto allocation = m_pDynamicBufferAllocator->Allocate(sizeInBytes, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
	memcpy(allocation.CPUHandle, pData, sizeInBytes);

	auto [_, rootIndex] = m_pGraphicsPipeline->GetResourceBindingIndex(name);
	if (rootIndex == INVALID_INDEX)
		return;

	m_d3d12CommandList2->SetGraphicsRootConstantBufferView(rootIndex, allocation.GPUHandle);
}

void Dx12CommandContext::Impl::SetComputeDynamicConstantBuffer(const std::string& name, size_t sizeInBytes, const void* pData)
{
	assert(IsComputeContext());

	auto allocation = m_pDynamicBufferAllocator->Allocate(sizeInBytes, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
	memcpy(allocation.CPUHandle, pData, sizeInBytes);

	auto [_, rootIndex] = m_pComputePipeline->GetResourceBindingIndex(name);
	if (rootIndex == INVALID_INDEX)
		return;

	m_d3d12CommandList2->SetComputeRootConstantBufferView(rootIndex, allocation.GPUHandle);
}

void Dx12CommandContext::Impl::SetGraphicsConstantBufferView(const std::string& name, D3D12_GPU_VIRTUAL_ADDRESS gpuHandle)
{
	assert(IsGraphicsContext());

	auto [_, rootIndex] = m_pComputePipeline->GetResourceBindingIndex(name);
	if (rootIndex == INVALID_INDEX)
		return;

	m_d3d12CommandList2->SetGraphicsRootConstantBufferView(rootIndex, gpuHandle);
}

void Dx12CommandContext::Impl::SetGraphicsShaderResourceView(const std::string& name, D3D12_GPU_VIRTUAL_ADDRESS gpuHandle)
{
	assert(IsGraphicsContext());

	auto [_, rootIndex] = m_pGraphicsPipeline->GetResourceBindingIndex(name);
	if (rootIndex == INVALID_INDEX)
		return;

	m_d3d12CommandList2->SetGraphicsRootShaderResourceView(rootIndex, gpuHandle);
}

void Dx12CommandContext::Impl::SetComputeConstantBufferView(const std::string& name, D3D12_GPU_VIRTUAL_ADDRESS gpuHandle)
{
	assert(IsComputeContext());

	auto [_, rootIndex] = m_pComputePipeline->GetResourceBindingIndex(name);
	if (rootIndex == INVALID_INDEX)
		return;

	m_d3d12CommandList2->SetComputeRootConstantBufferView(rootIndex, gpuHandle);
}

void Dx12CommandContext::Impl::SetComputeShaderResourceView(const std::string& name, D3D12_GPU_VIRTUAL_ADDRESS gpuHandle)
{
	assert(IsComputeContext());

	auto [_, rootIndex] = m_pComputePipeline->GetResourceBindingIndex(name);
	if (rootIndex == INVALID_INDEX)
		return;

	m_d3d12CommandList2->SetComputeRootShaderResourceView(rootIndex, gpuHandle);
}

void Dx12CommandContext::Impl::SetComputeUnorderedAccessView(const std::string& name, D3D12_GPU_VIRTUAL_ADDRESS gpuHandle)
{
	assert(IsComputeContext());

	auto [_, rootIndex] = m_pComputePipeline->GetResourceBindingIndex(name);
	if (rootIndex == INVALID_INDEX)
		return;

	m_d3d12CommandList2->SetComputeRootUnorderedAccessView(rootIndex, gpuHandle);
}

void Dx12CommandContext::Impl::StageDescriptor(
	const std::string& name, 
	Arc< Dx12StructuredBuffer > pBuffer, 
	D3D12_DESCRIPTOR_HEAP_TYPE heapType)
{
	StageDescriptor(name, pBuffer->GetShaderResourceView(), heapType);
}

void Dx12CommandContext::Impl::StageDescriptor(
	const std::string& name,
	Arc< Dx12Texture > pTexture,
	D3D12_DESCRIPTOR_HEAP_TYPE heapType)
{
	const auto& state = pTexture->GetCurrentState();
	bool bIsUAV = IsComputeContext() && state.GetSubresourceState() == D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

	if (bIsUAV)
	{
		StageDescriptor(name, pTexture->GetUnorderedAccessView(0), heapType);
	}
	else
	{
		StageDescriptor(name, pTexture->GetShaderResourceView(), heapType);
	}
}

void Dx12CommandContext::Impl::StageDescriptor(
	const std::string& name,
	D3D12_CPU_DESCRIPTOR_HANDLE srcHandle, 
	D3D12_DESCRIPTOR_HEAP_TYPE heapType)
{
	if (IsGraphicsContext())
	{
		auto [offset, rootIndex] = m_pGraphicsPipeline->GetResourceBindingIndex(name);
		if (rootIndex == INVALID_INDEX)
			return;

		m_pDescriptorHeaps[heapType]->StageDescriptor(rootIndex, 1, offset, srcHandle);
	}
	else if (IsComputeContext())
	{
		auto [offset, rootIndex] = m_pComputePipeline->GetResourceBindingIndex(name);
		if (rootIndex == INVALID_INDEX)
			return;

		m_pDescriptorHeaps[heapType]->StageDescriptor(rootIndex, 1, offset, srcHandle);
	}
	else
	{
		__debugbreak();
		assert(false && "No pipeline is set!");
	}
}

void Dx12CommandContext::Impl::StageDescriptors(
	std::vector< std::pair< std::string, D3D12_CPU_DESCRIPTOR_HANDLE > >&& srcHandles,
	D3D12_DESCRIPTOR_HEAP_TYPE heapType)
{
	for (const auto& [name, srcHandle] : srcHandles)
	{
		StageDescriptor(name, srcHandle, heapType);
	}
}

void Dx12CommandContext::Impl::StageDescriptors(u32 rootIndex, u32 offset, std::vector< D3D12_CPU_DESCRIPTOR_HANDLE >&& srcHandles, D3D12_DESCRIPTOR_HEAP_TYPE heapType)
{
	m_pDescriptorHeaps[heapType]->StageDescriptors(rootIndex, offset, std::move(srcHandles));
}

void Dx12CommandContext::Impl::Draw(u32 vertexCount, u32 instanceCount, u32 startVertex, u32 startInstance)
{
	FlushResourceBarriers();

	for (u32 i = 0; i < NUM_RESOURCE_DESCRIPTOR_TYPE; ++i)
	{
		m_pDescriptorHeaps[i]->CommitDescriptorsForDraw(m_d3d12CommandList2);
	}

	m_d3d12CommandList2->DrawInstanced(vertexCount, instanceCount, startVertex, startInstance);
}

void Dx12CommandContext::Impl::DrawIndexed(u32 indexCount, u32 instanceCount, u32 startIndex, u32 baseVertex, u32 startInstance)
{
	FlushResourceBarriers();

	for (u32 i = 0; i < NUM_RESOURCE_DESCRIPTOR_TYPE; ++i)
	{
		m_pDescriptorHeaps[i]->CommitDescriptorsForDraw(m_d3d12CommandList2);
	}

	m_d3d12CommandList2->DrawIndexedInstanced(indexCount, instanceCount, startIndex, baseVertex, startInstance);
}

void Dx12CommandContext::Impl::DrawScene(const Dx12SceneResource& sceneResource)
{
	auto pIDB = sceneResource.GetIndirectBuffer();
	TransitionBarrier(pIDB.get(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);

	FlushResourceBarriers();
	for (u32 i = 0; i < NUM_RESOURCE_DESCRIPTOR_TYPE; ++i)
	{
		m_pDescriptorHeaps[i]->CommitDescriptorsForDraw(m_d3d12CommandList2);
	}

	m_d3d12CommandList2->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	m_d3d12CommandList2->ExecuteIndirect(
		sceneResource.GetSceneD3D12CommandSignature(),
		sceneResource.NumMeshes(),
		pIDB->GetD3D12Resource(),
		0,
		nullptr,
		0
	);
}

void Dx12CommandContext::Impl::Dispatch(u32 numGroupsX, u32 numGroupsY, u32 numGroupsZ)
{
	FlushResourceBarriers();

	for (int i = 0; i < NUM_RESOURCE_DESCRIPTOR_TYPE; ++i)
	{
		m_pDescriptorHeaps[i]->CommitDescriptorsForDispatch(m_d3d12CommandList2);
	}

	m_d3d12CommandList2->Dispatch(numGroupsX, numGroupsY, numGroupsZ);
}

void Dx12CommandContext::Impl::AddBarrier(const D3D12_RESOURCE_BARRIER& barrier, bool bFlushImmediate)
{
	m_ResourceBarriers[m_NumBarriersToFlush++] = barrier;

	if (bFlushImmediate || m_NumBarriersToFlush == MAX_NUM_PENDING_BARRIERS)
	{
		FlushResourceBarriers();
	}
}

void Dx12CommandContext::Impl::FlushResourceBarriers()
{
	if (m_NumBarriersToFlush > 0)
	{
		m_d3d12CommandList2->ResourceBarrier(m_NumBarriersToFlush, m_ResourceBarriers);
		m_NumBarriersToFlush = 0;
	}
}

void Dx12CommandContext::Impl::BindDescriptorHeaps()
{
	u32 numDescriptorHeaps = 0;
	ID3D12DescriptorHeap* descriptorHeaps[NUM_RESOURCE_DESCRIPTOR_TYPE] = {};
	for (u32 i = 0; i < NUM_RESOURCE_DESCRIPTOR_TYPE; ++i)
	{
		ID3D12DescriptorHeap* descriptorHeap = m_pDescriptorHeaps[i]->GetD3D12DescriptorHeap();
		if (descriptorHeap)
		{
			descriptorHeaps[numDescriptorHeaps++] = descriptorHeap;
		}
	}

	m_d3d12CommandList2->SetDescriptorHeaps(numDescriptorHeaps, descriptorHeaps);
}


//-------------------------------------------------------------------------
// Command Context
//-------------------------------------------------------------------------
Dx12CommandContext::Dx12CommandContext(Dx12RenderDevice& rd, D3D12_COMMAND_LIST_TYPE type)
	: m_Impl(MakeBox< Impl >(rd, type)) 
{
}

void Dx12CommandContext::Open()
{
	m_Impl->Open();
}

void Dx12CommandContext::Close()
{
	m_Impl->Close();
}

void Dx12CommandContext::ClearTexture(const Arc< Dx12Texture >& pTexture)
{
	m_Impl->ClearTexture(pTexture);
}

void Dx12CommandContext::ClearDepthStencilTexture(const Arc< Dx12Texture >& pTexture, D3D12_CLEAR_FLAGS clearFlags)
{
	m_Impl->ClearDepthStencilTexture(pTexture, clearFlags);
}

void Dx12CommandContext::CopyBuffer(ID3D12Resource* d3d12DstBuffer, ID3D12Resource* d3d12SrcBuffer, SIZE_T sizeInBytes, SIZE_T dstOffsetInBytes)
{
	m_Impl->CopyBuffer(d3d12DstBuffer, d3d12SrcBuffer, sizeInBytes, dstOffsetInBytes);
}

void Dx12CommandContext::CopyBuffer(Arc< render::Buffer > pDstBuffer, Arc< render::Buffer > pSrcBuffer, u64 offsetInBytes)
{
	auto rhiBufferDst = StaticCast<Dx12Buffer>(pDstBuffer);
	auto rhiBufferSrc = StaticCast<Dx12Buffer>(pSrcBuffer);
	assert(rhiBufferDst && rhiBufferSrc);

	m_Impl->CopyBuffer(rhiBufferDst, rhiBufferSrc, rhiBufferSrc->SizeInBytes(), offsetInBytes);
}

void Dx12CommandContext::CopyTexture(Arc< render::Texture > pDstTexture, Arc< render::Texture > pSrcTexture, u64 offsetInBytes)
{
	UNUSED(offsetInBytes);

	auto rhiTextureDst = StaticCast<Dx12Texture>(pDstTexture);
	auto rhiTextureSrc = StaticCast<Dx12Texture>(pSrcTexture);
	assert(rhiTextureDst && rhiTextureSrc);

	m_Impl->CopyTexture(rhiTextureDst, rhiTextureSrc);
}

void Dx12CommandContext::ResolveSubresource(Dx12Resource* pDstResource, Dx12Resource* pSrcResource, u32 dstSubresource, u32 srcSubresource)
{
	m_Impl->ResolveSubresource(pDstResource, pSrcResource, dstSubresource, srcSubresource);
}

void Dx12CommandContext::TransitionBarrier(Arc< render::Texture > pTexture, render::eTextureLayout newState, u32 subresource, bool bFlushImmediate)
{
	auto rhiTexture = StaticCast<Dx12Texture>(pTexture);
	assert(rhiTexture);

	m_Impl->TransitionBarrier(rhiTexture.get(), DX12_RESOURCE_STATE(newState, IsComputeContext() ? render::eShaderStage::Compute : render::eShaderStage::AllGraphics), subresource, bFlushImmediate);
}

void Dx12CommandContext::TransitionBarrier(Dx12Resource* pResource, D3D12_RESOURCE_STATES stateAfter, u32 subresource, bool bFlushImmediate)
{
	m_Impl->TransitionBarrier(pResource, stateAfter, subresource, bFlushImmediate);
}

void Dx12CommandContext::UAVBarrier(Dx12Resource* pResource, bool bFlushImmediate)
{
	m_Impl->UAVBarrier(pResource, bFlushImmediate);
}

void Dx12CommandContext::AliasingBarrier(Dx12Resource* pResourceBefore, Dx12Resource* pResourceAfter, bool bFlushImmediate)
{
	m_Impl->AliasingBarrier(pResourceBefore, pResourceAfter, bFlushImmediate);
}

void Dx12CommandContext::SetRenderPipeline(render::ComputePipeline* pRenderPipeline)
{
	auto rhiRenderPipeline = static_cast<Dx12ComputePipeline*>(pRenderPipeline);
	assert(rhiRenderPipeline);

	m_Impl->SetRenderPipeline(rhiRenderPipeline);
}

void Dx12CommandContext::SetRenderPipeline(render::GraphicsPipeline* pRenderPipeline)
{
	auto rhiRenderPipeline = static_cast<Dx12GraphicsPipeline*>(pRenderPipeline);
	assert(rhiRenderPipeline);

	m_Impl->SetRenderPipeline(rhiRenderPipeline);
}

void Dx12CommandContext::SetComputeConstants(u32 sizeInBytes, const void* pData, u32 offsetInBytes)
{
	m_Impl->SetComputeRootConstants(sizeInBytes, pData, offsetInBytes);
}

void Dx12CommandContext::SetGraphicsConstants(u32 sizeInBytes, const void* pData, u32 offsetInBytes)
{
	m_Impl->SetGraphicsRootConstants(sizeInBytes, pData, offsetInBytes);
}

void Dx12CommandContext::SetComputeDynamicUniformBuffer(const std::string& name, u32 sizeInBytes, const void* pData)
{
	m_Impl->SetComputeDynamicConstantBuffer(name, sizeInBytes, pData);
}

void Dx12CommandContext::SetGraphicsDynamicUniformBuffer(const std::string& name, u32 sizeInBytes, const void* pData)
{
	m_Impl->SetGraphicsDynamicConstantBuffer(name, sizeInBytes, pData);
}

void Dx12CommandContext::SetComputeShaderResource(const std::string& name, Arc< render::Texture > pTexture, Arc< render::Sampler > pSamplerInCharge)
{
	auto rhiTexture = StaticCast<Dx12Texture>(pTexture);
	assert(rhiTexture);

	const auto& state = rhiTexture->GetCurrentState();
	bool bIsUAV = state.GetSubresourceState() == D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

	if (bIsUAV)
		m_Impl->SetComputeUnorderedAccessView(name, rhiTexture->GpuAddress());
	else
		m_Impl->SetComputeShaderResourceView(name, rhiTexture->GpuAddress());
}

void Dx12CommandContext::SetGraphicsShaderResource(const std::string& name, Arc< render::Texture > pTexture, Arc< render::Sampler > pSamplerInCharge)
{
	auto rhiTexture = StaticCast<Dx12Texture>(pTexture);
	assert(rhiTexture);

	m_Impl->SetGraphicsShaderResourceView(name, rhiTexture->GpuAddress());
}

void Dx12CommandContext::SetComputeShaderResource(const std::string& name, Arc< render::Buffer > pBuffer)
{
	auto rhiBuffer = StaticCast<Dx12Buffer>(pBuffer);
	assert(rhiBuffer);

	if (rhiBuffer->GetType() == eBufferType::Structured)
		m_Impl->SetComputeShaderResourceView(name, StaticCast<Dx12StructuredBuffer>(rhiBuffer)->GpuAddress());
	else
		m_Impl->SetComputeConstantBufferView(name, rhiBuffer->GpuAddress());
}

void Dx12CommandContext::SetGraphicsShaderResource(const std::string& name, Arc< render::Buffer > pBuffer)
{
	auto rhiBuffer = StaticCast<Dx12Buffer>(pBuffer);
	assert(rhiBuffer);

	if (rhiBuffer->GetType() == eBufferType::Structured)
		m_Impl->SetGraphicsShaderResourceView(name, StaticCast<Dx12StructuredBuffer>(rhiBuffer)->GpuAddress());
	else
		m_Impl->SetGraphicsConstantBufferView(name, rhiBuffer->GpuAddress());
}

void Dx12CommandContext::SetComputeConstantBufferView(const std::string& name, D3D12_GPU_VIRTUAL_ADDRESS srv)
{
	m_Impl->SetComputeConstantBufferView(name, srv);
}

void Dx12CommandContext::SetGraphicsConstantBufferView(const std::string& name, D3D12_GPU_VIRTUAL_ADDRESS srv)
{
	m_Impl->SetGraphicsConstantBufferView(name, srv);
}

void Dx12CommandContext::SetComputeShaderResourceView(const std::string& name, D3D12_GPU_VIRTUAL_ADDRESS srv)
{
	m_Impl->SetComputeShaderResourceView(name, srv);
}

void Dx12CommandContext::SetGraphicsShaderResourceView(const std::string& name, D3D12_GPU_VIRTUAL_ADDRESS srv)
{
	m_Impl->SetGraphicsShaderResourceView(name, srv);
}

void Dx12CommandContext::StageDescriptor(const std::string& name, Arc< render::Buffer > pBuffer, u32 offset)
{
	UNUSED(offset);

	auto rhiBuffer = StaticCast<Dx12StructuredBuffer>(pBuffer);
	assert(rhiBuffer);

	m_Impl->StageDescriptor(name, rhiBuffer);
}

void Dx12CommandContext::StageDescriptor(const std::string& name, Arc< render::Texture > pTexture, Arc< render::Sampler > pSamplerInCharge, u32 offset)
{
	UNUSED(offset);
	UNUSED(pSamplerInCharge);

	auto rhiTexture = StaticCast<Dx12Texture>(pTexture);
	assert(rhiTexture);

	m_Impl->StageDescriptor(name, rhiTexture);
}

void Dx12CommandContext::StageDescriptors(std::vector< std::pair< std::string, D3D12_CPU_DESCRIPTOR_HANDLE > >&& srcHandles, D3D12_DESCRIPTOR_HEAP_TYPE heapType)
{
	m_Impl->StageDescriptors(std::move(srcHandles), heapType);
}

void Dx12CommandContext::StageDescriptors(u32 rootIndex, u32 offset, std::vector< D3D12_CPU_DESCRIPTOR_HANDLE >&& srcHandles, D3D12_DESCRIPTOR_HEAP_TYPE heapType)
{
	m_Impl->StageDescriptors(rootIndex, offset, std::move(srcHandles), heapType);
}

void Dx12CommandContext::SetDescriptorHeaps(const std::vector<ID3D12DescriptorHeap*>& d3d12DescriptorHeaps)
{
	m_Impl->SetDescriptorHeaps(d3d12DescriptorHeaps);
}

void Dx12CommandContext::SetRenderTarget(u32 numRenderTargets, D3D12_CPU_DESCRIPTOR_HANDLE rtv, D3D12_CPU_DESCRIPTOR_HANDLE dsv)
{
	m_Impl->SetRenderTarget(numRenderTargets, rtv, dsv);
}

void Dx12CommandContext::BeginRenderPass(Arc< render::RenderTarget > pRenderTarget)
{
	auto rhiRenderTarget = StaticCast<Dx12RenderTarget>(pRenderTarget);
	assert(rhiRenderTarget);

	rhiRenderTarget->ClearTexture(*this, render::eAttachmentPoint::All);

	m_Impl->BeginRenderPass(rhiRenderTarget);
}

void Dx12CommandContext::Draw(u32 vertexCount, u32 instanceCount, u32 firstVertex, u32 firstInstance)
{
	m_Impl->Draw(vertexCount, instanceCount, firstVertex, firstInstance);
}

void Dx12CommandContext::DrawIndexed(u32 indexCount, u32 instanceCount, u32 firstIndex, i32 vertexOffset, u32 firstInstance)
{
	m_Impl->DrawIndexed(indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
}

void Dx12CommandContext::DrawScene(const render::SceneResource& sceneResource)
{
	const auto& rhiSceneResource = static_cast<const Dx12SceneResource&>(sceneResource);
	m_Impl->DrawScene(rhiSceneResource);
}

void Dx12CommandContext::Dispatch(u32 numGroupsX, u32 numGroupsY, u32 numGroupsZ)
{
	m_Impl->Dispatch(numGroupsX, numGroupsY, numGroupsZ);
}

bool Dx12CommandContext::IsComputeContext() const
{
	return m_Impl->IsComputeContext();
}

bool Dx12CommandContext::IsGraphicsContext() const
{
	return m_Impl->IsGraphicsContext();
}

D3D12_COMMAND_LIST_TYPE Dx12CommandContext::GetCommandListType() const
{
	return m_Impl->GetCommandListType();
}

ID3D12GraphicsCommandList2* Dx12CommandContext::GetD3D12CommandList() const
{
	return m_Impl->GetD3D12CommandList();
}

}