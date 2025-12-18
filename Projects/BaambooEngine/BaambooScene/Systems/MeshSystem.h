#pragma once
#include "SceneSystem.h"

namespace baamboo
{
	
class StaticMeshSystem : public SceneSystem< StaticMeshComponent >
{
using Super = SceneSystem< StaticMeshComponent >;
public:
	StaticMeshSystem(entt::registry& registry);

	virtual void OnComponentConstructed(entt::registry& registry, entt::entity entity) override;
	virtual void OnComponentUpdated(entt::registry& registry, entt::entity entity) override;
	virtual void OnComponentDestroyed(entt::registry& registry, entt::entity entity) override;

	virtual std::vector< u64 > UpdateRenderData(const EditorCamera& edCamera) override;
	virtual void CollectRenderData(SceneRenderView& outView) const override;
	virtual void RemoveRenderData(u64 entityId) override;

private:
	struct MeshRenderDataEntry
	{
		StaticMeshRenderView mesh;
		MaterialRenderView   material;

		bool hasMaterial = false;
	};

	std::unordered_map< u64, MeshRenderDataEntry > m_RenderData;
};

} // namespace baamboo