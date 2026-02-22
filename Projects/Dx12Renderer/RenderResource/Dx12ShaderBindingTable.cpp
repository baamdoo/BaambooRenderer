#include "RendererPch.h"
#include "Dx12ShaderBindingTable.h"
#include "Dx12Buffer.h"

#include <Utils/Math.hpp>

namespace dx12
{

namespace
{

constexpr u64 SBT_TABLE_ALIGNMENT  = D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT;  // 64
constexpr u64 SBT_RECORD_ALIGNMENT = D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT; // 32

}


Arc< Dx12ShaderBindingTable > Dx12ShaderBindingTable::Create(Dx12RenderDevice& rd, const char* name)
{
	return MakeArc< Dx12ShaderBindingTable >(rd, name);
}

Dx12ShaderBindingTable::Dx12ShaderBindingTable(Dx12RenderDevice & rd, const char* name)
	: Super(name)
	, m_RenderDevice(rd)
{
}

void Dx12ShaderBindingTable::Build()
{
	m_RayGenStride   = ComputeRecordStride(m_RayGenRecord.Size());
	m_MissStride     = m_MissRecords.size() > 0 ? ComputeRecordStride(GetMaxLocalDataSize(m_MissRecords)) : 0;
	m_HitGroupStride = m_HitGroupRecords.size() > 0 ? ComputeRecordStride(GetMaxLocalDataSize(m_HitGroupRecords)) : 0;
	
	m_RayGenSize   = baamboo::math::AlignUp(m_RayGenStride, SBT_TABLE_ALIGNMENT);
	m_MissSize     = baamboo::math::AlignUp(m_MissStride * static_cast<u32>(m_MissRecords.size()), SBT_TABLE_ALIGNMENT);
	m_HitGroupSize = baamboo::math::AlignUp(m_HitGroupStride * static_cast<u32>(m_HitGroupRecords.size()), SBT_TABLE_ALIGNMENT);

	u64 totalSBTSize = m_RayGenSize + m_MissSize + m_HitGroupSize;
	m_pSBTBuffer = Dx12ConstantBuffer::Create(
		m_RenderDevice,
		"SBTBuffer",
		totalSBTSize
	);

	m_pSBTBuffer->Reset();
	u8* pAddress = m_pSBTBuffer->GetSystemMemoryAddress();

	// Upload ray-generation section
	{
		UploadShaderRecord(pAddress, m_RayGenRecord);
		pAddress += m_RayGenSize;
	}
	// Upload miss section
	{
		for (const auto& record : m_MissRecords)
		{
			UploadShaderRecord(pAddress, record);
			pAddress += m_MissStride;
		}
		pAddress = m_pSBTBuffer->GetSystemMemoryAddress() + m_RayGenSize + m_MissSize;
	}
	// Upload hit-group section
	{
		for (const auto& record : m_HitGroupRecords)
		{
			UploadShaderRecord(pAddress, record);
			pAddress += m_HitGroupStride;
		}
	}
}

const D3D12_DISPATCH_RAYS_DESC& Dx12ShaderBindingTable::GetDispatchRaysDesc(u32 width, u32 height, u32 depth)
{
	D3D12_GPU_VIRTUAL_ADDRESS baseAddress = m_pSBTBuffer->GpuAddress();

	// RayGen (no stride - always exactly 1 record)
	m_DispatchDesc.RayGenerationShaderRecord.StartAddress = baseAddress;
	m_DispatchDesc.RayGenerationShaderRecord.SizeInBytes  = m_RayGenStride;

	// Miss
	m_DispatchDesc.MissShaderTable.StartAddress  = baseAddress + m_RayGenSize;
	m_DispatchDesc.MissShaderTable.SizeInBytes   = m_MissSize;
	m_DispatchDesc.MissShaderTable.StrideInBytes = m_MissStride;

	// HitGroup
	m_DispatchDesc.HitGroupTable.StartAddress  = baseAddress + m_RayGenSize + m_MissSize;
	m_DispatchDesc.HitGroupTable.SizeInBytes   = m_HitGroupSize;
	m_DispatchDesc.HitGroupTable.StrideInBytes = m_HitGroupStride;

	// Dispatch dimensions
	m_DispatchDesc.Width  = width;
	m_DispatchDesc.Height = height;
	m_DispatchDesc.Depth  = depth;

	return m_DispatchDesc;
}

u64 Dx12ShaderBindingTable::ComputeRecordStride(u32 maxLocalDataSize) const
{
	const u64 identifierSize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
	return baamboo::math::AlignUp(identifierSize + maxLocalDataSize, SBT_RECORD_ALIGNMENT);
}

u32 Dx12ShaderBindingTable::GetMaxLocalDataSize(const std::vector< ShaderRecord >& records) const
{
	u32 maxSize = 0;
	for (const auto& record : records)
		maxSize = std::max(maxSize, record.Size());
	return maxSize;
}

void Dx12ShaderBindingTable::UploadShaderRecord(u8* dest, const ShaderRecord& record) const
{
	if (!record.pIdentifier)
	{
		__debugbreak();
		return;
	}

	memcpy(dest, record.pIdentifier, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
	if (!record.rootArgs.empty())
	{
		memcpy(dest + D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES, record.rootArgs.data(), record.rootArgs.size());
	}
}

}