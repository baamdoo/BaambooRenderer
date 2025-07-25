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

	virtual std::vector< u64 > Update() override;
};

} // namespace baamboo