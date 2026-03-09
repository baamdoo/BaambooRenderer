#include "RendererPch.h"
#include "Dx12AccelerationStructure.h"

namespace dx12
{

using namespace render;

static D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS ConvertBuildFlags(RenderFlags flags)
{
	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS d3d12Flags =
		D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;

	if (flags & eASBuildFlag_AllowUpdate)
		d3d12Flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;
	if (flags & eASBuildFlag_AllowCompaction)
		d3d12Flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_COMPACTION;
	if (flags & eASBuildFlag_PreferFastTrace)
		d3d12Flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
	if (flags & eASBuildFlag_PreferFastBuild)
		d3d12Flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD;
	if (flags & eASBuildFlag_MinimizeMemory)
		d3d12Flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_MINIMIZE_MEMORY;

	return d3d12Flags;
}

static D3D12_RAYTRACING_GEOMETRY_FLAGS ConvertGeometryFlags(RenderFlags flags)
{
	D3D12_RAYTRACING_GEOMETRY_FLAGS d3d12Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_NONE;

	if (flags & eGeometryFlag_Opaque)
		d3d12Flags |= D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
	if (flags & eGeometryFlag_NoDuplicateAnyHit)
		d3d12Flags |= D3D12_RAYTRACING_GEOMETRY_FLAG_NO_DUPLICATE_ANYHIT_INVOCATION;

	return d3d12Flags;
}


//=========================================================================
// BLAS
//=========================================================================
Arc< Dx12BottomLevelAS > Dx12BottomLevelAS::Create(Dx12RenderDevice & rd, const char* name)
{
	return MakeArc< Dx12BottomLevelAS >(rd, name);
}

Dx12BottomLevelAS::Dx12BottomLevelAS(Dx12RenderDevice& rd, const char* name)
	: Super(name)
	, m_RenderDevice(rd)
{
}

Dx12BottomLevelAS::~Dx12BottomLevelAS()
{
	COM_RELEASE(m_d3d12ScratchBuffer);
	COM_RELEASE(m_d3d12ResultBuffer);
}

void Dx12BottomLevelAS::Prepare()
{
	assert(!m_Geometries.empty() && "At least one geometry must be added before Prepare()!");

	auto d3d12Device = m_RenderDevice.GetD3D12Device();

	m_d3d12GeometryDescs.clear();
	m_d3d12GeometryDescs.reserve(m_Geometries.size());
	for (const auto& geom : m_Geometries)
	{
		D3D12_RAYTRACING_GEOMETRY_DESC d3d12Geom = {};
		d3d12Geom.Type  = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
		d3d12Geom.Flags = ConvertGeometryFlags(geom.geometryFlags);

		auto& tri = d3d12Geom.Triangles;
		tri.VertexBuffer.StartAddress  = geom.vertexBufferAddress;
		tri.VertexBuffer.StrideInBytes = geom.vertexStride;
		tri.VertexCount                = geom.vertexCount;
		tri.VertexFormat               = DXGI_FORMAT_R32G32B32_FLOAT;

		if (geom.indexCount > 0)
		{
			tri.IndexBuffer = geom.indexBufferAddress;
			tri.IndexCount  = geom.indexCount;
			tri.IndexFormat = DXGI_FORMAT_R32_UINT;
		}
		else
		{
			tri.IndexBuffer = 0;
			tri.IndexCount  = 0;
			tri.IndexFormat = DXGI_FORMAT_UNKNOWN;
		}
		tri.Transform3x4 = geom.transformBufferAddress;

		m_d3d12GeometryDescs.push_back(d3d12Geom);
	}

	m_BuildInputs = {};
	m_BuildInputs.Type           = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
	m_BuildInputs.Flags          = ConvertBuildFlags(m_BuildFlags);
	m_BuildInputs.NumDescs       = static_cast<u32>(m_d3d12GeometryDescs.size());
	m_BuildInputs.DescsLayout    = D3D12_ELEMENTS_LAYOUT_ARRAY;
	m_BuildInputs.pGeometryDescs = m_d3d12GeometryDescs.data();

	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuildInfo = {};
	d3d12Device->GetRaytracingAccelerationStructurePrebuildInfo(&m_BuildInputs, &prebuildInfo);

	assert(prebuildInfo.ResultDataMaxSizeInBytes > 0);

	// Allocate result buffer
	{
		COM_RELEASE(m_d3d12ResultBuffer);

		auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
		auto desc = CD3DX12_RESOURCE_DESC::Buffer(
			prebuildInfo.ResultDataMaxSizeInBytes,
			D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

		ThrowIfFailed(d3d12Device->CreateCommittedResource(
			&heapProps, D3D12_HEAP_FLAG_NONE,
			&desc, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
			nullptr, IID_PPV_ARGS(&m_d3d12ResultBuffer)));

		auto wName = ConvertToWString(m_Name + "::Result");
		m_d3d12ResultBuffer->SetName(wName.c_str());
	}

	// Allocate scratch buffer
	{
		COM_RELEASE(m_d3d12ScratchBuffer);

		auto desc = CD3DX12_RESOURCE_DESC::Buffer(
			prebuildInfo.ScratchDataSizeInBytes,
			D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
		auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

		ThrowIfFailed(d3d12Device->CreateCommittedResource(
			&heapProps, D3D12_HEAP_FLAG_NONE,
			&desc, D3D12_RESOURCE_STATE_COMMON,
			nullptr, IID_PPV_ARGS(&m_d3d12ScratchBuffer)));

		auto wName = ConvertToWString(m_Name + "::Scratch");
		m_d3d12ScratchBuffer->SetName(wName.c_str());
	}

	m_bBuilt    = false;
	m_bPrepared = true;
}

u64 Dx12BottomLevelAS::GetGPUVirtualAddress() const
{
	return m_d3d12ResultBuffer ? m_d3d12ResultBuffer->GetGPUVirtualAddress() : 0;
}


//=========================================================================
// TLAS
//=========================================================================
Arc< Dx12TopLevelAS > Dx12TopLevelAS::Create(Dx12RenderDevice& rd, const char* name)
{
	return MakeArc< Dx12TopLevelAS >(rd, name);
}

Dx12TopLevelAS::Dx12TopLevelAS(Dx12RenderDevice& rd, const char* name)
	: Super(name)
	, m_RenderDevice(rd)
{
}

Dx12TopLevelAS::~Dx12TopLevelAS()
{
	COM_RELEASE(m_d3d12InstanceDescBuffer);
	COM_RELEASE(m_d3d12ScratchBuffer);
	COM_RELEASE(m_d3d12ResultBuffer);
}

void Dx12TopLevelAS::AllocateOrResizeBuffer(
	ID3D12Resource*& pBuffer, u64 requiredSize,
	D3D12_RESOURCE_STATES initialState, D3D12_HEAP_TYPE heapType,
	D3D12_RESOURCE_FLAGS flags, const wchar_t* debugName)
{
	if (pBuffer)
	{
		auto existingSize = pBuffer->GetDesc().Width;
		if (existingSize >= requiredSize)
			return;

		COM_RELEASE(pBuffer);
	}

	auto desc        = CD3DX12_RESOURCE_DESC::Buffer(requiredSize, flags);
	auto heapProps   = CD3DX12_HEAP_PROPERTIES(heapType);
	auto d3d12Device = m_RenderDevice.GetD3D12Device();
	ThrowIfFailed(d3d12Device->CreateCommittedResource(
		&heapProps, D3D12_HEAP_FLAG_NONE,
		&desc, initialState,
		nullptr, IID_PPV_ARGS(&pBuffer)));

	pBuffer->SetName(debugName);
}

void Dx12TopLevelAS::Prepare()
{
	auto d3d12Device = m_RenderDevice.GetD3D12Device();

	u32 numInstances = static_cast<u32>(m_Instances.size());
	if (numInstances > 0)
	{
		u64 instanceDescSize = sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * numInstances;

		auto wInstName = ConvertToWString(m_Name + "::InstanceDescs");
		AllocateOrResizeBuffer(
			m_d3d12InstanceDescBuffer, instanceDescSize,
			D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_HEAP_TYPE_UPLOAD,
			D3D12_RESOURCE_FLAG_NONE, wInstName.c_str());

		D3D12_RAYTRACING_INSTANCE_DESC* pMapped = nullptr;
		ThrowIfFailed(m_d3d12InstanceDescBuffer->Map(0, nullptr, reinterpret_cast<void**>(&pMapped)));

		for (u32 i = 0; i < numInstances; ++i)
		{
			const auto& inst = m_Instances[i];

			auto& d3d12Inst = pMapped[i];
			memset(&d3d12Inst, 0, sizeof(D3D12_RAYTRACING_INSTANCE_DESC));
			memcpy(d3d12Inst.Transform, inst.transform, sizeof(inst.transform));

			d3d12Inst.InstanceID                          = inst.instanceID;
			d3d12Inst.InstanceMask                        = inst.instanceMask;
			d3d12Inst.InstanceContributionToHitGroupIndex = inst.instanceContributionToHitGroupIndex;
			d3d12Inst.Flags                               = inst.flags;
			d3d12Inst.AccelerationStructure               = inst.pBLAS ? inst.pBLAS->GetGPUVirtualAddress() : 0;
		}

		m_d3d12InstanceDescBuffer->Unmap(0, nullptr);
	}

	// 式式 Fill build inputs 式式
	m_BuildInputs = {};
	m_BuildInputs.Type          = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
	m_BuildInputs.Flags         = ConvertBuildFlags(m_BuildFlags);
	m_BuildInputs.NumDescs      = numInstances;
	m_BuildInputs.DescsLayout   = D3D12_ELEMENTS_LAYOUT_ARRAY;
	m_BuildInputs.InstanceDescs = numInstances > 0
		? m_d3d12InstanceDescBuffer->GetGPUVirtualAddress()
		: 0;

	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuildInfo = {};
	d3d12Device->GetRaytracingAccelerationStructurePrebuildInfo(&m_BuildInputs, &prebuildInfo);

	// Allocate / resize result buffer
	{
		auto wResultName = ConvertToWString(m_Name + "::Result");
		AllocateOrResizeBuffer(
			m_d3d12ResultBuffer, prebuildInfo.ResultDataMaxSizeInBytes,
			D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, D3D12_HEAP_TYPE_DEFAULT,
			D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, wResultName.c_str());

		m_ResultBufferSize = prebuildInfo.ResultDataMaxSizeInBytes;
	}

	// Allocate / resize scratch buffer
	{
		auto wScratchName = ConvertToWString(m_Name + "::Scratch");
		AllocateOrResizeBuffer(
			m_d3d12ScratchBuffer, prebuildInfo.ScratchDataSizeInBytes,
			D3D12_RESOURCE_STATE_COMMON, D3D12_HEAP_TYPE_DEFAULT,
			D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, wScratchName.c_str());

		m_ScratchBufferSize = prebuildInfo.ScratchDataSizeInBytes;
	}

	m_bBuilt    = false;
	m_bPrepared = true;
}

u64 Dx12TopLevelAS::GetGPUVirtualAddress() const
{
	return m_d3d12ResultBuffer ? m_d3d12ResultBuffer->GetGPUVirtualAddress() : 0;
}


} // namespace dx12