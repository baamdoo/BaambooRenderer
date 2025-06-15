#include "RendererPch.h"
#include "Dx12Resource.h"
#include "RenderDevice/Dx12ResourceManager.h"

namespace dx12
{

Resource::Resource(RenderDevice& device, std::wstring_view name)
	: m_RenderDevice(device)
	, m_Name(name)
{
}

Resource::Resource(RenderDevice& device, std::wstring_view name, eResourceType type)
	: m_RenderDevice(device)
	, m_Name(name)
	, m_Type(type)
{
}

Resource::Resource(RenderDevice& device, std::wstring_view name, ResourceCreationInfo&& info, eResourceType type)
	: m_RenderDevice(device)
	, m_Name(name)
	, m_Type(type)
	, m_CurrentState(info.initialState)
{
	auto d3d12Device = m_RenderDevice.GetD3D12Device();

	switch (m_Type)
	{
	case eResourceType::Buffer:
		ThrowIfFailed(d3d12Device->CreateCommittedResource(
			&info.heapProps, info.heapFlags,
			&info.desc, info.initialState,
			nullptr, IID_PPV_ARGS(&m_d3d12Resource)));
		break;

	case eResourceType::Texture:
		m_pClearValue = nullptr;
		if (info.clearValue.Format != DXGI_FORMAT_UNKNOWN)
		{
			assert(info.clearValue.Format == info.desc.Format);

			m_pClearValue = new D3D12_CLEAR_VALUE();
			m_pClearValue->Format = info.clearValue.Format;
			memcpy(&(m_pClearValue->Color), &info.clearValue.Color[0], sizeof(info.clearValue.Color));
		}

		ThrowIfFailed(d3d12Device->CreateCommittedResource(
			&info.heapProps, info.heapFlags,
			&info.desc, info.initialState,
			m_pClearValue, IID_PPV_ARGS(&m_d3d12Resource)));

		break;

	case eResourceType::None:
		assert(!"Invalid entry in Resource::Resource()!");
	}

	assert(m_d3d12Resource);
	m_d3d12Resource->SetName(m_Name.data());
	m_ResourceDesc = m_d3d12Resource->GetDesc();

	SetFormatSupported();
}

Resource::~Resource()
{
	RELEASE(m_pClearValue);

	Resource::Reset();
}

void Resource::Reset()
{
	COM_RELEASE(m_d3d12Resource);
}

bool Resource::IsFormatSupported(D3D12_FORMAT_SUPPORT1 formatSupport) const
{
	return (m_FormatSupport.Support1 & formatSupport) != 0;
}

bool Resource::IsFormatSupported(D3D12_FORMAT_SUPPORT2 formatSupport) const
{
	return (m_FormatSupport.Support2 & formatSupport) != 0;
}

void Resource::SetD3D12Resource(ID3D12Resource* d3d12Resource, D3D12_RESOURCE_STATES states)
{
	assert(d3d12Resource);
	if (m_d3d12Resource)
		COM_RELEASE(m_d3d12Resource);

	m_d3d12Resource = d3d12Resource;
	m_ResourceDesc = m_d3d12Resource->GetDesc();
	ThrowIfFailed(m_d3d12Resource->SetName(m_Name.data()));

	SetFormatSupported();
	m_CurrentState.SetSubresourceState(states, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
}

void Resource::SetFormatSupported()
{
	auto d3d12Device = m_RenderDevice.GetD3D12Device();

	m_FormatSupport.Format = m_ResourceDesc.Format;
	ThrowIfFailed(d3d12Device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &m_FormatSupport, sizeof(D3D12_FEATURE_DATA_FORMAT_SUPPORT)));
}

}