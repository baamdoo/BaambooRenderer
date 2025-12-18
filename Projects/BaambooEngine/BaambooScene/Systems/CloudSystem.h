#pragma once
#include "SceneSystem.h"

namespace baamboo
{

class AtmosphereSystem;

class CloudSystem : public SceneSystem< CloudComponent >
{
using Super = SceneSystem< CloudComponent >;
public:
    CloudSystem(entt::registry& registry, AtmosphereSystem* pAtmosphereSystem);

    virtual void OnComponentConstructed(entt::registry& registry, entt::entity entity) override;
    virtual void OnComponentUpdated(entt::registry& registry, entt::entity entity) override;
    virtual void OnComponentDestroyed(entt::registry& registry, entt::entity entity) override;

    virtual std::vector< u64 > UpdateRenderData(const EditorCamera& edCamera) override;
    virtual void CollectRenderData(SceneRenderView& outView) const override;
    virtual void RemoveRenderData(u64 entityId) override;

private:
    AtmosphereSystem* m_pAtmosphereSystem = nullptr;

    CloudRenderView m_RenderData = {};

    bool m_bHasData = false;
};

} // namespace baamboo