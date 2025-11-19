#pragma once
#include "SceneSystem.h"

namespace baamboo
{

class SkyLightSystem : public SceneSystem< LightComponent >
{
using Super = SceneSystem< LightComponent >;

public:
    SkyLightSystem(entt::registry& registry);

    virtual void OnComponentConstructed(entt::registry& registry, entt::entity entity) override;
    virtual void OnComponentUpdated(entt::registry& registry, entt::entity entity) override;
    virtual void OnComponentDestroyed(entt::registry& registry, entt::entity entity) override;

    virtual std::vector< u64 > Update(const EditorCamera& edCamera) override;
};


class LocalLightSystem : public SceneSystem< LightComponent >
{
using Super = SceneSystem< LightComponent >;

public:
    LocalLightSystem(entt::registry& registry);

    virtual void OnComponentConstructed(entt::registry& registry, entt::entity entity) override;
    virtual void OnComponentUpdated(entt::registry& registry, entt::entity entity) override;
    virtual void OnComponentDestroyed(entt::registry& registry, entt::entity entity) override;

    virtual std::vector< u64 > Update(const EditorCamera& edCamera) override;
};

} // namespace baamboo
