#pragma once
#include "SceneSystem.h"

namespace baamboo
{

class TransformSystem;

class SkyLightSystem : public SceneSystem< LightComponent >
{
using Super = SceneSystem< LightComponent >;
public:
    SkyLightSystem(entt::registry& registry, TransformSystem* pTransformSystem);

    virtual void OnComponentConstructed(entt::registry& registry, entt::entity entity) override;
    virtual void OnComponentUpdated(entt::registry& registry, entt::entity entity) override;
    virtual void OnComponentDestroyed(entt::registry& registry, entt::entity entity) override;

    virtual std::vector< u64 > UpdateRenderData(const EditorCamera& edCamera) override;
    virtual void CollectRenderData(SceneRenderView& outView) const override;
    virtual void RemoveRenderData(u64 entityId) override;

    const std::vector< DirectionalLight >& GetRenderData() const { return m_DirectionalLights; }

private:
    TransformSystem* m_pTransformSystem = nullptr;

    std::vector< DirectionalLight > m_DirectionalLights;
};


class LocalLightSystem : public SceneSystem< LightComponent >
{
using Super = SceneSystem< LightComponent >;
public:
    LocalLightSystem(entt::registry& registry, TransformSystem* pTransformSystem);

    virtual void OnComponentConstructed(entt::registry& registry, entt::entity entity) override;
    virtual void OnComponentUpdated(entt::registry& registry, entt::entity entity) override;
    virtual void OnComponentDestroyed(entt::registry& registry, entt::entity entity) override;

    virtual std::vector< u64 > UpdateRenderData(const EditorCamera& edCamera) override;
    virtual void CollectRenderData(SceneRenderView& outView) const override;
    virtual void RemoveRenderData(u64 entityId) override;

private:
    TransformSystem* m_pTransformSystem = nullptr;

    std::vector< PointLight > m_PointLights;
    std::vector< SpotLight >  m_SpotLights;
};

} // namespace baamboo
