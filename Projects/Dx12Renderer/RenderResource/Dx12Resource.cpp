#include "RendererPch.h"
#include "Dx12Resource.h"
#include "RenderDevice/Dx12ResourceManager.h"

namespace dx12
{

Dx12Resource::Dx12Resource(Dx12RenderDevice& rd, const std::string& name)
	: m_RenderDevice(rd)
	, m_Name(ConvertToWString(name))
{
}

Dx12Resource::Dx12Resource(Dx12RenderDevice& rd, const std::string& name, eResourceType type)
	: m_RenderDevice(rd)
	, m_Name(ConvertToWString(name))
	, m_Type(type)
{
}

Dx12Resource::Dx12Resource(Dx12RenderDevice& rd, const std::string& name, Dx12ResourceCreationInfo&& info, eResourceType type)
	: m_RenderDevice(rd)
	, m_Name(ConvertToWString(name))
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
		if (info.desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET)
		{
			//assert(info.clearValue.Format == info.desc.Format);

			m_pClearValue         = new D3D12_CLEAR_VALUE();
			m_pClearValue->Format = info.clearValue.Format;
			memcpy(&(m_pClearValue->Color), &info.clearValue.Color[0], sizeof(info.clearValue.Color));
		}
		else if (info.desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)
		{
			m_pClearValue         = new D3D12_CLEAR_VALUE();
			m_pClearValue->Format = info.clearValue.Format;
			memcpy(&(m_pClearValue->DepthStencil), &info.clearValue.DepthStencil, sizeof(info.clearValue.DepthStencil));
		}

		ThrowIfFailed(d3d12Device->CreateCommittedResource(
			&info.heapProps, info.heapFlags,
			&info.desc, info.initialState,
			m_pClearValue, IID_PPV_ARGS(&m_d3d12Resource)));

		break;

	case eResourceType::Sampler:

		break;

	case eResourceType::None:
		__debugbreak();
		assert(!"Invalid entry in Resource::Resource()!");
		break;
	}

	assert(m_d3d12Resource);
	m_d3d12Resource->SetName(m_Name.data());
	m_ResourceDesc = m_d3d12Resource->GetDesc();

	SetFormatSupported();
}

Dx12Resource::~Dx12Resource()
{
	RELEASE(m_pClearValue);

	Dx12Resource::Reset();
}

void Dx12Resource::Reset()
{
	COM_RELEASE(m_d3d12Resource);
}

bool Dx12Resource::IsFormatSupported(D3D12_FORMAT_SUPPORT1 formatSupport) const
{
	return (m_FormatSupport.Support1 & formatSupport) != 0;
}

bool Dx12Resource::IsFormatSupported(D3D12_FORMAT_SUPPORT2 formatSupport) const
{
	return (m_FormatSupport.Support2 & formatSupport) != 0;
}

void Dx12Resource::SetD3D12Resource(ID3D12Resource* d3d12Resource, D3D12_RESOURCE_STATES states)
{
	assert(d3d12Resource);
	COM_RELEASE(m_d3d12Resource);

	m_d3d12Resource = d3d12Resource;
	m_ResourceDesc = m_d3d12Resource->GetDesc();
	ThrowIfFailed(m_d3d12Resource->SetName(m_Name.data()));

	SetFormatSupported();
	m_CurrentState.SetSubresourceState(states, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
}

void Dx12Resource::SetFormatSupported()
{
	auto d3d12Device = m_RenderDevice.GetD3D12Device();

	m_FormatSupport.Format = m_ResourceDesc.Format;
	ThrowIfFailed(d3d12Device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &m_FormatSupport, sizeof(D3D12_FEATURE_DATA_FORMAT_SUPPORT)));
}

}