#include "BaambooPch.h"
#include "Scene/Scene.h"
#include "AssetRegistry.h"

namespace baamboo
{

void AssetRegistry::SetResourceManager(ResourceManagerAPI* pResourceManager)
{
	m_pResourceManager = pResourceManager;
}

void AssetRegistry::ImportModel(fs::path filepath, MeshDescriptor descriptor, Scene& scene)
{
	
}

} // namespace baamboo