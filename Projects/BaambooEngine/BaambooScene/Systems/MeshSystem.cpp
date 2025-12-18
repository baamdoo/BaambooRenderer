#include "BaambooPch.h"
#include "MeshSystem.h"

namespace baamboo
{

StaticMeshSystem::StaticMeshSystem(entt::registry& registry)
	: Super(registry)
{
    DependsOn< MaterialComponent >();
}

void StaticMeshSystem::OnComponentConstructed(entt::registry& registry, entt::entity entity)
{
	// auto& mesh = registry.get< StaticMeshComponent >(entity);
	// TODO. set default geometry and material

	Super::OnComponentConstructed(registry, entity);
}

void StaticMeshSystem::OnComponentUpdated(entt::registry& registry, entt::entity entity)
{
    if (registry.any_of< StaticMeshComponent >(entity))
    {
        Super::OnComponentUpdated(registry, entity);
    }
}

void StaticMeshSystem::OnComponentDestroyed(entt::registry& registry, entt::entity entity)
{
	Super::OnComponentDestroyed(registry, entity);
}

std::vector< u64 > StaticMeshSystem::UpdateRenderData(const EditorCamera& edCamera)
{
    UNUSED(edCamera);

    std::vector< u64 > markedEntities;
    for (auto entity : m_DirtyEntities)
    {
        if (!m_Registry.valid(entity))
            continue;

        if (!m_Registry.all_of< TagComponent, StaticMeshComponent >(entity))
            continue;

        u64 id = entt::to_integral(entity);
        MeshRenderDataEntry& entry = m_RenderData[id];

        auto& tagComponent  = m_Registry.get< TagComponent >(entity);
        auto& meshComponent = m_Registry.get< StaticMeshComponent >(entity);
        entry.mesh.id     = id;
        entry.mesh.tag    = tagComponent.tag;
        entry.mesh.aabb   = meshComponent.aabb;
        entry.mesh.sphere = meshComponent.sphere;
        entry.mesh.vData  = meshComponent.pVertices;
        entry.mesh.vCount = meshComponent.numVertices;
        entry.mesh.iData  = meshComponent.pIndices;
        entry.mesh.iCount = meshComponent.numIndices;

        entry.hasMaterial = m_Registry.all_of< MaterialComponent >(entity);
        if (entry.hasMaterial)
        {
            auto& materialComponent = m_Registry.get< MaterialComponent >(entity);
            entry.material.id            = id;
            entry.material.tint          = materialComponent.tint;
            entry.material.ambient       = materialComponent.ambient;
            entry.material.shininess     = materialComponent.shininess;
            entry.material.roughness     = materialComponent.roughness;
            entry.material.metallic      = materialComponent.metallic;
            entry.material.ior           = materialComponent.ior;
            entry.material.emissivePower = materialComponent.emissivePower;
            entry.material.albedoTex     = materialComponent.albedoTex;
            entry.material.normalTex     = materialComponent.normalTex;
            entry.material.aoTex         = materialComponent.aoTex;
            entry.material.roughnessTex  = materialComponent.roughnessTex;
            entry.material.metallicTex   = materialComponent.metallicTex;
            entry.material.emissionTex   = materialComponent.emissionTex;
        }

        markedEntities.emplace_back(id);
    }

    ClearDirtyEntities();
    return markedEntities;
}

void StaticMeshSystem::CollectRenderData(SceneRenderView& outView) const
{
    outView.meshes.reserve(m_RenderData.size());
    outView.materials.reserve(m_RenderData.size());

    for (const auto& [id, entry] : m_RenderData)
    {
        outView.meshes.push_back(entry.mesh);
        u32 meshIndex = static_cast<u32>(outView.meshes.size()) - 1;

        auto& draw = outView.draws[u32(id)];
        draw.mesh  = meshIndex;

        if (entry.hasMaterial)
        {
            outView.materials.push_back(entry.material);
            draw.material = static_cast<u32>(outView.materials.size()) - 1;
        }
    }
}

void StaticMeshSystem::RemoveRenderData(u64 entityId)
{
    m_RenderData.erase(entityId);
}

}