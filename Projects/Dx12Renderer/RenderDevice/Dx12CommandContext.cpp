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

namespace dx12
{
	
CommandContext::CommandContext(RenderDevice& device, D3D12_COMMAND_LIST_TYPE type)
	: m_RenderDevice(device)
	, m_Type(type)
{
	auto d3d12Device = m_RenderDevice.GetD3D12Device();

	ThrowIfFailed(d3d12Device->CreateCommandAllocator(m_Type, IID_PPV_ARGS(&m_d3d12CommandAllocator)));
	ThrowIfFailed(d3d12Device->CreateCommandList1(
		0, m_Type, D3D12_COMMAND_LIST_FLAG_NONE, IID_PPV_ARGS(&m_d3d12CommandList)));

	m_pDynamicBufferAllocator = new DynamicBufferAllocator(m_RenderDevice);
	
	for (u32 i = 0; i < NUM_RESOURCE_DESCRIPTOR_TYPE; ++i)
	{
		m_pDescriptorHeaps[i] = 
			new DescriptorHeap(m_RenderDevice, (D3D12_DESCRIPTOR_HEAP_TYPE)i, MAX_NUM_DESCRIPTOR_PER_POOL[i]);
	}
}

CommandContext::~CommandContext()
{
	for (u32 i = 0; i < NUM_RESOURCE_DESCRIPTOR_TYPE; ++i)
	{
		RELEASE(m_pDescriptorHeaps[i]);
	}
	RELEASE(m_pDynamicBufferAllocator);

	COM_RELEASE(m_d3d12CommandList);
	COM_RELEASE(m_d3d12CommandAllocator);
}

void CommandContext::Open()
{
	m_pGraphicsPipeline = nullptr;
	m_pComputePipeline = nullptr;
	m_pRootSignature = nullptr;
	m_PrimitiveTopology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;

	ThrowIfFailed(m_d3d12CommandAllocator->Reset());
	ThrowIfFailed(m_d3d12CommandList->Reset(m_d3d12CommandAllocator, nullptr));

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

void CommandContext::Close()
{
	FlushResourceBarriers();
	m_d3d12CommandList->Close();
}

SyncObject CommandContext::Execute()
{
	switch (m_Type)
	{
	case D3D12_COMMAND_LIST_TYPE_DIRECT:
		return { m_RenderDevice.GraphicsQueue().ExecuteCommandList(this), m_RenderDevice.GraphicsQueue() };
		break;
	case D3D12_COMMAND_LIST_TYPE_COMPUTE:
		return { m_RenderDevice.ComputeQueue().ExecuteCommandList(this), m_RenderDevice.ComputeQueue() };
		break;
	case D3D12_COMMAND_LIST_TYPE_COPY:
		return { m_RenderDevice.CopyQueue().ExecuteCommandList(this), m_RenderDevice.CopyQueue() };
		break;
	}

	assert(false && "Invalid command list execution!");
	return { 0, m_RenderDevice.GraphicsQueue() };
}

void CommandContext::TransitionBarrier(const Arc< Resource >& pResource, D3D12_RESOURCE_STATES stateAfter, u32 subresource, bool bFlushImmediate)
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

void CommandContext::UAVBarrier(const Arc< Resource >& pResource, bool bFlushImmediate)
{
	if (pResource)
	{
		AddBarrier(CD3DX12_RESOURCE_BARRIER::UAV(pResource->GetD3D12Resource()), bFlushImmediate);
	}
}

void CommandContext::AliasingBarrier(const Arc< Resource >& pResourceBefore, const Arc< Resource >& pResourceAfter, bool bFlushImmediate)
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

void CommandContext::CopyBuffer(const Arc< Buffer >& pDstBuffer, const Arc< Buffer >& pSrcBuffer, size_t sizeInBytes)
{
	CopyBuffer(pDstBuffer->GetD3D12Resource(), pSrcBuffer->GetD3D12Resource(), sizeInBytes);
}

void CommandContext::CopyBuffer(ID3D12Resource* d3d12DstBuffer, ID3D12Resource* d3d12SrcBuffer, SIZE_T sizeInBytes)
{
	m_d3d12CommandList->CopyBufferRegion(d3d12DstBuffer, 0, d3d12SrcBuffer, 0, sizeInBytes);
}

void CommandContext::CopyTexture(const Arc< Texture >& pDstTexture, const Arc< Texture >& pSrcTexture)
{
	TransitionBarrier(pDstTexture, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, false);
	TransitionBarrier(pSrcTexture, D3D12_RESOURCE_STATE_COPY_SOURCE);

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

		m_d3d12CommandList->CopyTextureRegion(&dstLocation, 0, 0, 0, &srcLocation, nullptr);
	}
}

void CommandContext::ResolveSubresource(const Arc< Resource >& pDstResource, const Arc< Resource >& pSrcResource, u32 dstSubresource, u32 srcSubresource)
{
	if (pDstResource && pSrcResource)
	{
		TransitionBarrier(pDstResource, D3D12_RESOURCE_STATE_RESOLVE_DEST, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, false);
		TransitionBarrier(pSrcResource, D3D12_RESOURCE_STATE_RESOLVE_SOURCE, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, false);

		FlushResourceBarriers();

		m_d3d12CommandList->ResolveSubresource(pDstResource->GetD3D12Resource(), dstSubresource,
			pSrcResource->GetD3D12Resource(), srcSubresource, pDstResource->Desc().Format);
	}
}

void CommandContext::SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY primitiveTopology)
{
	assert(m_pGraphicsPipeline);

	if (m_PrimitiveTopology != primitiveTopology)
	{
		m_PrimitiveTopology = primitiveTopology;
		m_d3d12CommandList->IASetPrimitiveTopology(m_PrimitiveTopology);
	}
}

void CommandContext::ClearTexture(const Arc< Texture >& pTexture)
{
	assert(pTexture);
	static constexpr float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

	TransitionBarrier(pTexture, D3D12_RESOURCE_STATE_RENDER_TARGET);
	m_d3d12CommandList->ClearRenderTargetView(pTexture->GetRenderTargetView(), pTexture->GetClearValue() ? pTexture->GetClearValue()->Color : clearColor, 0, nullptr);
}

void CommandContext::ClearDepthStencilTexture(const Arc< Texture >& pTexture, D3D12_CLEAR_FLAGS clearFlags)
{
	assert(pTexture);
	static constexpr float clearDepth = 1.0f;
	static constexpr u8 clearStencil = 0;

	TransitionBarrier(pTexture, D3D12_RESOURCE_STATE_DEPTH_WRITE);
	m_d3d12CommandList->ClearDepthStencilView(pTexture->GetDepthStencilView(), clearFlags, clearDepth, clearStencil, 0, nullptr);
}

void CommandContext::SetViewport(const D3D12_VIEWPORT& viewport)
{
	SetViewports({ viewport });
}

void CommandContext::SetViewports(const std::vector< D3D12_VIEWPORT >& viewports)
{
	assert(viewports.size() < D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE);
	m_d3d12CommandList->RSSetViewports(static_cast<u32>(viewports.size()), viewports.data());
}

void CommandContext::SetScissorRect(const D3D12_RECT& scissorRect)
{
	SetScissorRects({ scissorRect });
}

void CommandContext::SetScissorRects(const std::vector< D3D12_RECT >& scissorRects)
{
	assert(scissorRects.size() < D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE);
	m_d3d12CommandList->RSSetScissorRects(static_cast<u32>(scissorRects.size()), scissorRects.data());
}

void CommandContext::SetRenderPipeline(GraphicsPipeline* pGraphicsPipeline)
{
	if (m_pGraphicsPipeline != pGraphicsPipeline)
	{
		m_pGraphicsPipeline = pGraphicsPipeline;
		m_d3d12CommandList->SetPipelineState(m_pGraphicsPipeline->GetD3D12PipelineState());
	}
}

void CommandContext::SetRenderPipeline(ComputePipeline* pComputePipeline)
{
	if (m_pComputePipeline != pComputePipeline)
	{
		m_pComputePipeline = pComputePipeline;
		m_d3d12CommandList->SetPipelineState(m_pComputePipeline->GetD3D12PipelineState());
	}
}

void CommandContext::SetGraphicsRootSignature(RootSignature* pRootSignature)
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

		m_d3d12CommandList->SetGraphicsRootSignature(d3d12RootSignature);
	}
}

void CommandContext::SetComputeRootSignature(RootSignature* pRootSignature)
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

		m_d3d12CommandList->SetComputeRootSignature(d3d12RootSignature);
	}
}

void CommandContext::SetDescriptorHeaps(const std::vector< ID3D12DescriptorHeap* >& d3d12DescriptorHeaps)
{
	m_d3d12CommandList->SetDescriptorHeaps(static_cast<u32>(d3d12DescriptorHeaps.size()), d3d12DescriptorHeaps.data());
}

void CommandContext::SetRenderTarget(u32 numRenderTargets, D3D12_CPU_DESCRIPTOR_HANDLE rtv, D3D12_CPU_DESCRIPTOR_HANDLE dsv)
{
	if (dsv.ptr == 0)
	{
		m_d3d12CommandList->OMSetRenderTargets(numRenderTargets, &rtv, FALSE, nullptr);
	}
	else
	{
		m_d3d12CommandList->OMSetRenderTargets(numRenderTargets, &rtv, FALSE, &dsv);
	}
}

void CommandContext::SetRenderTarget(const RenderTarget& renderTarget)
{
	std::vector< D3D12_CPU_DESCRIPTOR_HANDLE > d3d12RenderTargetDescriptors;
	d3d12RenderTargetDescriptors.reserve(eAttachmentPoint::NumAttachmentPoints);

	const auto& pTextures = renderTarget.GetAttachments();
	for (auto i = 0; i < eAttachmentPoint::DepthStencil; ++i)
	{
		auto pTexture = pTextures[i];
		if (pTexture)
		{
			TransitionBarrier(pTexture, D3D12_RESOURCE_STATE_RENDER_TARGET);
			d3d12RenderTargetDescriptors.push_back(pTexture->GetRenderTargetView());
		}
	}

	CD3DX12_CPU_DESCRIPTOR_HANDLE depthStencilDescriptor(D3D12_DEFAULT);
	auto pDepthTexture = renderTarget.Attachment(eAttachmentPoint::DepthStencil);
	if (pDepthTexture)
	{
		TransitionBarrier(pDepthTexture, D3D12_RESOURCE_STATE_DEPTH_WRITE);
		depthStencilDescriptor = pDepthTexture->GetDepthStencilView();
	}

	D3D12_CPU_DESCRIPTOR_HANDLE* d3d12DSV = depthStencilDescriptor.ptr != 0 ? &depthStencilDescriptor : nullptr;

	SetViewport(renderTarget.GetViewport());
	SetScissorRect(renderTarget.GetScissorRect());
	m_d3d12CommandList->OMSetRenderTargets(static_cast<u32>(d3d12RenderTargetDescriptors.size()),
		d3d12RenderTargetDescriptors.data(), FALSE, d3d12DSV);
}

void CommandContext::SetGraphicsRootConstant(u32 rootIndex, u32 srcValue, u32 dstOffset)
{
	m_d3d12CommandList->SetGraphicsRoot32BitConstant(rootIndex, srcValue, dstOffset);
}

void CommandContext::SetGraphicsRootConstants(u32 rootIndex, u32 srcSizeInBytes, void* srcData, u32 dstOffsetInBytes)
{
	u32 size      = srcSizeInBytes / 4;
	u32 dstOffset = dstOffsetInBytes / 4;
	m_d3d12CommandList->SetGraphicsRoot32BitConstants(rootIndex, size, srcData, dstOffset);
}

void CommandContext::SetComputeRootConstant(u32 rootIndex, u32 srcValue, u32 dstOffset)
{
	m_d3d12CommandList->SetComputeRoot32BitConstant(rootIndex, srcValue, dstOffset);
}

void CommandContext::SetComputeRootConstants(u32 rootIndex, u32 srcSizeInBytes, const void* srcData, u32 dstOffsetInBytes)
{
	u32 size      = srcSizeInBytes / 4;
	u32 dstOffset = dstOffsetInBytes / 4;
	m_d3d12CommandList->SetComputeRoot32BitConstants(rootIndex, size, srcData, dstOffset);
}

void CommandContext::SetGraphicsDynamicConstantBuffer(u32 rootIndex, size_t sizeInBytes, const void* pData)
{
	auto allocation = m_pDynamicBufferAllocator->Allocate(sizeInBytes, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
	memcpy(allocation.CPUHandle, pData, sizeInBytes);

	m_d3d12CommandList->SetGraphicsRootConstantBufferView(rootIndex, allocation.GPUHandle);
}

void CommandContext::SetComputeDynamicConstantBuffer(u32 rootIndex, size_t sizeInBytes, const void* pData)
{
	auto allocation = m_pDynamicBufferAllocator->Allocate(sizeInBytes, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
	memcpy(allocation.CPUHandle, pData, sizeInBytes);

	m_d3d12CommandList->SetComputeRootConstantBufferView(rootIndex, allocation.GPUHandle);
}

void CommandContext::SetGraphicsConstantBufferView(u32 rootIndex, D3D12_GPU_VIRTUAL_ADDRESS gpuHandle)
{
	m_d3d12CommandList->SetGraphicsRootConstantBufferView(rootIndex, gpuHandle);
}

void CommandContext::SetGraphicsShaderResourceView(u32 rootIndex, D3D12_GPU_VIRTUAL_ADDRESS gpuHandle)
{
	m_d3d12CommandList->SetGraphicsRootShaderResourceView(rootIndex, gpuHandle);
}

void CommandContext::SetComputeConstantBufferView(u32 rootIndex, D3D12_GPU_VIRTUAL_ADDRESS gpuHandle)
{
	m_d3d12CommandList->SetComputeRootConstantBufferView(rootIndex, gpuHandle);
}

void CommandContext::SetComputeShaderResourceView(u32 rootIndex, D3D12_GPU_VIRTUAL_ADDRESS gpuHandle)
{
	m_d3d12CommandList->SetComputeRootShaderResourceView(rootIndex, gpuHandle);
}

void CommandContext::SetComputeUnorderedAccessView(u32 rootIndex, D3D12_GPU_VIRTUAL_ADDRESS gpuHandle)
{
	m_d3d12CommandList->SetComputeRootUnorderedAccessView(rootIndex, gpuHandle);
}

void CommandContext::StageDescriptors(
	u32 rootIndex, 
	u32 numDescriptors, 
	u32 offset, 
	D3D12_CPU_DESCRIPTOR_HANDLE srcHandle, 
	D3D12_DESCRIPTOR_HEAP_TYPE heapType)
{
	m_pDescriptorHeaps[heapType]->StageDescriptors(rootIndex, numDescriptors, offset, srcHandle);
}

void CommandContext::StageDescriptors(
	u32 rootIndex, 
	u32 offset, 
	std::vector< D3D12_CPU_DESCRIPTOR_HANDLE >&& srcHandles, 
	D3D12_DESCRIPTOR_HEAP_TYPE heapType)
{
	m_pDescriptorHeaps[heapType]->StageDescriptors(rootIndex, offset, std::move(srcHandles));
}

void CommandContext::Draw(u32 vertexCount, u32 instanceCount, u32 startVertex, u32 startInstance)
{
	FlushResourceBarriers();

	for (u32 i = 0; i < NUM_RESOURCE_DESCRIPTOR_TYPE; ++i)
	{
		m_pDescriptorHeaps[i]->CommitDescriptorsForDraw(*this);
	}

	m_d3d12CommandList->DrawInstanced(vertexCount, instanceCount, startVertex, startInstance);
}

void CommandContext::DrawIndexed(u32 indexCount, u32 instanceCount, u32 startIndex, u32 baseVertex, u32 startInstance)
{
	FlushResourceBarriers();

	for (u32 i = 0; i < NUM_RESOURCE_DESCRIPTOR_TYPE; ++i)
	{
		m_pDescriptorHeaps[i]->CommitDescriptorsForDraw(*this);
	}

	m_d3d12CommandList->DrawIndexedInstanced(indexCount, instanceCount, startIndex, baseVertex, startInstance);
}

void CommandContext::DrawIndexedIndirect(const SceneResource& sceneResource)
{
	auto pIDB = sceneResource.GetIndirectBuffer();
	TransitionBarrier(pIDB, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);

	FlushResourceBarriers();

	for (u32 i = 0; i < NUM_RESOURCE_DESCRIPTOR_TYPE; ++i)
	{
		m_pDescriptorHeaps[i]->CommitDescriptorsForDraw(*this);
	}

	m_d3d12CommandList->ExecuteIndirect(
		sceneResource.GetSceneD3D12CommandSignature(),
		sceneResource.NumMeshes(),
		pIDB->GetD3D12Resource(),
		0,
		nullptr,
		0);
}

void CommandContext::Dispatch(u32 numGroupsX, u32 numGroupsY, u32 numGroupsZ)
{
	FlushResourceBarriers();

	for (int i = 0; i < NUM_RESOURCE_DESCRIPTOR_TYPE; ++i)
	{
		m_pDescriptorHeaps[i]->CommitDescriptorsForDispatch(*this);
	}

	m_d3d12CommandList->Dispatch(numGroupsX, numGroupsY, numGroupsZ);
}

void CommandContext::AddBarrier(const D3D12_RESOURCE_BARRIER& barrier, bool bFlushImmediate)
{
	m_ResourceBarriers[m_NumBarriersToFlush++] = barrier;

	if (bFlushImmediate || m_NumBarriersToFlush == MAX_NUM_PENDING_BARRIERS)
	{
		FlushResourceBarriers();
	}
}

void CommandContext::FlushResourceBarriers()
{
	if (m_NumBarriersToFlush > 0)
	{
		m_d3d12CommandList->ResourceBarrier(m_NumBarriersToFlush, m_ResourceBarriers);
		m_NumBarriersToFlush = 0;
	}
}

void CommandContext::BindDescriptorHeaps()
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

	m_d3d12CommandList->SetDescriptorHeaps(numDescriptorHeaps, descriptorHeaps);
}

}