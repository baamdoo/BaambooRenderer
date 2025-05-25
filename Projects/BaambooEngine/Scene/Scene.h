#pragma once
#include "Components.h"
#include "SceneRenderView.h"
#include "BaambooUtils/ModelLoader.h"

namespace baamboo
{

class EditorCamera;
class TransformSystem;
class StaticMeshSystem;
class MaterialSystem;

class Scene
{
public:
	Scene(const std::string& name);
	~Scene();

	[[nodiscard]]
	class Entity CreateEntity(const std::string& tag = "Empty");
	void RemoveEntity(Entity entity);

	class Entity ImportModel(fs::path filepath, MeshDescriptor descriptor);
	class Entity ImportModel(Entity rootEntity, fs::path filepath, MeshDescriptor descriptor);

	void Update(float dt);

	[[nodiscard]]
	SceneRenderView RenderView(const EditorCamera& camera) const;

	[[nodiscard]]
	const std::string& Name() const { return m_name; }
	[[nodiscard]]
	bool IsLoading() const { return m_bLoading; }

	[[nodiscard]]
	entt::registry& Registry() { return m_registry; }
	[[nodiscard]]
	const entt::registry& Registry() const { return m_registry; }
	[[nodiscard]]
	TransformSystem* GetTransformSystem() const { return m_pTransformSystem; }

private:
	friend class Entity;
	entt::registry m_registry;

	std::string m_name;
	bool m_bLoading = false;

	// [entity, dirty-components]
	std::unordered_map< entt::entity, u64 > m_entityDirtyMasks;

	TransformSystem*  m_pTransformSystem = nullptr;
	StaticMeshSystem* m_pStaticMeshSystem = nullptr;
	MaterialSystem*   m_pMaterialSystem = nullptr;
};

} // namespace baamboo 