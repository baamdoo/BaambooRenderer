#pragma once
#include "SceneSystem.h"

struct AtmosphereRenderView;

namespace baamboo
{

class SkyLightSystem;

class AtmosphereSystem : public SceneSystem< AtmosphereComponent >
{
using Super = SceneSystem< AtmosphereComponent >;
public:
	AtmosphereSystem(entt::registry& registry, SkyLightSystem* pSkyLightSystem);

	virtual void OnComponentConstructed(entt::registry& registry, entt::entity entity) override;
	virtual void OnComponentUpdated(entt::registry& registry, entt::entity entity) override;
	virtual void OnComponentDestroyed(entt::registry& registry, entt::entity entity) override;

	virtual std::vector< u64 > UpdateRenderData(const EditorCamera& edCamera) override;
	virtual void CollectRenderData(SceneRenderView& outView) const override;
	virtual void RemoveRenderData(u64 entityId) override;

	const AtmosphereRenderView& GetRenderData() const { return m_RenderData; }

private:
	SkyLightSystem* m_pSkyLightSystem = nullptr;

	AtmosphereRenderView m_RenderData;

	bool m_bHasData = false;
};

} // namespace baamboo