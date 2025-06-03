#pragma once
#include "Components.h"
#include "FreeList.hpp"

namespace baamboo
{

class TransformSystem
{
public:
	TransformSystem(entt::registry& registry);

	void OnTransformConstructed(entt::registry& registry, entt::entity entity);
	void OnTransformUpdated(entt::registry& registry, entt::entity entity);
	void OnTransformDestroyed(entt::registry& registry, entt::entity entity);

	std::vector< entt::entity > Update();

	void MarkDirty(entt::entity entity);
	void AttachChild(entt::entity parent, entt::entity child);
	void DetachChild(entt::entity child);

	[[nodiscard]]
	const mat4& WorldMatrix(u32 index) const { assert(index < m_mWorlds.size()); return m_mWorlds[index]; }

private:
	void UpdateWorldTransform(entt::entity entity);

private:
	entt::registry& m_Registry;

	std::vector< mat4 > m_mWorlds;
	FreeList<>          m_IndexAllocator;
};

} // namespace baamboo