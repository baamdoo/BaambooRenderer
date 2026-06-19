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

    for (auto entity : m_ExpiredEntities)
    {
        RemoveRenderData(entt::to_integral(entity));
    }

    std::vector< u64 > markedEntities;
    for (auto entity : m_DirtyEntities)
    {
        if (!m_Registry.valid(entity))
            continue;

        if (!m_Registry.all_of< TagComponent, StaticMeshComponent >(entity))
            continue;

        u64 id = entt::to_integral(entity);
        MeshRenderDataEntry& entry = m_RenderData[id];

        auto& meshComponent = m_Registry.get< StaticMeshComponent >(entity);
        if (!meshComponent.pVertices || meshComponent.numVertices == 0u ||
            !meshComponent.lods[0].pIndices || meshComponent.lods[0].numIndices == 0u)
        {
            RemoveRenderData(id);
            markedEntities.emplace_back(id);
            continue;
        }

        entry.mesh.id  = id;
        entry.mesh.tag = meshComponent.tag;
        
        entry.mesh.vData  = meshComponent.pVertices;
        entry.mesh.vCount = meshComponent.numVertices;

		entry.mesh.maxLOD = meshComponent.maxLOD;
        for (u8 i = 0; i <= meshComponent.maxLOD; ++i)
        {
            entry.mesh.lods[i].iData  = meshComponent.lods[i].pIndices;
            entry.mesh.lods[i].iCount = meshComponent.lods[i].numIndices;

            entry.mesh.lods[i].mData   = meshComponent.lods[i].pMeshlets;
            entry.mesh.lods[i].mCount  = meshComponent.lods[i].numMeshlets;
            entry.mesh.lods[i].mvData  = meshComponent.lods[i].pMeshletVertices;
            entry.mesh.lods[i].mvCount = meshComponent.lods[i].numMeshletVertices;
            entry.mesh.lods[i].mtData  = meshComponent.lods[i].pMeshletTriangles;
            entry.mesh.lods[i].mtCount = meshComponent.lods[i].numMeshletTriangles;

            entry.mesh.lods[i].simplifyError = meshComponent.lods[i].simplifyError;
        }

        entry.mesh.aabb   = meshComponent.aabb;
        entry.mesh.sphere = meshComponent.sphere;

        entry.bHasMaterial = m_Registry.all_of< MaterialComponent >(entity);
        if (entry.bHasMaterial)
        {
            auto& materialComponent = m_Registry.get< MaterialComponent >(entity);
            entry.material.id            = id;
            entry.material.tint          = materialComponent.tint;
            entry.material.shininess     = materialComponent.shininess;
            entry.material.roughness     = materialComponent.roughness;
            entry.material.metallic      = materialComponent.metallic;
            entry.material.ior           = materialComponent.ior;
            entry.material.emissionColor = materialComponent.emissionColor;
            entry.material.emissivePower = materialComponent.emissivePower;

            entry.material.alphaCutoff        = materialComponent.alphaCutoff;
            entry.material.clearcoat          = materialComponent.clearcoat;
            entry.material.clearcoatRoughness = materialComponent.clearcoatRoughness;
            entry.material.anisotropy         = materialComponent.anisotropy;
            entry.material.anisotropyRotation = materialComponent.anisotropyRotation;
            entry.material.specularColor      = materialComponent.specularColor;
            entry.material.specularStrength   = materialComponent.specularStrength;
            entry.material.sheenColor         = materialComponent.sheenColor;
            entry.material.sheenRoughness     = materialComponent.sheenRoughness;
            entry.material.subsurface         = materialComponent.subsurface;
            entry.material.transmission       = materialComponent.transmission;

            entry.material.albedoTex       = materialComponent.albedoTex;
            entry.material.normalTex       = materialComponent.normalTex;
            entry.material.aoTex           = materialComponent.aoTex;
            entry.material.roughnessTex    = materialComponent.roughnessTex;
            entry.material.metallicTex     = materialComponent.metallicTex;
            entry.material.emissionTex     = materialComponent.emissionTex;
            entry.material.clearcoatTex    = materialComponent.clearcoatTex;
            entry.material.sheenTex        = materialComponent.sheenTex;
            entry.material.anisotropyTex   = materialComponent.anisotropyTex;
            entry.material.subsurfaceTex   = materialComponent.subsurfaceTex;
            entry.material.transmissionTex = materialComponent.transmissionTex;
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

    std::unordered_map< std::string_view, u32 > meshIndexMap;
    std::unordered_map< u64, u32 >              materialIndexMap;

    for (const auto& [id, entry] : m_RenderData)
    {
        u32 meshIndex = kInvalidIndex;
        auto meshIt = meshIndexMap.find(entry.mesh.tag);
        if (meshIt == meshIndexMap.end())
        {
            meshIndex = static_cast<u32>(outView.meshes.size());

            outView.meshes.push_back(entry.mesh);
            meshIndexMap.emplace(entry.mesh.tag, meshIndex);
        }
        else
        {
            meshIndex = meshIt->second;
        }

        u32 materialIndex = kInvalidIndex;
        if (entry.bHasMaterial)
        {
            auto matIt = materialIndexMap.find(entry.material.id);
            if (matIt == materialIndexMap.end())
            {
                materialIndex = static_cast<u32>(outView.materials.size());

                outView.materials.push_back(entry.material);
                materialIndexMap.emplace(entry.material.id, materialIndex);
            }
            else
            {
                materialIndex = matIt->second;
            }
        }

        auto& draw = outView.draws[static_cast<u32>(id)];
        draw.mesh     = meshIndex;
        draw.material = materialIndex;
    }
}

void StaticMeshSystem::RemoveRenderData(u64 entityId)
{
    m_RenderData.erase(entityId);
}

}
