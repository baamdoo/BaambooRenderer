#include "RendererPch.h"
#include "Dx12Buffer.h"
#include "RenderDevice/Dx12CommandContext.h"
#include "RenderDevice/Dx12ResourceManager.h"
#include "Utils/Math.hpp"

namespace dx12
{

namespace
{

	inline D3D12_RESOURCE_FLAGS ConvertToDx12BufferResourceFlags(RenderFlags usage, u8 mapDirection)
	{
		D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE;

		const bool bUAVCapableHeap = mapDirection == 0;
		if (bUAVCapableHeap &&
			((usage & render::eBufferUsage_Storage) ||
			 (usage & render::eBufferUsage_ShaderDeviceAddress)))
		{
			flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
		}

		return flags;
	}

	eBufferType GetBufferType(const render::Buffer::CreationInfo& info, eBufferType explicitType)
	{
		if (explicitType != eBufferType::None)
			return explicitType;

		if (info.format != render::eFormat::UNKNOWN)
			return eBufferType::Typed;

		if ((info.bufferUsage & render::eBufferUsage_ShaderDeviceAddress) != 0)
			return eBufferType::Raw;

		if ((info.bufferUsage & render::eBufferUsage_Storage) != 0 && info.elementSizeInBytes > 0)
			return eBufferType::Structured;

		return eBufferType::None;
	}

	D3D12_HEAP_TYPE HeapType(u8 mapDirection)
	{
		switch (mapDirection)
		{
		case 1 : return D3D12_HEAP_TYPE_UPLOAD;
		case 2 : return D3D12_HEAP_TYPE_READBACK;
		default: return D3D12_HEAP_TYPE_DEFAULT;
		}
	}

}


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
			.desc = CD3DX12_RESOURCE_DESC1::Buffer(
					baamboo::math::AlignUp(m_CreationInfo.count * m_CreationInfo.elementSizeInBytes, (u64)D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT),
					ConvertToDx12BufferResourceFlags(m_CreationInfo.bufferUsage, m_CreationInfo.mapDirection)
				),
			.heapProps     = HeapType(info.mapDirection),
			.initialLayout = D3D12_BARRIER_LAYOUT_COMMON // Buffers are effectively created in state D3D12_RESOURCE_STATE_COMMON
		}, eResourceType::Buffer)
	, m_Count(m_CreationInfo.count)
	, m_Type(GetBufferType(m_CreationInfo, type))
	, m_ElementSize(m_CreationInfo.elementSizeInBytes)
	, m_BufferSize(baamboo::math::AlignUp(m_CreationInfo.count * m_CreationInfo.elementSizeInBytes, (u64)D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT))
{
	if (m_CreationInfo.mapDirection > 0)
	{
		CD3DX12_RANGE writeRange(0, 0);
		m_d3d12Resource->Map(0, &writeRange, reinterpret_cast<void**>(&m_pSystemMemory));
	}

	if (type == eBufferType::None)
		CreateViews();
}

Dx12Buffer::~Dx12Buffer()
{
	ReleaseViews();

	if (m_pSystemMemory && m_d3d12Resource)
	{
		m_d3d12Resource->Unmap(0, nullptr);
		m_pSystemMemory = nullptr;
	}
}

void Dx12Buffer::Resize(u64 sizeInBytes, bool bReset)
{
	if (sizeInBytes <= m_BufferSize && !bReset)
		return;

	if (m_pSystemMemory && m_d3d12Resource)
	{
		m_d3d12Resource->Unmap(0, nullptr);
		m_pSystemMemory = nullptr;
	}
	ReleaseViews();

	ID3D12Resource2* d3d12OldResource = m_d3d12Resource;
	u64 oldSize = m_BufferSize;

	u64 alignedSize = baamboo::math::AlignUp(sizeInBytes, (u64)D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);

	auto d3d12Device = m_RenderDevice.GetD3D12Device();

	auto desc      = CD3DX12_RESOURCE_DESC1::Buffer(alignedSize, ConvertToDx12BufferResourceFlags(m_CreationInfo.bufferUsage, m_CreationInfo.mapDirection));
	auto heapProps = CD3DX12_HEAP_PROPERTIES(HeapType(m_CreationInfo.mapDirection));

	ThrowIfFailed(d3d12Device->CreateCommittedResource3(
		&heapProps, D3D12_HEAP_FLAG_NONE,
		&desc,
		D3D12_BARRIER_LAYOUT_UNDEFINED,
		nullptr,
		nullptr,
		0, nullptr,
		IID_PPV_ARGS(&m_d3d12Resource))
	);

	m_d3d12Resource->SetName(m_wName.data());
	m_ResourceDesc = m_d3d12Resource->GetDesc1();
	m_BufferSize   = alignedSize;

	// m_Count must be refreshed before creating views (see BugHistory.md for details)
	if (m_ElementSize > 0)
		m_Count = static_cast<u32>(m_BufferSize / m_ElementSize);

	m_CurrentState = ResourceState(D3D12_BARRIER_SYNC_NONE, D3D12_BARRIER_ACCESS_NO_ACCESS);

	if (m_CreationInfo.mapDirection > 0)
	{
		CD3DX12_RANGE writeRange(0, 0);
		m_d3d12Resource->Map(0, &writeRange, reinterpret_cast<void**>(&m_pSystemMemory));
	}
	CreateViews();

	if (d3d12OldResource)
	{
		if (!bReset && oldSize > 0)
		{
			auto pContext = m_RenderDevice.BeginCommand(D3D12_COMMAND_LIST_TYPE_COPY);
			{
				pContext->CopyBuffer(m_d3d12Resource, d3d12OldResource, std::min(oldSize, sizeInBytes), 0, 0);
				pContext->Close();
			}
			m_RenderDevice.ExecuteCommand(std::move(pContext)).Wait();
		}
		else
		{
			m_RenderDevice.Flush();
		}
	}

	COM_RELEASE(d3d12OldResource);
}


void Dx12Buffer::CreateViews()
{
	if (!m_d3d12Resource)
		return;

	auto  d3d12Device = m_RenderDevice.GetD3D12Device();
	auto& rm = static_cast<Dx12ResourceManager&>(m_RenderDevice.GetResourceManager());

	switch (m_Type)
	{
		
	// -----------------------------------------------------------------
	// Typed: Buffer<T>
	// -----------------------------------------------------------------
	case eBufferType::Typed:
	{
		DXGI_FORMAT dxgiFormat = DX12_FORMAT(m_CreationInfo.format);
			
		// SRV
		{
			m_SRVAllocation = rm.AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);

			D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
			srvDesc.Shader4ComponentMapping    = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			srvDesc.Format                     = dxgiFormat;
			srvDesc.ViewDimension              = D3D12_SRV_DIMENSION_BUFFER;
			srvDesc.Buffer.FirstElement        = 0;
			srvDesc.Buffer.NumElements         = m_Count;
			srvDesc.Buffer.StructureByteStride = 0;
			srvDesc.Buffer.Flags               = D3D12_BUFFER_SRV_FLAG_NONE;
			d3d12Device->CreateShaderResourceView(m_d3d12Resource, &srvDesc, m_SRVAllocation.GetCPUHandle());
		}

		// UAV
		if ((m_ResourceDesc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) != 0)
		{
			m_UAVAllocation = rm.AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);

			D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
			uavDesc.Format                      = dxgiFormat;
			uavDesc.ViewDimension               = D3D12_UAV_DIMENSION_BUFFER;
			uavDesc.Buffer.FirstElement         = 0;
			uavDesc.Buffer.NumElements          = m_Count;
			uavDesc.Buffer.StructureByteStride  = 0;
			uavDesc.Buffer.CounterOffsetInBytes = 0;
			uavDesc.Buffer.Flags                = D3D12_BUFFER_UAV_FLAG_NONE;
			d3d12Device->CreateUnorderedAccessView(m_d3d12Resource, nullptr, &uavDesc, m_UAVAllocation.GetCPUHandle());
		}
		break;
	}

	// -----------------------------------------------------------------
	// Structured: StructuredBuffer<T>
	// -----------------------------------------------------------------
	case eBufferType::Structured:
	{
		// SRV
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

		// UAV
		if ((m_ResourceDesc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) != 0)
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
		break;
	}

	// -----------------------------------------------------------------
	// Raw: ByteAddressBuffer
	// -----------------------------------------------------------------
	case eBufferType::Raw:
	{
		u32 numR32Elements = static_cast<u32>(m_BufferSize / 4);

		// SRV
		{
			m_SRVAllocation = rm.AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);

			D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
			srvDesc.Shader4ComponentMapping    = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			srvDesc.Format                     = DXGI_FORMAT_R32_TYPELESS;
			srvDesc.ViewDimension              = D3D12_SRV_DIMENSION_BUFFER;
			srvDesc.Buffer.FirstElement        = 0;
			srvDesc.Buffer.NumElements         = numR32Elements;
			srvDesc.Buffer.StructureByteStride = 0;
			srvDesc.Buffer.Flags               = D3D12_BUFFER_SRV_FLAG_RAW;
			d3d12Device->CreateShaderResourceView(m_d3d12Resource, &srvDesc, m_SRVAllocation.GetCPUHandle());
		}

		// UAV
		if ((m_ResourceDesc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) != 0)
		{
			m_UAVAllocation = rm.AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);

			D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
			uavDesc.Format                      = DXGI_FORMAT_R32_TYPELESS;
			uavDesc.ViewDimension               = D3D12_UAV_DIMENSION_BUFFER;
			uavDesc.Buffer.FirstElement         = 0;
			uavDesc.Buffer.NumElements          = numR32Elements;
			uavDesc.Buffer.StructureByteStride  = 0;
			uavDesc.Buffer.CounterOffsetInBytes = 0;
			uavDesc.Buffer.Flags                = D3D12_BUFFER_UAV_FLAG_RAW;
			d3d12Device->CreateUnorderedAccessView(m_d3d12Resource, nullptr, &uavDesc, m_UAVAllocation.GetCPUHandle());
		}
		break;
	}

	default:
		break;
	}
}

void Dx12Buffer::ReleaseViews()
{
	m_SRVAllocation.Free();
	m_UAVAllocation.Free();
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
			.mapDirection       = 0,
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
			.mapDirection       = 0,
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
			.mapDirection       = 1,
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

void Dx12ConstantBuffer::Reset()
{
	memset(MappedMemory(), 0, SizeInBytes());
}

void Dx12ConstantBuffer::Upload(const void* pData, u64 sizeInBytes, u64 offsetInBytes)
{
	if (pData && sizeInBytes > 0)
	{
		memcpy((u8*)MappedMemory() + offsetInBytes, pData, sizeInBytes);
	}
}


//-------------------------------------------------------------------------
// Structured Buffer
//-------------------------------------------------------------------------
Arc< Dx12StructuredBuffer > Dx12StructuredBuffer::Create(Dx12RenderDevice& rd, const char* name, u64 elementSizeInBytes, u32 numElements, RenderFlags additionalUsage)
{
	return MakeArc< Dx12StructuredBuffer >(rd, name, elementSizeInBytes, numElements, additionalUsage);
}

Dx12StructuredBuffer::Dx12StructuredBuffer(Dx12RenderDevice& rd, const char* name, u64 elementSizeInBytes, u32 numElements, RenderFlags additionalUsage)
	: Super(rd, name, 
		{
			.count              = numElements,
			.elementSizeInBytes = elementSizeInBytes,
			.bufferUsage        = additionalUsage
									| render::eBufferUsage_Storage
									| render::eBufferUsage_ShaderDeviceAddress
		}, eBufferType::Structured)
{
	CreateViews();
}

Dx12StructuredBuffer::~Dx12StructuredBuffer()
{
}

}
