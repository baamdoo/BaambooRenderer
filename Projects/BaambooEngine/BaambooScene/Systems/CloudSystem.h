#pragma once
#include "SceneSystem.h"

namespace baamboo
{

class CloudSystem : public SceneSystem< CloudComponent >
{
using Super = SceneSystem< CloudComponent >;
public:
    CloudSystem(entt::registry& registry);

    virtual void OnComponentConstructed(entt::registry& registry, entt::entity entity) override;
    virtual void OnComponentUpdated(entt::registry& registry, entt::entity entity) override;
    virtual void OnComponentDestroyed(entt::registry& registry, entt::entity entity) override;

    virtual std::vector< u64 > Update(const EditorCamera& edCamera) override;
};

} // namespace baamboo