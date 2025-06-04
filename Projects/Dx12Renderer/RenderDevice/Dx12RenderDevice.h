#pragma once

namespace D3D12MA
{
	class Allocator;
}

namespace dx12
{

class CommandQueue;
class CommandContext;
class DescriptorPool;
class DescriptorAllocation;
class ResourceManager;
class Resource;

class RenderDevice
{
public:
	RenderDevice(bool bEnableGBV = false);
	~RenderDevice();

	void Flush();
	u32 Swap();

	void UpdateSubresources(Arc< Resource > pResource, u32 firstSubresource, u32 numSubresources, const D3D12_SUBRESOURCE_DATA* pSrcData);

	ID3D12Resource* CreateRHIResource(const D3D12_RESOURCE_DESC& desc, D3D12_RESOURCE_STATES initialState, D3D12_HEAP_PROPERTIES heapProperties, const D3D12_CLEAR_VALUE* pClearValue = nullptr);

	[[nodiscard]]
	inline ID3D12Device5* GetD3D12Device() const { return m_d3d12Device; }

	[[nodiscard]]
	CommandQueue& GraphicsQueue() const { return *m_pGraphicsCommandQueue; }
	[[nodiscard]]
	CommandQueue& ComputeQueue() const { return *m_pComputeCommandQueue; }
	[[nodiscard]]
	CommandQueue& CopyQueue() const { return *m_pCopyCommandQueue; }
	[[nodiscard]]
	CommandContext& BeginCommand(D3D12_COMMAND_LIST_TYPE commandType) const;

	[[nodiscard]]
	inline D3D12MA::Allocator* dmaAllocator() const { return m_dmaAllocator; }
	[[nodiscard]]
	inline ResourceManager& GetResourceManager() const { return *m_pResourceManager; }

	[[nodiscard]]
	inline u32 GetSRVDescriptorSize() const { return m_SRVDescriptorSize; }
	[[nodiscard]]
	inline u32 GetRTVDescriptorSize() const { return m_RTVDescriptorSize; }
	[[nodiscard]]
	inline u32 GetDSVDescriptorSize() const { return m_DSVDescriptorSize; }
	[[nodiscard]]
	inline u32 GetDescriptorSize(D3D12_DESCRIPTOR_HEAP_TYPE type) const { return m_d3d12Device->GetDescriptorHandleIncrementSize(type); }

	[[nodiscard]]
	DXGI_SAMPLE_DESC GetMultisampleQualityLevels(DXGI_FORMAT format, D3D12_MULTISAMPLE_QUALITY_LEVEL_FLAGS flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE) const;
	[[nodiscard]]
	D3D_ROOT_SIGNATURE_VERSION GetHighestRootSignatureVersion() const { return m_HighestRootSignatureVersion; }

	[[nodiscard]]
	inline u8 FrameIndex() const { return m_FrameIndex; }

	[[nodiscard]]
	u32 WindowWidth() const { return m_WindowWidth; }
	void SetWindowWidth(u32 width) { m_WindowWidth = width; }
	[[nodiscard]]
	u32 WindowHeight() const { return m_WindowHeight; }
	void SetWindowHeight(u32 height) { m_WindowHeight = height; }

private:
	void CreateDevice(bool bEnableGBV);

private:
	ID3D12Device5* m_d3d12Device = nullptr;

	CommandQueue* m_pGraphicsCommandQueue;
	CommandQueue* m_pComputeCommandQueue;
	CommandQueue* m_pCopyCommandQueue;

	ResourceManager*    m_pResourceManager;
	D3D12MA::Allocator* m_dmaAllocator;

	u32 m_SRVDescriptorSize = 0;
	u32 m_RTVDescriptorSize = 0;
	u32 m_DSVDescriptorSize = 0;
	D3D_ROOT_SIGNATURE_VERSION m_HighestRootSignatureVersion;

	u8  m_FrameIndex = 0;
	u32 m_WindowWidth = 0;
	u32 m_WindowHeight = 0;
};

}