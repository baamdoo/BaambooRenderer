#pragma once
#include "RenderCommon/RenderResources.h"

namespace dx12
{

class Dx12ConstantBuffer;

class Dx12ShaderBindingTable : public render::ShaderBindingTable
{
using Super = render::ShaderBindingTable;
public:
	static Arc< Dx12ShaderBindingTable > Create(Dx12RenderDevice& rd, const char* name);

	Dx12ShaderBindingTable(Dx12RenderDevice& rd, const char* name);
	~Dx12ShaderBindingTable() = default;

	virtual void Build() override;

	[[nodiscard]]
	const D3D12_DISPATCH_RAYS_DESC& GetDispatchRaysDesc(u32 width, u32 height, u32 depth = 1);

private:
	u64 ComputeRecordStride(u32 maxLocalDataSize) const;
	u32 GetMaxLocalDataSize(const std::vector< ShaderRecord >& records) const;
	void UploadShaderRecord(u8* dest, const ShaderRecord& record) const;

private:
	Dx12RenderDevice& m_RenderDevice;

	Arc< Dx12ConstantBuffer > m_pSBTBuffer;

	u64 m_RayGenSize   = 0;
	u64 m_RayGenStride = 0;

	u64 m_MissSize   = 0;
	u64 m_MissStride = 0;

	u64 m_HitGroupSize   = 0;
	u64 m_HitGroupStride = 0;

	D3D12_DISPATCH_RAYS_DESC m_DispatchDesc = {};
};

} // namespace dx12