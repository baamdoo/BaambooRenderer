#pragma once
#include <BaambooCore/ResourceHandle.h>

namespace D3D12MA
{
	class Allocator;
}

namespace dx12
{

class CommandList;
class CommandQueue;
class DescriptorPool;
class DescriptorAllocation;
class ResourceManager;
class SceneResource;
class Resource;
class Texture;

struct FrameData
{
	// data
	CameraData camera = {};

	// scene-resource
	SceneResource* pSceneResource = nullptr;

	// render-target
	Texture* pColor;
	Texture* pDepth;
};
inline FrameData g_FrameData = {};

class RenderContext
{
public:
	RenderContext(bool bEnableGBV = false);
	~RenderContext();

	void Flush();
	u32 Swap();

	[[nodiscard]]
	CommandList& AllocateCommandList(D3D12_COMMAND_LIST_TYPE type);

	void UpdateSubresources(Resource* pResource, u32 firstSubresource, u32 numSubresources, const D3D12_SUBRESOURCE_DATA* pSrcData);

	ID3D12Resource* CreateRHIResource(const D3D12_RESOURCE_DESC& desc, D3D12_RESOURCE_STATES initialState, D3D12_HEAP_PROPERTIES heapProperties, const D3D12_CLEAR_VALUE* pClearValue = nullptr);

	[[nodiscard]]
	inline ID3D12Device5* GetD3D12Device() const { return m_d3d12Device; }
	[[nodiscard]]
	CommandQueue& GetCommandQueue(D3D12_COMMAND_LIST_TYPE type = D3D12_COMMAND_LIST_TYPE_DIRECT);

	[[nodiscard]]
	inline D3D12MA::Allocator* dmaAllocator() const { return m_dmaAllocator; }
	[[nodiscard]]
	inline ResourceManager& GetResourceManager() { return *m_pResourceManager; }

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
	inline u8 ContextIndex() const { return m_ContextIndex; }

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

	CommandQueue* m_pGraphicsCommandQueue = nullptr;
	CommandQueue* m_pComputeCommandQueue = nullptr;
	CommandQueue* m_pCopyCommandQueue = nullptr;

	ResourceManager*    m_pResourceManager = nullptr;
	D3D12MA::Allocator* m_dmaAllocator;

	u32 m_SRVDescriptorSize = 0;
	u32 m_RTVDescriptorSize = 0;
	u32 m_DSVDescriptorSize = 0;
	D3D_ROOT_SIGNATURE_VERSION m_HighestRootSignatureVersion;

	u8  m_ContextIndex = 0;
	u32 m_WindowWidth = 0;
	u32 m_WindowHeight = 0;
};

}