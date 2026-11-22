#pragma once
#include "RenderCommon/RenderDevice.h"

#include <atomic>

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
class Dx12RootSignature;
class CommandSignature;

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
using Super = render::RenderDevice;
public:
	Dx12RenderDevice(const render::DeviceSettings& ds, bool bEnableGBV = false);
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
	virtual Arc< render::ShaderBindingTable > CreateSBT(const char* name) override;

	virtual Arc< render::BottomLevelAccelerationStructure > CreateBLAS(const char* name) override;
	virtual Arc< render::TopLevelAccelerationStructure > CreateTLAS(const char* name) override;

	virtual Box< render::GraphicsPipeline > CreateGraphicsPipeline(const char* name) override;
	virtual Box< render::ComputePipeline > CreateComputePipeline(const char* name) override;
	virtual Box< render::RaytracingPipeline > CreateRaytracingPipeline(const char* name) override;

	void UpdateSubresources(Dx12Resource* pResource, u32 firstSubresource, u32 numSubresources, const D3D12_SUBRESOURCE_DATA* pSrcData);

	ID3D12Resource2* CreateRHIResource(const D3D12_RESOURCE_DESC1& desc, D3D12_BARRIER_LAYOUT initialLayout, D3D12_HEAP_PROPERTIES heapProperties, const D3D12_CLEAR_VALUE* pClearValue = nullptr);

	[[nodiscard]]
	inline ID3D12Device12* GetD3D12Device() const { return m_d3d12Device; }

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
	DescriptorAllocation AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE type, u32 numDescriptors = 1);

	using render::RenderDevice::DeferRelease;
	void DeferRelease(IUnknown* d3d12Object);

	Arc< Dx12RootSignature > GetGlobalRootSignature() const;
	Arc< DescriptorPool > GetGlobalDescriptorHeap() const;
	ID3D12CommandSignature* GetDispatchD3D12CommandSignature() const;
	ID3D12CommandSignature* GetMeshDispatchD3D12CommandSignature() const;

	virtual bool SaveTextureToEXR(const Arc< render::Texture >& pTexture, const char* path) override;

	virtual u64 ValidationErrorCount() const override { return m_ValidationErrorCount.load(std::memory_order_relaxed); }
	virtual bool IsValidationActive() const override { return m_d3d12InfoQueue != nullptr; }

	[[nodiscard]]
	D3D_ROOT_SIGNATURE_VERSION GetHighestRootSignatureVersion() const { return m_HighestRootSignatureVersion; }

private:
	void CreateDevice(bool bEnableGBV);
	void RegisterValidationCallback();

	static void CALLBACK OnValidationMessage(
		D3D12_MESSAGE_CATEGORY category, D3D12_MESSAGE_SEVERITY severity, D3D12_MESSAGE_ID id, LPCSTR description, void* pContext);

private:
	ID3D12Device12* m_d3d12Device = nullptr;

	// Debug-layer message sink; null when the debug layer (or ID3D12InfoQueue1) is unavailable.
	ID3D12InfoQueue1*    m_d3d12InfoQueue          = nullptr;
	DWORD                m_ValidationCallbackCookie = 0;
	std::atomic< u64 >   m_ValidationErrorCount     = 0;

	Dx12CommandQueue* m_pGraphicsQueue = nullptr;
	Dx12CommandQueue* m_pComputeQueue  = nullptr;
	Dx12CommandQueue* m_pCopyQueue     = nullptr;

	D3D12MA::Allocator*  m_dmaAllocator     = nullptr;

	Box< DescriptorPool >    m_pRtvDescriptorPool;
	Box< DescriptorPool >    m_pDsvDescriptorPool;
	Arc< DescriptorPool >    m_pGlobalDescriptorHeap;
	Arc< Dx12RootSignature > m_pGlobalRootSignature;
	Box< CommandSignature >  m_pDispatchSignature; // compute indirect: DISPATCH arguments only
	Box< CommandSignature >  m_pMeshDispatchSignature;

	Dx12ResourceManager* m_pResourceManager = nullptr;

	D3D_ROOT_SIGNATURE_VERSION m_HighestRootSignatureVersion;
};

}