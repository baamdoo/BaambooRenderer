#pragma once
#include "RenderCommon/RenderDevice.h"

namespace D3D12MA
{
	class Allocator;
}

namespace dx12
{

class Dx12CommandQueue;
class Dx12CommandContext;
class DescriptorPool;
class DescriptorAllocation;
class Dx12ResourceManager;
class Dx12Resource;

class SyncObject
{
public:
	SyncObject(u64 fenceValue, Dx12CommandQueue& cmdQueue) : m_FenceValue(fenceValue), m_CommandQueue(cmdQueue) {}
	void Wait();

private:
	u64               m_FenceValue;
	Dx12CommandQueue& m_CommandQueue;
};

class Dx12RenderDevice : public render::RenderDevice
{
public:
	Dx12RenderDevice(bool bEnableGBV = false);
	~Dx12RenderDevice();

	u32 Swap();
	virtual void Flush() override;

	virtual Arc< render::Buffer > CreateBuffer(const char* name, render::Buffer::CreationInfo&& desc) override;
	virtual Arc< render::Buffer > CreateEmptyBuffer(const char* name = "") override;
	virtual Arc< render::Texture > CreateTexture(const char* name, render::Texture::CreationInfo&& desc) override;
	virtual Arc< render::Texture > CreateEmptyTexture(const char* name = "") override;

	virtual Arc< render::RenderTarget > CreateEmptyRenderTarget(const char* name = "") override;

	virtual Arc< render::Sampler > CreateSampler(const char* name, render::Sampler::CreationInfo&& info) override;

	virtual Arc< render::Shader > CreateShader(const char* name, render::Shader::CreationInfo&& info) override;

	virtual Box< render::ComputePipeline > CreateComputePipeline(const char* name) override;
	virtual Box< render::GraphicsPipeline > CreateGraphicsPipeline(const char* name) override;

	virtual Box< render::SceneResource > CreateSceneResource() override;

	void UpdateSubresources(Dx12Resource* pResource, u32 firstSubresource, u32 numSubresources, const D3D12_SUBRESOURCE_DATA* pSrcData);

	ID3D12Resource* CreateRHIResource(const D3D12_RESOURCE_DESC& desc, D3D12_RESOURCE_STATES initialState, D3D12_HEAP_PROPERTIES heapProperties, const D3D12_CLEAR_VALUE* pClearValue = nullptr);

	[[nodiscard]]
	inline ID3D12Device5* GetD3D12Device() const { return m_d3d12Device; }

	[[nodiscard]]
	Dx12CommandQueue& GraphicsQueue() const { return *m_pGraphicsQueue; }
	[[nodiscard]]
	Dx12CommandQueue& ComputeQueue() const { return *m_pComputeQueue; }
	[[nodiscard]]
	Dx12CommandQueue& CopyQueue() const { return *m_pCopyQueue; }

	Arc< Dx12CommandContext > BeginCommand(D3D12_COMMAND_LIST_TYPE commandType);
	SyncObject ExecuteCommand(Arc< Dx12CommandContext >&& pContext);

	inline D3D12MA::Allocator* dmaAllocator() const { return m_dmaAllocator; }
	virtual render::ResourceManager& GetResourceManager() const override;

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

private:
	void CreateDevice(bool bEnableGBV);

private:
	ID3D12Device5* m_d3d12Device = nullptr;

	Dx12CommandQueue* m_pGraphicsQueue = nullptr;
	Dx12CommandQueue* m_pComputeQueue  = nullptr;
	Dx12CommandQueue* m_pCopyQueue     = nullptr;

	Dx12ResourceManager* m_pResourceManager = nullptr;
	D3D12MA::Allocator*  m_dmaAllocator;

	u32 m_SRVDescriptorSize = 0;
	u32 m_RTVDescriptorSize = 0;
	u32 m_DSVDescriptorSize = 0;
	D3D_ROOT_SIGNATURE_VERSION m_HighestRootSignatureVersion;
};

}