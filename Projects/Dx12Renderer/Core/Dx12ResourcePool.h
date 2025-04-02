#pragma once
#include "BaambooCore/ResourceHandle.h"

namespace dx12
{

template< typename T > concept has_creation_info = requires { typename T::CreationInfo; };

template< typename TResource >
class ResourcePool
{
public:
    ResourcePool();
    void Release();

	baamboo::ResourceHandle< TResource > Create(RenderContext& context, std::wstring_view name, typename TResource::CreationInfo&& info)
	{
        u32 index;
        if (m_frees.size() > 0)
        {
            index = m_frees.front();
            m_frees.pop_front();
        }
        else 
        {
            index = static_cast<u32>(m_versions.size());
        	m_versions.push_back(0);
        }

        if (m_pResources.size() <= index)
            m_pResources.resize(m_pResources.size() * 2);
        m_pResources[index] = new TResource(context, name, std::move(info));
        return baamboo::ResourceHandle< TResource >(index, m_versions[index]);
	}

    baamboo::ResourceHandle< TResource > Add(TResource* pResource)
    {
        u32 index;
        if (m_frees.size() > 0)
        {
            index = m_frees.front();
            m_frees.pop_front();
        }
        else
        {
            index = static_cast<u32>(m_versions.size());
            m_versions.push_back(0);
        }

        if (m_pResources.size() <= index)
            m_pResources.resize(m_pResources.size() * 2);
        m_pResources[index] = pResource;
        return baamboo::ResourceHandle< TResource >(index, m_versions[index]);
    }

    void Remove(baamboo::ResourceHandle< TResource > handle)
	{
        if (!handle.IsValid())
            return;

        delete m_pResources[handle.Index()];
        m_pResources[handle.Index()] = nullptr;

        if (++m_versions[handle.Index()] < baamboo::INVALID_VERSION)
            m_frees.push_back(handle.Index());
	}

    TResource* Get(baamboo::ResourceHandle< TResource > handle) const
    {
        if (!handle.IsValid())
            return nullptr;

        assert(handle.Index() < m_versions.size());
        BB_ASSERT(handle.Version() == m_versions[handle.Index()],
            "Accessing deleted resource! Resource: %s_%d, Current version : %d", typeid(TResource).name(), handle.Index(), m_versions[handle.Index()]);

        return m_pResources[handle.Index()];
    }

private:
	std::deque< u32 >  m_frees;
	std::vector< u8 >  m_versions;

	std::vector< TResource* > m_pResources;
};

template< typename TResource >
ResourcePool< TResource >::ResourcePool()
	: m_pResources(1024)
{
}

template< typename TResource >
void ResourcePool< TResource >::Release()
{
    for (auto& pResource : m_pResources)
        RELEASE(pResource);
}

} // namespace dx12