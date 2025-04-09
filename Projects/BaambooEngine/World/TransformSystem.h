#pragma once
#include "Components.h"
#include "BaambooUtils/FreeList.hpp"

namespace baamboo
{

class TransformSystem
{
public:
	TransformSystem(entt::registry& registry);

	void OnTransformConstructed(entt::registry& registry, entt::entity entity);
	void OnTransformUpdated(entt::registry& registry, entt::entity entity);
	void OnTransformDestroy(entt::registry& registry, entt::entity entity);

	void Update();

	void MarkDirty(entt::entity entity);
	void AttachChild(entt::entity parent, entt::entity child);
	void DetachChild(entt::entity child);

private:
	void UpdateWorldTransform(entt::entity entity);

private:
	entt::registry& m_registry;

	std::vector< mat4 > m_mWorlds;
	FreeList<>          m_indexAllocator;
};

} // namespace baamboo