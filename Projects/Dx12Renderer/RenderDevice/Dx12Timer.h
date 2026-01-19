#pragma once

namespace dx12
{

using namespace Microsoft::WRL;

class Dx12Timer
{
public:
	Dx12Timer() = default;
	~Dx12Timer() = default;

	void Init(ID3D12Device* d3d12Device, ID3D12CommandQueue* d3d12CommandQueue, UINT numQueries = 2);
	void Destroy();

	void Start(ID3D12GraphicsCommandList* d3d12CommandList);
	void End(ID3D12GraphicsCommandList* d3d12CommandList);

	double GetElapsedTime() const;

private:
	ComPtr< ID3D12QueryHeap > m_QueryHeap;
	ComPtr< ID3D12Resource >  m_ReadbackBuffer;

	UINT64 m_GpuFrequency = 0;
	UINT   m_NumQueries   = 0;
	u32    m_QueryCounter = 0;

	bool m_bFirstQuery = true;
};

}

