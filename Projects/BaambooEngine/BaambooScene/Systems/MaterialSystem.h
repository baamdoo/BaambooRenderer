#pragma once
#include "SceneSystem.h"

namespace baamboo
{

class MaterialSystem : public SceneSystem< MaterialComponent >
{
using Super = SceneSystem< MaterialComponent >;
public:
	MaterialSystem(entt::registry& registry);

	virtual void OnComponentConstructed(entt::registry& registry, entt::entity entity) override;
	virtual void OnComponentUpdated(entt::registry& registry, entt::entity entity) override;
	virtual void OnComponentDestroyed(entt::registry& registry, entt::entity entity) override;

	virtual std::vector< u64 > Update(const EditorCamera& edCamera) override;
};

} // namespace baamboo