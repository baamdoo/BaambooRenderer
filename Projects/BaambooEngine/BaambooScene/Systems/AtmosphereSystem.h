#pragma once
#include "SceneSystem.h"

namespace baamboo
{

class AtmosphereSystem : public SceneSystem< AtmosphereComponent >
{
using Super = SceneSystem< AtmosphereComponent >;

public:
	AtmosphereSystem(entt::registry& registry);

	virtual void OnComponentConstructed(entt::registry& registry, entt::entity entity) override;
	virtual void OnComponentUpdated(entt::registry& registry, entt::entity entity) override;
	virtual void OnComponentDestroyed(entt::registry& registry, entt::entity entity) override;

	virtual std::vector< u64 > Update() override;
};

} // namespace baamboo