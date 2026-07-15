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

    auto index      = m_IndexAllocator.allocate();
    auto& transform = registry.get< TransformComponent >(entity);
    transform.world = index;

    if (index >= m_mWorlds.size())
        m_mWorlds.resize(static_cast<u64>(index) * 2);

    m_mWorlds[index] = mat4(1.0f);

    registry.emplace< RootComponent >(entity);
    m_bHierarchyDirty = true;
}

void TransformSystem::OnComponentUpdated(entt::registry& registry, entt::entity entity)
{
    MarkDirty(entity);
}

void TransformSystem::OnComponentDestroyed(entt::registry& registry, entt::entity entity)
{
    auto& transform = registry.get< TransformComponent >(entity);
    m_IndexAllocator.release(transform.world);

    m_bHierarchyDirty = true;

    Super::OnComponentDestroyed(registry, entity);
}

std::vector< u64 > TransformSystem::UpdateRenderData(const EditorCamera& edCamera)
{
    UNUSED(edCamera);

    std::vector< u64 > markedEntities;
    if (m_DirtyEntities.empty() && m_ExpiredEntities.empty())
    {
        return markedEntities;
    }

    for (auto entity : m_ExpiredEntities)
    {
        auto id = entt::to_integral(entity);

        markedEntities.emplace_back(id);
        RemoveRenderData(id);
    }
    m_ExpiredEntities.clear();

    // Rebuild DFS pre-order cache if hierarchy structure changed
    if (m_bHierarchyDirty)
        RebuildHierarchyOrder();

    // Sort dirty entities by hierarchy order (guarantees parent-before-child)
    std::vector< entt::entity > sortedDirty;
    sortedDirty.reserve(m_DirtyEntities.size());
    for (auto entity : m_DirtyEntities)
    {
        if (m_Registry.valid(entity))
            sortedDirty.push_back(entity);
    }
    std::sort(sortedDirty.begin(), sortedDirty.end(), [this](entt::entity a, entt::entity b)
        {
            return m_HierarchyPosition[a] < m_HierarchyPosition[b];
        });

    for (auto entity : sortedDirty)
    {
        auto& transformComponent = m_Registry.get<TransformComponent>(entity);
        transformComponent.transform.Update();
        UpdateWorldTransform(entity);

        u64 id = entt::to_integral(entity);
        TransformRenderView& view = m_RenderData[id];
        view.id            = id;
        view.mWorld        = WorldMatrix(transformComponent.world);
        view.mWorldInverse = glm::inverse(view.mWorld);

        markedEntities.emplace_back(id);
    }

    ClearDirtyEntities();
    return markedEntities;
}

void TransformSystem::CollectRenderData(SceneRenderView& outView) const
{
    outView.transforms.reserve(m_RenderData.size());

    for (const auto& [id, view] : m_RenderData)
    {
        outView.transforms.push_back(view);

        u32 transformIndex = static_cast<u32>(outView.transforms.size()) - 1;

        auto& draw     = outView.draws[id];
        draw.transform = transformIndex;
    }
}

void TransformSystem::RemoveRenderData(u64 entityId)
{
    m_RenderData.erase(entityId);
}

void TransformSystem::MarkDirty(entt::entity entity)
{
    Super::MarkDirty(entity);

    auto& transform = m_Registry.get< TransformComponent >(entity);
    auto  child     = transform.hierarchy.firstChild;
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
        if (m_Registry.any_of< RootComponent >(child))
            m_Registry.remove< RootComponent >(child);

        auto& parentTransform = m_Registry.get< TransformComponent >(parent);
        childTransform.hierarchy.depth = parentTransform.hierarchy.depth + 1;

        if (parentTransform.hierarchy.firstChild == entt::null)
        {
            parentTransform.hierarchy.firstChild = child;
            parentTransform.hierarchy.lastChild  = child;
        }
        else
        {
            auto& lastChildTransform = m_Registry.get< TransformComponent >(parentTransform.hierarchy.lastChild);
            lastChildTransform.hierarchy.nextSibling = child;
            childTransform.hierarchy.prevSibling = parentTransform.hierarchy.lastChild;
            parentTransform.hierarchy.lastChild = child;
        }

        m_bHierarchyDirty = true;
        MarkDirty(child);
    }
    else
    {
        m_Registry.emplace_or_replace< RootComponent >(child);

        childTransform.hierarchy.depth = 0;
        childTransform.hierarchy.prevSibling = entt::null;
        childTransform.hierarchy.nextSibling = entt::null;
        m_bHierarchyDirty = true;
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

        // Update firstChild if detaching the first child
        if (parentTransform.hierarchy.firstChild == child)
            parentTransform.hierarchy.firstChild = childTransform.hierarchy.nextSibling;

        // Update lastChild if detaching the last child
        if (parentTransform.hierarchy.lastChild == child)
            parentTransform.hierarchy.lastChild = childTransform.hierarchy.prevSibling;

        // Relink siblings
        if (childTransform.hierarchy.prevSibling != entt::null)
        {
            auto& prevSiblingTransform = m_Registry.get< TransformComponent >(childTransform.hierarchy.prevSibling);
            prevSiblingTransform.hierarchy.nextSibling = childTransform.hierarchy.nextSibling;
        }
        if (childTransform.hierarchy.nextSibling != entt::null)
        {
            auto& nextSiblingTransform = m_Registry.get< TransformComponent >(childTransform.hierarchy.nextSibling);
            nextSiblingTransform.hierarchy.prevSibling = childTransform.hierarchy.prevSibling;
        }

        childTransform.hierarchy.parent = entt::null;
        childTransform.hierarchy.prevSibling = entt::null;
        childTransform.hierarchy.nextSibling = entt::null;

        m_Registry.emplace_or_replace<RootComponent>(child);
        m_bHierarchyDirty = true;
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

void TransformSystem::RebuildHierarchyOrder()
{
    m_HierarchyOrder.clear();
    m_HierarchyPosition.clear();

    // Collect root entities
    std::vector< entt::entity > roots;
    m_Registry.view< RootComponent, TransformComponent >().each([&roots](auto entity, auto&)
        {
            roots.push_back(entity);
        });

    m_HierarchyOrder.reserve(m_Registry.view< TransformComponent >().size());

    // Iterative DFS pre-order traversal
    std::vector< entt::entity > stack;
    for (auto it = roots.rbegin(); it != roots.rend(); ++it)
        stack.push_back(*it);

    while (!stack.empty())
    {
        auto entity = stack.back();
        stack.pop_back();

        m_HierarchyPosition[entity] = static_cast< u32 >(m_HierarchyOrder.size());
        m_HierarchyOrder.push_back(entity);

        // Push children in reverse order so firstChild is processed first
        auto& tc = m_Registry.get< TransformComponent >(entity);
        auto child = tc.hierarchy.lastChild;
        while (child != entt::null)
        {
            stack.push_back(child);
            child = m_Registry.get< TransformComponent >(child).hierarchy.prevSibling;
        }
    }

    m_bHierarchyDirty = false;
}

} // namespace baamboo