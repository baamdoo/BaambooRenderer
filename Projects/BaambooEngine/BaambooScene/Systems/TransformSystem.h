#pragma once
#include "SceneSystem.h"
#include "FreeList.hpp"

namespace baamboo
{

class TransformSystem : public SceneSystem< TransformComponent >
{
using Super = SceneSystem< TransformComponent >;

public:
	TransformSystem(entt::registry& registry);

	virtual void OnComponentConstructed(entt::registry& registry, entt::entity entity) override;
	virtual void OnComponentUpdated(entt::registry& registry, entt::entity entity) override;
	virtual void OnComponentDestroyed(entt::registry& registry, entt::entity entity) override;

	virtual std::vector< u64 > Update() override;

	void AttachChild(entt::entity parent, entt::entity child);
	void DetachChild(entt::entity child);

	[[nodiscard]]
	const mat4& WorldMatrix(u32 index) const { assert(index < m_mWorlds.size()); return m_mWorlds[index]; }

private:
	virtual void MarkDirty(entt::entity entity) override;

	void UpdateWorldTransform(entt::entity entity);

private:
	std::vector< mat4 > m_mWorlds;
	FreeList<>          m_IndexAllocator;
};

} // namespace baamboo