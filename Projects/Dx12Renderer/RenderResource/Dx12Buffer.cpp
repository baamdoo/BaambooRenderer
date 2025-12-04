#include "RendererPch.h"
#include "Dx12Buffer.h"
#include "RenderDevice/Dx12ResourceManager.h"
#include "Utils/Math.hpp"

namespace dx12
{


//-------------------------------------------------------------------------
// Base Buffer
//-------------------------------------------------------------------------
Arc< Dx12Buffer > Dx12Buffer::Create(Dx12RenderDevice& rd, const char* name, CreationInfo&& desc)
{
	return MakeArc< Dx12Buffer >(rd, name, std::move(desc));
}

Arc< Dx12Buffer > Dx12Buffer::CreateEmpty(Dx12RenderDevice& rd, const char* name)
{
	return MakeArc< Dx12Buffer >(rd, name);
}

Dx12Buffer::Dx12Buffer(Dx12RenderDevice& rd, const char* name)
	: render::Buffer(name)
	, Dx12Resource(rd, name)
{
}

Dx12Buffer::Dx12Buffer(Dx12RenderDevice& rd, const char* name, CreationInfo&& info, eBufferType type)
	: render::Buffer(name, std::move(info))
	, Dx12Resource(rd, name, 
		{
			.desc         = CD3DX12_RESOURCE_DESC::Buffer(baamboo::math::AlignUp(m_CreationInfo.count * m_CreationInfo.elementSizeInBytes, (u64)D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT)),
			.heapProps    = m_CreationInfo.bMap ? D3D12_HEAP_TYPE_UPLOAD : D3D12_HEAP_TYPE_DEFAULT,
			.initialState = D3D12_RESOURCE_STATE_COMMON // Buffers are effectively created in state D3D12_RESOURCE_STATE_COMMON
		}, eResourceType::Buffer)
	, m_Count(m_CreationInfo.count)
	, m_Type(type)
	, m_ElementSize(baamboo::math::AlignUp(m_CreationInfo.elementSizeInBytes, (u64)D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT))
{
	if (m_CreationInfo.bMap)
	{
		CD3DX12_RANGE writeRange(0, 0);
		m_d3d12Resource->Map(0, &writeRange, reinterpret_cast<void**>(&m_pSystemMemory));
	}
}

void Dx12Buffer::Resize(u64 sizeInBytes, bool bReset)
{
	// TODO
	UNUSED(sizeInBytes);
	UNUSED(bReset);
}


//-------------------------------------------------------------------------
// Vertex Buffer
//------------------------------------------------------------------------- 
Arc< Dx12VertexBuffer > Dx12VertexBuffer::Create(Dx12RenderDevice& rd, const char* name, u32 numVertices)
{
	return MakeArc< Dx12VertexBuffer >(rd, name, numVertices);
}

Dx12VertexBuffer::Dx12VertexBuffer(Dx12RenderDevice& rd, const char* name, u32 numVertices)
	: Super(rd, name, 
		{
			.count              = numVertices,
			.elementSizeInBytes = sizeof(Vertex),
			.bMap               = false,
			.bufferUsage        = render::eBufferUsage_TransferDest | render::eBufferUsage_Vertex
		}, eBufferType::Vertex)
{
	m_d3d12BufferView.BufferLocation = m_d3d12Resource->GetGPUVirtualAddress();
	m_d3d12BufferView.StrideInBytes  = sizeof(Vertex);
	m_d3d12BufferView.SizeInBytes    = static_cast<u32>(SizeInBytes());
}


//-------------------------------------------------------------------------
// Index Buffer
//-------------------------------------------------------------------------
Arc< Dx12IndexBuffer > Dx12IndexBuffer::Create(Dx12RenderDevice& rd, const char* name, u32 numIndices)
{
	return MakeArc< Dx12IndexBuffer >(rd, name, numIndices);
}

Dx12IndexBuffer::Dx12IndexBuffer(Dx12RenderDevice& rd, const char* name, u32 numIndices)
	: Super(rd, name, 
		{
			.count              = numIndices,
			.elementSizeInBytes = 4,
			.bMap               = false,
			.bufferUsage        = render::eBufferUsage_TransferDest | render::eBufferUsage_Index
		}, eBufferType::Index)
{
	m_d3d12BufferView.BufferLocation = m_d3d12Resource->GetGPUVirtualAddress();
	m_d3d12BufferView.Format         = DXGI_FORMAT_R32_UINT;
	m_d3d12BufferView.SizeInBytes    = static_cast<u32>(SizeInBytes());
}


//-------------------------------------------------------------------------
// Constant Buffer
//-------------------------------------------------------------------------
Arc< Dx12ConstantBuffer > Dx12ConstantBuffer::Create(Dx12RenderDevice& rd, const char* name, u64 sizeInBytes, RenderFlags additionalUsage)
{
	return MakeArc< Dx12ConstantBuffer >(rd, name, sizeInBytes, additionalUsage);
}

Dx12ConstantBuffer::Dx12ConstantBuffer(Dx12RenderDevice& rd, const char* name, u64 sizeInBytes, RenderFlags additionalUsage)
	: Super(rd, name, 
		{
			.count              = 1,
			.elementSizeInBytes = sizeInBytes,
			.bMap               = true,
			.bufferUsage        = additionalUsage | render::eBufferUsage_Uniform
		}, eBufferType::Constant)
{
	auto d3d12Device = m_RenderDevice.GetD3D12Device();

	auto& rm = static_cast<Dx12ResourceManager&>(m_RenderDevice.GetResourceManager());
	m_CBVAllocation = rm.AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, m_Count);

	D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
	cbvDesc.BufferLocation = m_d3d12Resource->GetGPUVirtualAddress();
	cbvDesc.SizeInBytes    = static_cast<u32>(SizeInBytes());
	d3d12Device->CreateConstantBufferView(&cbvDesc, m_CBVAllocation.GetCPUHandle());
}

Dx12ConstantBuffer::~Dx12ConstantBuffer()
{
	m_CBVAllocation.Free();
}


//-------------------------------------------------------------------------
// Structured Buffer
//-------------------------------------------------------------------------
Arc< Dx12StructuredBuffer > Dx12StructuredBuffer::Create(Dx12RenderDevice& rd, const char* name, u64 sizeInBytes, RenderFlags additionalUsage)
{
	return MakeArc< Dx12StructuredBuffer >(rd, name, sizeInBytes, additionalUsage);
}

Dx12StructuredBuffer::Dx12StructuredBuffer(Dx12RenderDevice& rd, const char* name, u64 sizeInBytes, RenderFlags additionalUsage)
	: Super(rd, name, 
		{
			.count              = 1,
			.elementSizeInBytes = sizeInBytes,
			.bufferUsage        = additionalUsage
									| render::eBufferUsage_Storage
									| render::eBufferUsage_ShaderDeviceAddress
		}, eBufferType::Structured)
{
	auto  d3d12Device = m_RenderDevice.GetD3D12Device();
	auto& rm = static_cast<Dx12ResourceManager&>(m_RenderDevice.GetResourceManager());

	// srv
	{
		m_SRVAllocation = rm.AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);

		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Shader4ComponentMapping    = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Format                     = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension              = D3D12_SRV_DIMENSION_BUFFER;
		srvDesc.Buffer.FirstElement        = 0;
		srvDesc.Buffer.NumElements         = m_Count;
		srvDesc.Buffer.StructureByteStride = static_cast<u32>(m_ElementSize);
		srvDesc.Buffer.Flags               = D3D12_BUFFER_SRV_FLAG_NONE;

		d3d12Device->CreateShaderResourceView(m_d3d12Resource, &srvDesc, m_SRVAllocation.GetCPUHandle());
	}

	// uav
	if (m_ResourceDesc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)
	{
		m_UAVAllocation = rm.AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);

		D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
		uavDesc.Format                      = DXGI_FORMAT_UNKNOWN;
		uavDesc.ViewDimension               = D3D12_UAV_DIMENSION_BUFFER;
		uavDesc.Buffer.FirstElement         = 0;
		uavDesc.Buffer.NumElements          = m_Count;
		uavDesc.Buffer.StructureByteStride  = static_cast<u32>(m_ElementSize);
		uavDesc.Buffer.CounterOffsetInBytes = 0;
		uavDesc.Buffer.Flags                = D3D12_BUFFER_UAV_FLAG_NONE;

		d3d12Device->CreateUnorderedAccessView(m_d3d12Resource, nullptr, &uavDesc, m_UAVAllocation.GetCPUHandle());
	}
}

Dx12StructuredBuffer::~Dx12StructuredBuffer()
{
	m_SRVAllocation.Free();
	m_UAVAllocation.Free();
}

}