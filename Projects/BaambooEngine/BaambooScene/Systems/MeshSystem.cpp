#include "BaambooPch.h"
#include "MeshSystem.h"

namespace baamboo
{

namespace
{

MaterialRenderView MakeMaterialRenderView(
    const MaterialData& material,
    u64                 id,
    bool                bFaceNormals)
{
    MaterialRenderView view = {};
    view.id            = id;
    view.tint          = float3(material.tint);
    view.roughness     = material.roughness;
    view.metallic      = material.metallic;
    view.ior           = material.ior;
    view.emissionColor = material.emissionColor;
    view.emissivePower = material.emissivePower;

    view.alphaCutoff        = material.alphaCutoff;
    view.clearcoat          = material.clearcoat;
    view.clearcoatRoughness = material.clearcoatRoughness;
    view.anisotropy         = material.anisotropy;
    view.anisotropyRotation = material.anisotropyRotation;
    view.specularColor      = material.specularColor;
    view.specularStrength   = material.specularStrength;
    view.sheenColor         = material.sheenColor;
    view.sheenRoughness     = material.sheenRoughness;
    view.subsurface         = material.subsurface;
    view.transmission       = material.transmission;
    view.materialType       = material.materialType;
    view.materialFlags      = bFaceNormals ? MATERIAL_FLAG_FACE_NORMALS : 0u;

    view.albedoTex       = material.albedoTex;
    view.normalTex       = material.normalTex;
    view.aoTex           = material.aoTex;
    view.roughnessTex    = material.roughnessTex;
    view.metallicTex     = material.metallicTex;
    view.emissionTex     = material.emissionTex;
    view.clearcoatTex    = material.clearcoatTex;
    view.sheenTex        = material.sheenTex;
    view.anisotropyTex   = material.anisotropyTex;
    view.subsurfaceTex   = material.subsurfaceTex;
    view.transmissionTex = material.transmissionTex;
    return view;
}

MaterialSlabData MakeMaterialSlabData(const MaterialLayer& layer)
{
    MaterialSlabData data = {};
    data.materialID = kInvalidIndex;
    data.thickness  = std::max(layer.thickness, 0.0f);
    data.phaseG     = std::clamp(layer.phaseG, -0.999f, 0.999f);
    data.sigmaA     = float3(
        std::max(layer.sigmaA.x, 0.0f),
        std::max(layer.sigmaA.y, 0.0f),
        std::max(layer.sigmaA.z, 0.0f));
    data.sigmaS = float3(
        std::max(layer.sigmaS.x, 0.0f),
        std::max(layer.sigmaS.y, 0.0f),
        std::max(layer.sigmaS.z, 0.0f));
    return data;
}

} // namespace

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
    m_ExpiredEntities.clear();

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
        entry.materials.clear();
        entry.materialSlabs.clear();
        if (entry.bHasMaterial)
        {
            const auto& materialComponent = m_Registry.get< MaterialComponent >(entity);
            BB_ASSERT(!materialComponent.layers.empty(), "MaterialComponent must contain at least one MaterialLayer");

            entry.bHasMaterial = !materialComponent.layers.empty();
            entry.materials.reserve(materialComponent.layers.size());
            entry.materialSlabs.reserve(materialComponent.layers.size());
            for (const auto& layer : materialComponent.layers)
            {
                entry.materials.push_back(
                    MakeMaterialRenderView(layer.material, id, materialComponent.bFaceNormals));
                entry.materialSlabs.push_back(MakeMaterialSlabData(layer));
            }
        }

        markedEntities.emplace_back(id);
    }

    ClearDirtyEntities();
    return markedEntities;
}

void StaticMeshSystem::CollectRenderData(SceneRenderView& outView) const
{
    outView.meshes.reserve(m_RenderData.size());

    size_t materialRecordCount = 0;
    for (const auto& [id, entry] : m_RenderData)
    {
        UNUSED(id);
        materialRecordCount += entry.materials.size();
    }
    outView.materials.reserve(outView.materials.size() + materialRecordCount);
    outView.materialSlabs.reserve(outView.materialSlabs.size() + materialRecordCount);

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
            BB_ASSERT(!entry.materials.empty(), "Material render stack must contain at least one closure");
            BB_ASSERT(entry.materials.size() == entry.materialSlabs.size(), "Material closure/slab counts must match");

            auto matIt = materialIndexMap.find(id);
            if (matIt == materialIndexMap.end())
            {
                materialIndex = static_cast<u32>(outView.materials.size());
                const u32 layerOffset = static_cast<u32>(outView.materialSlabs.size());
                const u32 layerCount  = static_cast<u32>(entry.materials.size());

                for (u32 layerIndex = 0; layerIndex < layerCount; ++layerIndex)
                {
                    MaterialRenderView material = entry.materials[layerIndex];
                    material.layerOffset = layerIndex == 0u ? layerOffset : kInvalidIndex;
                    material.layerCount  = layerIndex == 0u ? layerCount : 0u;

                    MaterialSlabData slab = entry.materialSlabs[layerIndex];
                    slab.materialID = static_cast<u32>(outView.materials.size());

                    outView.materials.push_back(std::move(material));
                    outView.materialSlabs.push_back(slab);
                }
                materialIndexMap.emplace(id, materialIndex);
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
