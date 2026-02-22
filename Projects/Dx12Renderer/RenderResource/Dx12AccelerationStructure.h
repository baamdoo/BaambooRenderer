#pragma once
#include "RenderCommon/RenderResources.h"

namespace dx12
{

//-------------------------------------------------------------------------
// BLAS
//-------------------------------------------------------------------------
class Dx12BottomLevelAS : public render::BottomLevelAccelerationStructure
{
using Super = render::BottomLevelAccelerationStructure;
public:
	static Arc< Dx12BottomLevelAS > Create(Dx12RenderDevice& rd, const char* name);

	Dx12BottomLevelAS(Dx12RenderDevice& rd, const char* name);
	virtual ~Dx12BottomLevelAS();

	virtual void Prepare() override;

	[[nodiscard]]
	virtual u64 GetGPUVirtualAddress() const override;

	[[nodiscard]]
	virtual bool IsBuilt() const override { return m_bBuilt; }

	[[nodiscard]]
	const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS& GetBuildInputs() const { return m_BuildInputs; }

	[[nodiscard]]
	ID3D12Resource* GetResultBuffer() const { return m_d3d12ResultBuffer; }
	[[nodiscard]]
	ID3D12Resource* GetScratchBuffer() const { return m_d3d12ScratchBuffer; }

	void MarkBuilt() { m_bBuilt = true; }

private:
	Dx12RenderDevice& m_RenderDevice;

	std::vector< D3D12_RAYTRACING_GEOMETRY_DESC > m_d3d12GeometryDescs;

	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS m_BuildInputs = {};

	ID3D12Resource* m_d3d12ResultBuffer  = nullptr;
	ID3D12Resource* m_d3d12ScratchBuffer = nullptr;

	bool m_bBuilt    = false;
	bool m_bPrepared = false;
};


//-------------------------------------------------------------------------
// TLAS
//-------------------------------------------------------------------------
class Dx12TopLevelAS : public render::TopLevelAccelerationStructure
{
using Super = render::TopLevelAccelerationStructure;
public:
	static Arc< Dx12TopLevelAS > Create(Dx12RenderDevice& rd, const char* name);

	Dx12TopLevelAS(Dx12RenderDevice& rd, const char* name);
	virtual ~Dx12TopLevelAS();

	virtual void Prepare() override;

	[[nodiscard]]
	virtual u64 GetGPUVirtualAddress() const override;

	[[nodiscard]]
	virtual bool IsBuilt() const override { return m_bBuilt; }

	[[nodiscard]]
	const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS& GetBuildInputs() const { return m_BuildInputs; }

	[[nodiscard]]
	ID3D12Resource* GetResultBuffer() const { return m_d3d12ResultBuffer; }
	[[nodiscard]]
	ID3D12Resource* GetScratchBuffer() const { return m_d3d12ScratchBuffer; }

	void MarkBuilt() { m_bBuilt = true; }

private:
	void AllocateOrResizeBuffer(ID3D12Resource*& pBuffer, u64 requiredSize,
		D3D12_RESOURCE_STATES initialState, D3D12_HEAP_TYPE heapType,
		D3D12_RESOURCE_FLAGS flags, const wchar_t* debugName);

private:
	Dx12RenderDevice& m_RenderDevice;

	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS m_BuildInputs = {};

	ID3D12Resource* m_d3d12ResultBuffer       = nullptr;
	ID3D12Resource* m_d3d12ScratchBuffer      = nullptr;
	ID3D12Resource* m_d3d12InstanceDescBuffer = nullptr;

	u64 m_ResultBufferSize  = 0;
	u64 m_ScratchBufferSize = 0;

	bool m_bBuilt    = false;
	bool m_bPrepared = false;
};

} // namespace dx12