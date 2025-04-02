#include "RendererPch.h"
#include "Dx12Buffer.h"
#include "RenderDevice/Dx12ResourceManager.h"

#include <BaambooUtils/Math.hpp>

namespace dx12
{

Buffer::Buffer(RenderContext& context, std::wstring_view name)
	: Super(context, name)
{
}

Buffer::Buffer(RenderContext& context, std::wstring_view name, const CreationInfo& info, u32 count, u32 elementSize, eResourceType type)
	: Super(context, name, info, type)
	, m_Count(count)
	, m_ElementSize(elementSize)
{
}

VertexBuffer::VertexBuffer(RenderContext& context, std::wstring_view name, const ResourceCreationInfo& info, u32 count, u32 elementSize)
	: Super(context, name, info, count, elementSize, eResourceType::VertexBuffer)
{
	m_d3d12BufferView.BufferLocation = m_d3d12Resource->GetGPUVirtualAddress();
	m_d3d12BufferView.StrideInBytes = elementSize;
	m_d3d12BufferView.SizeInBytes = static_cast<u32>(GetSizeInBytes());
}

IndexBuffer::IndexBuffer(RenderContext& context, std::wstring_view name, const ResourceCreationInfo& info, u32 count, u32 elementSize)
	: Super(context, name, info, count, elementSize, eResourceType::IndexBuffer)
{
	m_d3d12BufferView.BufferLocation = m_d3d12Resource->GetGPUVirtualAddress();
	m_d3d12BufferView.Format = DXGI_FORMAT_R16_UINT;
	m_d3d12BufferView.SizeInBytes = static_cast<u32>(GetSizeInBytes());
}

ConstantBuffer::ConstantBuffer(RenderContext& context, std::wstring_view name, const ResourceCreationInfo& info, u32 count, u32 elementSize)
	: Super(context, name, info, count, elementSize, eResourceType::ConstantBuffer)
{
	auto d3d12Device = m_RenderContext.GetD3D12Device();
	auto& rm = m_RenderContext.GetResourceManager();
	m_CBVAllocation = rm.AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, m_Count);

	D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
	cbvDesc.BufferLocation = m_d3d12Resource->GetGPUVirtualAddress();
	cbvDesc.SizeInBytes = static_cast<u32>(baamboo::math::AlignUp(GetSizeInBytes(), (u32)D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT));

	d3d12Device->CreateConstantBufferView(&cbvDesc, m_CBVAllocation.GetCPUHandle());

	D3D12_HEAP_PROPERTIES heapProps = {};
	ThrowIfFailed(m_d3d12Resource->GetHeapProperties(&heapProps, nullptr));

	if (heapProps.Type == D3D12_HEAP_TYPE_UPLOAD)
	{
		CD3DX12_RANGE writeRange(0, 0);
		m_d3d12Resource->Map(0, &writeRange, reinterpret_cast<void**>(&m_pSystemMemory));
	}
}

ConstantBuffer::~ConstantBuffer()
{
	m_CBVAllocation.Free();
}

StructuredBuffer::StructuredBuffer(RenderContext& context, std::wstring_view name, const ResourceCreationInfo& info, u32 count, u32 elementSize)
	: Super(context, name, info, count, elementSize, eResourceType::StructuredBuffer)
{
	auto d3d12Device = m_RenderContext.GetD3D12Device();
	auto& rm = m_RenderContext.GetResourceManager();

	// srv
	{
		m_SRVAllocation = rm.AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);

		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
		srvDesc.Buffer.FirstElement = 0;
		srvDesc.Buffer.NumElements = m_Count;
		srvDesc.Buffer.StructureByteStride = m_ElementSize;
		srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

		d3d12Device->CreateShaderResourceView(m_d3d12Resource, &srvDesc, m_SRVAllocation.GetCPUHandle());
	}

	// uav
	if (m_ResourceDesc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)
	{
		m_UAVAllocation = rm.AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);

		D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
		uavDesc.Format = DXGI_FORMAT_UNKNOWN;
		uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
		uavDesc.Buffer.FirstElement = 0;
		uavDesc.Buffer.NumElements = m_Count;
		uavDesc.Buffer.StructureByteStride = m_ElementSize;
		uavDesc.Buffer.CounterOffsetInBytes = 0;
		uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

		d3d12Device->CreateUnorderedAccessView(m_d3d12Resource, nullptr, &uavDesc, m_UAVAllocation.GetCPUHandle());
	}

	D3D12_HEAP_PROPERTIES heapProps = {};
	ThrowIfFailed(m_d3d12Resource->GetHeapProperties(&heapProps, nullptr));
	if (heapProps.Type == D3D12_HEAP_TYPE_UPLOAD)
	{
		CD3DX12_RANGE writeRange(0, 0);
		m_d3d12Resource->Map(0, &writeRange, reinterpret_cast<void**>(&m_pSystemMemory));
	}
}

StructuredBuffer::~StructuredBuffer()
{
	m_SRVAllocation.Free();
	m_UAVAllocation.Free();
}

}