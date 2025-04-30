#pragma once
#include "BackendAPI.h"
#include "Scene/Entity.h"
#include "Scene/Scene.h"
#include "BaambooCore/Singleton.h"
#include "BaambooUtils/ModelLoader.h"

#include <queue>

namespace baamboo
{

class Scene;

struct MeshData
{
	VertexHandle vertex;
	IndexHandle  index;
};

struct MaterialData
{
	float3 tint{ 1, 1, 1 };

	u32 albedo;
	u32 normal;
	u32 specular;
	u32 emission;
	u32 orm;
};

struct ModelData
{
	MeshData mesh;

	std::optional< MaterialData > material;
};

class AssetRegistry : public Singleton< AssetRegistry >
{
public:
	void SetResourceManager(ResourceManagerAPI* pResourceManager);
	void ImportModel(fs::path filepath, MeshDescriptor descriptor, Scene& scene);

private:
	ResourceManagerAPI* m_pResourceManager = nullptr;

	std::unordered_map< std::wstring_view, ModelData > m_modelCache;
};

} // namespace baamboo