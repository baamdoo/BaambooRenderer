#pragma once
#include "RenderDevice/Dx12DescriptorAllocation.h"

#include <filesystem>
namespace fs = std::filesystem;

namespace dx12
{

constexpr D3D12_RESOURCE_STATES D3D12_RESOURCE_STATE_INVALID = (D3D12_RESOURCE_STATES)(~0u);

enum class eResourceType
{
	None,
	Buffer,
	Texture,
	Sampler,
	Shader,
};

struct ResourceState
{
	ResourceState() = default;
	explicit ResourceState(D3D12_RESOURCE_STATES state)
		: State(state) {}

	// iterator
	auto begin() const noexcept { return SubresourceStates.begin(); }
	auto end() const noexcept { return SubresourceStates.end(); }

	bool IsValid() const { return State != D3D12_RESOURCE_STATE_INVALID;}

	void SetSubresourceState(D3D12_RESOURCE_STATES state, u32 subresource)
	{
		if (subresource == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES)
		{
			State = state;
			SubresourceStates.clear();
		}
		else
		{
			SubresourceStates[subresource] = state;
		}
	}

	D3D12_RESOURCE_STATES GetSubresourceState(u32 subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES) const
	{
		D3D12_RESOURCE_STATES state = State;

		const auto iter = SubresourceStates.find(subresource);
		if (iter != SubresourceStates.end())
			state = iter->second;

		return state;
	}

	D3D12_RESOURCE_STATES                  State = D3D12_RESOURCE_STATE_INVALID;
	std::map< u32, D3D12_RESOURCE_STATES > SubresourceStates;
};

struct ResourceCreationInfo
{
	D3D12_RESOURCE_DESC   desc;
	D3D12_HEAP_PROPERTIES heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
	D3D12_HEAP_FLAGS      heapFlags = D3D12_HEAP_FLAG_NONE;
	D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_COMMON;
	D3D12_CLEAR_VALUE     clearValue;
};

class Resource : public ArcBase
{
public:
	friend class ResourceManager;

	Resource(RenderDevice& device, std::wstring_view name);
	Resource(RenderDevice& device, std::wstring_view name, eResourceType type);
	Resource(RenderDevice& device, std::wstring_view name, ResourceCreationInfo&& info, eResourceType type);
	virtual ~Resource();

	[[nodiscard]]
	inline bool IsValid() const { return m_d3d12Resource != nullptr; }

	[[nodiscard]]
	inline ID3D12Resource* GetD3D12Resource() const { return m_d3d12Resource; }
	[[nodiscard]]
	inline D3D12_RESOURCE_DESC GetResourceDesc() const { return m_ResourceDesc; }
	[[nodiscard]]
	inline const ResourceState& GetCurrentState() const { return m_CurrentState; }

	virtual void SetD3D12Resource(ID3D12Resource* d3d12Resource, D3D12_RESOURCE_STATES states);
	void SetCurrentState(D3D12_RESOURCE_STATES state, u32 subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES) { m_CurrentState.SetSubresourceState(state, subresource); }

protected:
	bool IsFormatSupported(D3D12_FORMAT_SUPPORT1 formatSupport) const;
	bool IsFormatSupported(D3D12_FORMAT_SUPPORT2 formatSupport) const;

	virtual void Reset();

private:
	void SetFormatSupported();

protected:
	RenderDevice&    m_RenderDevice;
	std::wstring_view m_Name;
	eResourceType     m_Type = eResourceType::None;

	ID3D12Resource*                   m_d3d12Resource = nullptr;
	D3D12_RESOURCE_DESC               m_ResourceDesc = {};
	D3D12_FEATURE_DATA_FORMAT_SUPPORT m_FormatSupport = {};

	ResourceState m_CurrentState = {};

	D3D12_CLEAR_VALUE* m_pClearValue = nullptr;
};

}