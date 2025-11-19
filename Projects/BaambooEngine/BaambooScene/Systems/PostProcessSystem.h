#pragma once
#include "SceneSystem.h"

namespace baamboo
{

class PostProcessSystem : public SceneSystem< PostProcessComponent >
{
using Super = SceneSystem< PostProcessComponent >;
public:
	PostProcessSystem(entt::registry& registry);

	virtual void OnComponentConstructed(entt::registry& registry, entt::entity entity) override;
	virtual void OnComponentUpdated(entt::registry& registry, entt::entity entity) override;
	virtual void OnComponentDestroyed(entt::registry& registry, entt::entity entity) override;

	virtual std::vector< u64 > Update(const EditorCamera& edCamera) override;
};

} // namespace baamboo