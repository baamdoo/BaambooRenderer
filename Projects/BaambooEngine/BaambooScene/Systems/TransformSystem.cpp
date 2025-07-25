#include "BaambooPch.h"
#include "TransformSystem.h"

namespace baamboo
{

TransformSystem::TransformSystem(entt::registry& registry)
	: Super(registry)
{
    m_mWorlds.resize(1024);
    m_IndexAllocator.reserve(1024);
}

void TransformSystem::OnComponentConstructed(entt::registry& registry, entt::entity entity)
{
	MarkDirty(entity);

    auto index = m_IndexAllocator.allocate();
    auto& transform = registry.get< TransformComponent >(entity);
    transform.world = index;

    if (index > m_mWorlds.size())
        m_mWorlds.resize(index * 2);

    m_mWorlds[index] = mat4(1.0f);
}

void TransformSystem::OnComponentUpdated(entt::registry& registry, entt::entity entity)
{
    MarkDirty(entity);
}

void TransformSystem::OnComponentDestroyed(entt::registry& registry, entt::entity entity)
{
    auto& transform = registry.get< TransformComponent >(entity);
    m_IndexAllocator.release(transform.world);
}

std::vector< u64 > TransformSystem::Update()
{
    // Sort by depth to ensure parents are processed before children
    m_Registry.sort< TransformComponent >([](const auto& lhs, const auto& rhs)
        {
            return lhs.hierarchy.depth < rhs.hierarchy.depth;
        });

    std::vector< u64 > markedEntities;
    m_Registry.view< TransformComponent >().each([&](auto entity, auto& transformComponent)
        {
            if (m_DirtyEntities.contains(entity))
            {
                transformComponent.transform.Update();
                UpdateWorldTransform(entity);

                m_DirtyEntities.erase(entity);
                markedEntities.push_back(entt::to_integral(entity));
            }
        });

    return markedEntities;
}

void TransformSystem::MarkDirty(entt::entity entity)
{
    Super::MarkDirty(entity);

    auto& transform = m_Registry.get< TransformComponent >(entity);

    auto child = transform.hierarchy.firstChild;
    while (child != entt::null) 
    {
        MarkDirty(child);
        auto& childTransform = m_Registry.get< TransformComponent >(child);
        child = childTransform.hierarchy.nextSibling;
    }
}

void TransformSystem::AttachChild(entt::entity parent, entt::entity child)
{
    auto& childTransform = m_Registry.get< TransformComponent >(child);
    if (childTransform.hierarchy.parent != entt::null)
        DetachChild(child);

    childTransform.hierarchy.parent = parent;
    if (parent != entt::null) 
    {
        auto& parentTransform = m_Registry.get< TransformComponent >(parent);
        childTransform.hierarchy.depth = parentTransform.hierarchy.depth + 1;

        if (parentTransform.hierarchy.firstChild == entt::null) 
        {
            parentTransform.hierarchy.firstChild = child;
        }
        else 
        {
            auto lastChild = parentTransform.hierarchy.firstChild;
            while (m_Registry.valid(lastChild)) 
            {
                auto& lastChildTransform = m_Registry.get< TransformComponent >(lastChild);
                if (lastChildTransform.hierarchy.nextSibling == entt::null) 
                {
                    lastChildTransform.hierarchy.nextSibling = child;
                    childTransform.hierarchy.prevSibling = lastChild;
                    break;
                }

                lastChild = lastChildTransform.hierarchy.nextSibling;
            }
        }

        MarkDirty(child);
    }
    else 
    {
        childTransform.hierarchy.depth = 0;
        childTransform.hierarchy.prevSibling = entt::null;
        childTransform.hierarchy.nextSibling = entt::null;
        MarkDirty(child);
    }
}

void TransformSystem::DetachChild(entt::entity child)
{
    auto& childTransform = m_Registry.get< TransformComponent >(child);
    auto parent = childTransform.hierarchy.parent;

    if (parent != entt::null && m_Registry.valid(parent)) 
    {
        auto& parentTransform = m_Registry.get< TransformComponent >(parent);
        if (parentTransform.hierarchy.firstChild == child) 
        {
            parentTransform.hierarchy.firstChild = childTransform.hierarchy.nextSibling;

            if (childTransform.hierarchy.nextSibling != entt::null) 
            {
                auto& nextSiblingTransform = m_Registry.get< TransformComponent >(childTransform.hierarchy.nextSibling);
                nextSiblingTransform.hierarchy.prevSibling = entt::null;
            }
        }
        else 
        {
            if (childTransform.hierarchy.prevSibling != entt::null) 
            {
                auto& prevSiblingTransform = m_Registry.get< TransformComponent >(childTransform.hierarchy.prevSibling);
                prevSiblingTransform.hierarchy.nextSibling = childTransform.hierarchy.nextSibling;

                if (childTransform.hierarchy.nextSibling != entt::null) 
                {
                    auto& nextSiblingTransform = m_Registry.get< TransformComponent >(childTransform.hierarchy.nextSibling);
                    nextSiblingTransform.hierarchy.prevSibling = childTransform.hierarchy.prevSibling;
                }
            }
        }

        childTransform.hierarchy.parent = entt::null;
        childTransform.hierarchy.prevSibling = entt::null;
        childTransform.hierarchy.nextSibling = entt::null;
    }
}

void TransformSystem::UpdateWorldTransform(entt::entity entity)
{
    auto& transformComponent = m_Registry.get< TransformComponent >(entity);
    auto localMatrix = transformComponent.transform.Matrix();

    assert(transformComponent.world < m_mWorlds.size());
    auto& worldTransform = m_mWorlds[transformComponent.world];
    if (transformComponent.hierarchy.parent != entt::null && m_Registry.valid(transformComponent.hierarchy.parent)) 
    {
        auto& parentTransform = m_Registry.get< TransformComponent >(transformComponent.hierarchy.parent);
        worldTransform = m_mWorlds[parentTransform.world] * localMatrix;
    }
    else 
    {
        worldTransform = localMatrix;
    }
}

} // namespace baamboo