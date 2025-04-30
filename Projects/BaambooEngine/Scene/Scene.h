#pragma once
#include "Components.h"
#include "SceneRenderView.h"
#include "BaambooUtils/ModelLoader.h"

namespace baamboo
{

constexpr size_t NUM_MAX_ENTITIES = 8 * 1024 * 1024;

class TransformSystem;
class CameraSystem;

class Scene
{
public:
	Scene(const std::string& name);
	~Scene();

	[[nodiscard]]
	class Entity CreateEntity(const std::string& tag = "Empty");
	void RemoveEntity(Entity entity);

	[[nodiscard]]
	class Entity ImportModel(fs::path filepath, MeshDescriptor descriptor, ResourceManagerAPI& rm);

	void Update(float dt);

	[[nodiscard]]
	SceneRenderView RenderView() const;

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
	[[nodiscard]]
	CameraSystem* GetCameraSystem() const { return m_pCameraSystem; }

private:
	friend class Entity;
	entt::registry m_registry;

	std::string m_name;
	bool m_bLoading = false;

	TransformSystem* m_pTransformSystem;
	CameraSystem*    m_pCameraSystem;
};

} // namespace baamboo 