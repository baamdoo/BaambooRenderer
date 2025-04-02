#pragma once
#include "BaambooCore/Transform.hpp"

#include <entt/entt.hpp>

//-------------------------------------------------------------------------
// TagComponent(Core) : Determines whether to expose in UI panel
//-------------------------------------------------------------------------
struct TagComponent
{
	std::string tag;
};

//-------------------------------------------------------------------------
// TransformComponent : Determines whether to be allocated to scene
//-------------------------------------------------------------------------
struct TransformComponent
{
	Transform transform;

	struct Hierarchy
	{
		entt::entity parent = entt::null;
		std::vector< entt::entity > children;
	} hierarchy;
};

//-------------------------------------------------------------------------
// StaticMeshComponent : Determines whether to be rendered statically
//-------------------------------------------------------------------------
struct StaticMeshComponent
{
	
};

//-------------------------------------------------------------------------
// DynamicMeshComponent : Determines whether to be rendered dynamically
//-------------------------------------------------------------------------
struct DynamicMeshComponent
{

};