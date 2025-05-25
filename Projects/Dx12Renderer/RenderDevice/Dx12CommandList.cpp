#include "RendererPch.h"
#include "Dx12CommandList.h"
#include "Dx12RenderContext.h"
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
	
CommandList::CommandList(RenderContext& context, D3D12_COMMAND_LIST_TYPE type)
	: m_RenderContext(context)
	, m_Type(type)
{
	auto d3d12Device = m_RenderContext.GetD3D12Device();

	ThrowIfFailed(d3d12Device->CreateCommandAllocator(m_Type, IID_PPV_ARGS(&m_d3d12CommandAllocator)));
	ThrowIfFailed(d3d12Device->CreateCommandList1(
		0, m_Type, D3D12_COMMAND_LIST_FLAG_NONE, IID_PPV_ARGS(&m_d3d12CommandList)));

	m_pDynamicBufferAllocator = new DynamicBufferAllocator(m_RenderContext);
	
	for (u32 i = 0; i < NUM_RESOURCE_DESCRIPTOR_TYPE; ++i)
	{
		m_pDescriptorHeaps[i] = 
			new DescriptorHeap(m_RenderContext, (D3D12_DESCRIPTOR_HEAP_TYPE)i, MAX_NUM_DESCRIPTOR_PER_POOL[i]);
	}
}

CommandList::~CommandList()
{
	for (u32 i = 0; i < NUM_RESOURCE_DESCRIPTOR_TYPE; ++i)
	{
		RELEASE(m_pDescriptorHeaps[i]);
	}
	RELEASE(m_pDynamicBufferAllocator);

	COM_RELEASE(m_d3d12CommandList);
	COM_RELEASE(m_d3d12CommandAllocator);
}

void CommandList::Open()
{
	m_CurrentContextIndex = m_RenderContext.ContextIndex();

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

void CommandList::Close()
{
	FlushResourceBarriers();
	m_d3d12CommandList->Close();
}

void CommandList::TransitionBarrier(Resource* pResource, D3D12_RESOURCE_STATES stateAfter, u32 subresource, bool bFlushImmediate)
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

void CommandList::UAVBarrier(Resource* pResource, bool bFlushImmediate)
{
	if (pResource)
	{
		AddBarrier(CD3DX12_RESOURCE_BARRIER::UAV(pResource->GetD3D12Resource()), bFlushImmediate);
	}
}

void CommandList::AliasingBarrier(Resource* pResourceBefore, Resource* pResourceAfter, bool bFlushImmediate)
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

void CommandList::CopyBuffer(Buffer* pDstBuffer, Buffer* pSrcBuffer, size_t sizeInBytes)
{
	CopyBuffer(pDstBuffer->GetD3D12Resource(), pSrcBuffer->GetD3D12Resource(), sizeInBytes);
}

void CommandList::CopyBuffer(ID3D12Resource* d3d12DstBuffer, ID3D12Resource* d3d12SrcBuffer, SIZE_T sizeInBytes)
{
	m_d3d12CommandList->CopyBufferRegion(d3d12DstBuffer, 0, d3d12SrcBuffer, 0, sizeInBytes);
}

void CommandList::CopyTexture(Texture* pDstTexture, Texture* pSrcTexture)
{
	TransitionBarrier(pDstTexture, D3D12_RESOURCE_STATE_COPY_DEST);
	TransitionBarrier(pSrcTexture, D3D12_RESOURCE_STATE_COPY_SOURCE);

	D3D12_RESOURCE_DESC Desc = pDstTexture->GetResourceDesc();
	for (u16 i = 0; i < Desc.MipLevels; i++)
	{
		D3D12_TEXTURE_COPY_LOCATION	dstLocation = {};
		dstLocation.pResource = pDstTexture->GetD3D12Resource();
		dstLocation.SubresourceIndex = i;
		dstLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;

		D3D12_TEXTURE_COPY_LOCATION	srcLocation = {};
		srcLocation.pResource = pSrcTexture->GetD3D12Resource();
		srcLocation.SubresourceIndex = i;
		srcLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;

		m_d3d12CommandList->CopyTextureRegion(&dstLocation, 0, 0, 0, &srcLocation, nullptr);
	}
}

void CommandList::ResolveSubresource(Resource* pDstResource, Resource* pSrcResource, u32 dstSubresource, u32 srcSubresource)
{
	if (pDstResource && pSrcResource)
	{
		TransitionBarrier(pDstResource, D3D12_RESOURCE_STATE_RESOLVE_DEST, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, false);
		TransitionBarrier(pSrcResource, D3D12_RESOURCE_STATE_RESOLVE_SOURCE, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, false);

		FlushResourceBarriers();

		m_d3d12CommandList->ResolveSubresource(pDstResource->GetD3D12Resource(), dstSubresource,
			pSrcResource->GetD3D12Resource(), srcSubresource, pDstResource->GetResourceDesc().Format);
	}
}

void CommandList::SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY primitiveTopology)
{
	if (m_PrimitiveTopology != primitiveTopology)
	{
		m_PrimitiveTopology = primitiveTopology;
		m_d3d12CommandList->IASetPrimitiveTopology(m_PrimitiveTopology);
	}
}

void CommandList::ClearTexture(Texture* pTexture)
{
	assert(pTexture);
	static constexpr float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

	TransitionBarrier(pTexture, D3D12_RESOURCE_STATE_RENDER_TARGET);
	m_d3d12CommandList->ClearRenderTargetView(pTexture->GetRenderTargetView(), pTexture->GetClearValue() ? pTexture->GetClearValue()->Color : clearColor, 0, nullptr);
}

void CommandList::ClearDepthStencilTexture(Texture* pTexture, D3D12_CLEAR_FLAGS clearFlags)
{
	assert(pTexture);
	static constexpr float clearDepth = 1.0f;
	static constexpr u8 clearStencil = 0;

	TransitionBarrier(pTexture, D3D12_RESOURCE_STATE_DEPTH_WRITE);
	m_d3d12CommandList->ClearDepthStencilView(pTexture->GetDepthStencilView(), clearFlags, clearDepth, clearStencil, 0, nullptr);
}

void CommandList::SetViewport(const D3D12_VIEWPORT& viewport)
{
	SetViewports({ viewport });
}

void CommandList::SetViewports(const std::vector< D3D12_VIEWPORT >& viewports)
{
	assert(viewports.size() < D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE);
	m_d3d12CommandList->RSSetViewports(static_cast<u32>(viewports.size()), viewports.data());
}

void CommandList::SetScissorRect(const D3D12_RECT& scissorRect)
{
	SetScissorRects({ scissorRect });
}

void CommandList::SetScissorRects(const std::vector< D3D12_RECT >& scissorRects)
{
	assert(scissorRects.size() < D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE);
	m_d3d12CommandList->RSSetScissorRects(static_cast<u32>(scissorRects.size()), scissorRects.data());
}

void CommandList::SetPipelineState(GraphicsPipeline* pGraphicsPipeline)
{
	if (m_pGraphicsPipeline != pGraphicsPipeline)
	{
		m_pGraphicsPipeline = pGraphicsPipeline;
		m_d3d12CommandList->SetPipelineState(m_pGraphicsPipeline->GetD3D12PipelineState());
	}
}

void CommandList::SetPipelineState(ComputePipeline* pComputePipeline)
{
	if (m_pComputePipeline != pComputePipeline)
	{
		m_pComputePipeline = pComputePipeline;
		m_d3d12CommandList->SetPipelineState(m_pComputePipeline->GetD3D12PipelineState());
	}
}

void CommandList::SetGraphicsRootSignature(RootSignature* pRootSignature)
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

void CommandList::SetComputeRootSignature(RootSignature* pRootSignature)
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

void CommandList::SetDescriptorHeaps(const std::vector< ID3D12DescriptorHeap* >& d3d12DescriptorHeaps)
{
	m_d3d12CommandList->SetDescriptorHeaps(static_cast<u32>(d3d12DescriptorHeaps.size()), d3d12DescriptorHeaps.data());
}

void CommandList::SetRenderTarget(const RenderTarget& renderTarget)
{
	std::vector< D3D12_CPU_DESCRIPTOR_HANDLE > d3d12RenderTargetDescriptors;
	d3d12RenderTargetDescriptors.reserve(eAttachmentPoint::NumAttachmentPoints);

	const auto& pTextures = renderTarget.GetTextures();
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

void CommandList::SetGraphics32BitConstant(u32 rootIndex, u32 srcValue, u32 dstOffset)
{
	m_d3d12CommandList->SetGraphicsRoot32BitConstant(rootIndex, srcValue, dstOffset);
}

void CommandList::SetGraphics32BitConstants(u32 rootIndex, u32 srcSizeInBytes, void* srcData, u32 dstOffsetInBytes)
{
	u32 size = srcSizeInBytes / 4;
	u32 dstOffset = dstOffsetInBytes / 4;
	m_d3d12CommandList->SetGraphicsRoot32BitConstants(rootIndex, size, srcData, dstOffset);
}

void CommandList::SetGraphicsDynamicConstantBuffer(u32 rootIndex, size_t sizeInBytes, const void* bufferData)
{
	auto allocation = m_pDynamicBufferAllocator->Allocate(sizeInBytes, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
	memcpy(allocation.CPUHandle, bufferData, sizeInBytes);
	
	m_d3d12CommandList->SetGraphicsRootConstantBufferView(rootIndex, allocation.GPUHandle);
}

void CommandList::StageDescriptors(
	u32 rootIndex, 
	u32 numDescriptors, 
	u32 offset, 
	D3D12_CPU_DESCRIPTOR_HANDLE srcHandle, 
	D3D12_DESCRIPTOR_HEAP_TYPE heapType)
{
	m_pDescriptorHeaps[heapType]->StageDescriptors(rootIndex, numDescriptors, offset, srcHandle);
}

void CommandList::SetVertexBuffer(u32 slot, VertexBuffer* pVertexBuffer)
{
	SetVertexBuffers(slot, { pVertexBuffer });
}

void CommandList::SetVertexBuffers(u32 startSlot, const std::vector< VertexBuffer* >& vertexBufferList)
{
	std::vector< D3D12_VERTEX_BUFFER_VIEW > d3d12Views;
	d3d12Views.reserve(vertexBufferList.size());

	for (auto pVertexBuffer : vertexBufferList)
	{
		if (pVertexBuffer)
		{
			TransitionBarrier(pVertexBuffer, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);

			d3d12Views.push_back(pVertexBuffer->GetBufferView());
		}
	}

	m_d3d12CommandList->IASetVertexBuffers(startSlot, static_cast<u32>(d3d12Views.size()), d3d12Views.data());
}

void CommandList::SetIndexBuffer(IndexBuffer* pIndexBuffer)
{
	if (pIndexBuffer)
	{
		TransitionBarrier(pIndexBuffer, D3D12_RESOURCE_STATE_INDEX_BUFFER);

		m_d3d12CommandList->IASetIndexBuffer(&pIndexBuffer->GetBufferView());
	}
}

void CommandList::Draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t startVertex, uint32_t startInstance)
{
	FlushResourceBarriers();

	for (u32 i = 0; i < NUM_RESOURCE_DESCRIPTOR_TYPE; ++i)
	{
		m_pDescriptorHeaps[i]->CommitDescriptorsForDraw(*this);
	}

	m_d3d12CommandList->DrawInstanced(vertexCount, instanceCount, startVertex, startInstance);
}

void CommandList::DrawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t startIndex, int32_t baseVertex, uint32_t startInstance)
{
	FlushResourceBarriers();

	for (u32 i = 0; i < NUM_RESOURCE_DESCRIPTOR_TYPE; ++i)
	{
		m_pDescriptorHeaps[i]->CommitDescriptorsForDraw(*this);
	}

	m_d3d12CommandList->DrawIndexedInstanced(indexCount, instanceCount, startIndex, baseVertex, startInstance);
}

void CommandList::DrawIndexedIndirect(const SceneResource& sceneResource)
{
	FlushResourceBarriers();

	for (u32 i = 0; i < NUM_RESOURCE_DESCRIPTOR_TYPE; ++i)
	{
		m_pDescriptorHeaps[i]->CommitDescriptorsForDraw(*this);
	}

	auto pIDB = sceneResource.GetIndirectBuffer();
	m_d3d12CommandList->ExecuteIndirect(
		sceneResource.GetSceneD3D12CommandSignature(),
		sceneResource.NumMeshes(),
		pIDB->GetD3D12Resource(),
		0,
		nullptr,
		0);
}

void CommandList::Dispatch(uint32_t numGroupsX, uint32_t numGroupsY, uint32_t numGroupsZ)
{
	FlushResourceBarriers();

	for (int i = 0; i < NUM_RESOURCE_DESCRIPTOR_TYPE; ++i)
	{
		m_pDescriptorHeaps[i]->CommitDescriptorsForDispatch(*this);
	}

	m_d3d12CommandList->Dispatch(numGroupsX, numGroupsY, numGroupsZ);
}

void CommandList::AddBarrier(const D3D12_RESOURCE_BARRIER& barrier, bool bFlushImmediate)
{
	m_ResourceBarriers[m_NumBarriersToFlush++] = barrier;

	if (bFlushImmediate || m_NumBarriersToFlush == MAX_NUM_PENDING_BARRIERS)
	{
		FlushResourceBarriers();
	}
}

void CommandList::FlushResourceBarriers()
{
	if (m_NumBarriersToFlush > 0)
	{
		m_d3d12CommandList->ResourceBarrier(m_NumBarriersToFlush, m_ResourceBarriers);
		m_NumBarriersToFlush = 0;
	}
}

void CommandList::BindDescriptorHeaps()
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