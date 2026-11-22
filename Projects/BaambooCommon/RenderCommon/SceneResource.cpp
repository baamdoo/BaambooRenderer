#include "SceneResource.h"
#include "RenderDevice.h"
#include "CommandContext.h"
#include "Utils/Math.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <utility>

namespace render
{

static constexpr RenderFlags kSceneBufferUsage = eBufferUsage_Storage | eBufferUsage_TransferSource | eBufferUsage_TransferDest;

template< typename... TComponents >
static constexpr u64 ComponentMask(TComponents... components)
{
    return ((1ULL << components) | ...);
}

static constexpr u64 kTransformInputs = ComponentMask(CTransform, CEntity, CVoxelTerrain);
static constexpr u64 kMaterialInputs  = ComponentMask(CMaterial);
static constexpr u64 kMeshInputs      = ComponentMask(CStaticMesh, CVoxelTerrain);
static constexpr u64 kInstanceInputs  = ComponentMask(CEntity, CStaticMesh, CMaterial, CMedium, CVoxelTerrain);
static constexpr u64 kTLASInputs      = kInstanceInputs | ComponentMask(CTransform);
static constexpr u64 kMediumInputs    = kInstanceInputs | ComponentMask(CTransform);
static constexpr u64 kLightInputs     = ComponentMask(CSkyLight, CLocalLight);

static std::string MakeTextureCacheKey(const std::string& filepath, eTextureColorSpace colorSpace)
{
    return filepath + (colorSpace == eTextureColorSpace::SRGB ? "|srgb" : "|linear");
}

static std::string MakeLodCacheKey(const std::string& tag, u32 lod)
{
    return tag + "_LOD" + std::to_string(lod);
}


//-------------------------------------------------------------------------
// Geometry Pool
//-------------------------------------------------------------------------
void SceneResource::GeometryPool::Reserve(RenderDevice& rd, u32 end)
{
    if (!pBuffer)
    {
        capacity = std::max(end, 1u);
        pBuffer  = Buffer::Create(rd, name,
            {
                .count              = capacity,
                .elementSizeInBytes = stride,
                .bufferUsage        = usage,
            });
    }
    else if (end > capacity)
    {
        capacity = end * 2;
        pBuffer->Resize(capacity * stride, false);
    }
}

SceneResource::BufferHandle SceneResource::GeometryPool::Append(RenderDevice& rd, const void* pData, u32 count)
{
    Reserve(rd, used + count);
    if (count > 0)
        rd.GetResourceManager().UploadData(pBuffer, pData, count * stride, ePipelineStage::AllShader, used * stride);

    BufferHandle handle = {};
    handle.offset = used;
    handle.count  = count;

    used += count;
    return handle;
}


//-------------------------------------------------------------------------
// SceneDataBuffer
//-------------------------------------------------------------------------
void SceneResource::SceneDataBuffer::Write(RenderDevice& rd, CommandContext& context, const void* pData, u32 count)
{
    if (!pBuffer)
    {
        capacity = std::max(count, 1u);
        pBuffer  = Buffer::Create(rd, name,
            {
                .count              = capacity,
                .elementSizeInBytes = stride,
                .bufferUsage        = usage,
            });
    }
    else if (count > capacity)
    {
        capacity = count * 2;
        pBuffer->Resize(capacity * stride, true);
    }

    if (count == 0)
        return;

    context.UploadData(pBuffer, pData, count, stride, 0);
    context.TransitionBufferToRead(pBuffer, ePipelineStage::AllShader, 0, true);
}


//-------------------------------------------------------------------------
// Scene Resource
//-------------------------------------------------------------------------
SceneResource::SceneResource(RenderDevice& rd)
    : m_RenderDevice(rd)
    , m_Vertices{ "VertexPool", sizeof(::Vertex), kSceneBufferUsage } // unqualified, Vertex is the 4-byte eShaderStage enumerator in this namespace
    , m_Indices{ "IndexPool", sizeof(Index), eBufferUsage_Index | kSceneBufferUsage }
    , m_Meshlets{ "MeshletPool", sizeof(Meshlet), kSceneBufferUsage }
    , m_MeshletVertices{ "MeshletVertexPool", sizeof(u32), kSceneBufferUsage }
    , m_MeshletTriangles{ "MeshletTrianglePool", sizeof(u32), kSceneBufferUsage }
    , m_MeshData{ "MeshDataPool", sizeof(MeshData), kSceneBufferUsage }
    , m_Instances{ "InstancePool", sizeof(InstanceData), kSceneBufferUsage }
    , m_Transforms{ "TransformPool", sizeof(TransformData), kSceneBufferUsage }
    , m_Materials{ "MaterialPool", sizeof(MaterialData), kSceneBufferUsage }
    , m_MaterialTextures{ "MaterialTexturePool", sizeof(MaterialTextureData), kSceneBufferUsage }
    , m_Media{ "MediumPool", sizeof(MediumData), kSceneBufferUsage }
    , m_MediumDensity{ "MediumDensityPool", sizeof(u32), kSceneBufferUsage }
    , m_Light{ "LightPool", sizeof(LightData), kSceneBufferUsage | eBufferUsage_ShaderDeviceAddress }
{
    for (auto& pGlobals : m_SceneGlobalsBuffer)
    {
        pGlobals = Buffer::Create(m_RenderDevice, "SceneGlobals",
            {
                .count              = 1,
                .elementSizeInBytes = sizeof(SceneGlobals),
                .mapDirection       = 1,
                .bufferUsage        = eBufferUsage_Storage,
            });
    }

    if (m_RenderDevice.GetDeviceSettings().bRaytracing)
        m_pTLAS = TopLevelAccelerationStructure::Create(m_RenderDevice, "SceneTLAS");
}

void SceneResource::UpdateSceneGlobals(const SceneRenderView& sceneView, CommandContext& context)
{
    auto ApplyJittering = [viewport = sceneView.viewport](const mat4& m_, float2 jitter)
        {
            mat4 m = m_;
            m[2][0] += (jitter.x * 2.0f - 1.0f) / viewport.x;
            m[2][1] += (jitter.y * 2.0f - 1.0f) / viewport.y;
            return m;
        };

    // 1) Compute frozen camera first.
    const CameraRenderView& frozenCam = sceneView.bFrozen ? sceneView.frozenCamera : sceneView.camera;
    const float2&           frozenVp  = sceneView.bFrozen ? sceneView.frozenViewport : sceneView.viewport;

    auto& frozenCamera = m_SceneGlobalsData.frozenCamera;
    frozenCamera = {};
    frozenCamera.mView        = frozenCam.mView;
    frozenCamera.mProj        = frozenCam.mProj;
    frozenCamera.mViewProj    = frozenCamera.mProj * frozenCamera.mView;
    frozenCamera.mViewProjInv = glm::inverse(frozenCamera.mViewProj);
    frozenCamera.position     = frozenCam.pos;
    frozenCamera.zNear        = frozenCam.zNear;
    frozenCamera.zFar         = frozenCam.zFar;
    frozenCamera.viewport     = frozenVp;

    // 2) Compute observer with jittering for TAA.
    CameraData camera = {};
    camera.mView = sceneView.camera.mView;
    camera.mProj = sceneView.postProcess.effectBits & (1 << ePostProcess::AntiAliasing) ?
        ApplyJittering(sceneView.camera.mProj, baamboo::math::GetHaltonSequence((u32)context.RenderSequence())) : sceneView.camera.mProj;
    camera.mViewProj               = camera.mProj * camera.mView;
    camera.mViewProjInv            = glm::inverse(camera.mViewProj);
    camera.mViewProjUnjittered     = sceneView.camera.mProj * camera.mView;
    camera.mViewProjUnjitteredPrev =
        m_SceneGlobalsData.camera.mViewProjUnjittered == glm::identity< mat4 >() ? camera.mViewProjUnjittered : m_SceneGlobalsData.camera.mViewProjUnjittered;
    camera.position = sceneView.camera.pos;
    camera.zNear    = sceneView.camera.zNear;
    camera.zFar     = sceneView.camera.zFar;

    // uv-space jitter delta of the final proj vs unjittered proj
    const glm::vec4 cj = camera.mProj * glm::vec4(0.0f, 0.0f, -1.0f, 1.0f);
    const glm::vec4 cu = sceneView.camera.mProj * glm::vec4(0.0f, 0.0f, -1.0f, 1.0f);
    camera.jitterUV = 0.5f * (float2(cj.x / cj.w, cj.y / cj.w) - float2(cu.x / cu.w, cu.y / cu.w));

    m_SceneGlobalsData.camera = camera;

    // 3) CullData — derive frustum planes from the FROZEN camera so mesh frustum cull is consistent with the cluster grid and lighting decisions.
    mat4 mViewProjectionT = glm::transpose(frozenCamera.mViewProj);

    auto& cull = m_SceneGlobalsData.cull;
    cull = {};
    cull.frustum[0] = baamboo::math::NormalizePlane(mViewProjectionT[3] + mViewProjectionT[0]); // w + x < 0
    cull.frustum[1] = baamboo::math::NormalizePlane(mViewProjectionT[3] - mViewProjectionT[0]); // w - x < 0
    cull.frustum[2] = baamboo::math::NormalizePlane(mViewProjectionT[3] + mViewProjectionT[1]); // w + y < 0
    cull.frustum[3] = baamboo::math::NormalizePlane(mViewProjectionT[3] - mViewProjectionT[1]); // w - y < 0
    cull.frustum[4] = baamboo::math::NormalizePlane(mViewProjectionT[3] - mViewProjectionT[2]); // w - z < 0 (reversed-z)
    cull.frustum[5] = float4();                                                                 // z < 0 (reversed-z, infinite far plane)

    cull.sseThresholdPx = sceneView.sseThresholdPx;
    cull.viewportHeight = frozenVp.y;
    cull.cullFlags      = sceneView.cullFlags;
    cull.hiZMipCount    = sceneView.hiZMipCount;
    cull.hiZWidth       = sceneView.hiZWidth;
    cull.hiZHeight      = sceneView.hiZHeight;

    m_SceneGlobalsData.environment =
    {
        .atmosphere = sceneView.atmosphere.data,
        .cloud      = sceneView.cloud.data
    };

    auto& buffers = m_SceneGlobalsData.buffers;
    buffers.vertices         = m_Vertices.pBuffer->GetShaderResourceHandle();
    buffers.indices          = m_Indices.pBuffer->GetShaderResourceHandle();
    buffers.meshlets         = m_Meshlets.pBuffer->GetShaderResourceHandle();
    buffers.meshletVertices  = m_MeshletVertices.pBuffer->GetShaderResourceHandle();
    buffers.meshletTriangles = m_MeshletTriangles.pBuffer->GetShaderResourceHandle();
    buffers.meshes           = m_MeshData.pBuffer->GetShaderResourceHandle();
    buffers.instances        = m_Instances.pBuffer->GetShaderResourceHandle();
    buffers.transforms       = m_Transforms.pBuffer->GetShaderResourceHandle();
    buffers.materials        = m_Materials.pBuffer->GetShaderResourceHandle();
    buffers.materialTextures = m_MaterialTextures.pBuffer->GetShaderResourceHandle();
    buffers.media            = m_Media.pBuffer->GetShaderResourceHandle();
    buffers.mediumDensity    = m_MediumDensity.pBuffer->GetShaderResourceHandle();
    buffers.lights           = m_Light.pBuffer->GetShaderResourceHandle();
    buffers.tlas             = m_pTLAS ? m_pTLAS->GetShaderResourceHandle() : kInvalidIndex;

    m_SceneGlobalsIndex = static_cast< u32 >(context.RenderSequence() % kMaxFramesInFlight);
    const auto& pGlobals = m_SceneGlobalsBuffer[m_SceneGlobalsIndex];
    memcpy(pGlobals->MappedMemory(), &m_SceneGlobalsData, sizeof(SceneGlobals));
    pGlobals->FlushMappedRange(0, sizeof(SceneGlobals));
}

void SceneResource::UpdateSceneResources(const SceneRenderView& sceneView, CommandContext& context)
{
    if (sceneView.sceneRevision == m_LastSceneRevision)
    {
        UpdateSceneGlobals(sceneView, context);
        return;
    }

    const ComponentRevisions& revisions = sceneView.componentRevisions;
    auto IsStale = [&revisions](const ComponentRevisions& built, u64 inputs)
        {
            for (u32 component = 0; component < NumComponents; ++component)
            {
                if ((inputs & (1ULL << component)) != 0 && built[component] != revisions[component])
                    return true;
            }
            return false;
        };

    if (IsStale(m_Transforms.builtRevisions, kTransformInputs))
    {
        WriteTransforms(sceneView, context);
        m_Transforms.builtRevisions = revisions;
    }

    if (IsStale(m_Materials.builtRevisions, kMaterialInputs))
    {
        WriteMaterials(sceneView, context);
        m_Materials.builtRevisions        = revisions;
        m_MaterialTextures.builtRevisions = revisions;
    }

    if (IsStale(m_MeshData.builtRevisions, kMeshInputs))
    {
        WriteMeshes(sceneView, context);
        m_MeshData.builtRevisions = revisions;
    }

    if (IsStale(m_Instances.builtRevisions, kInstanceInputs))
    {
        WriteInstances(sceneView, context);
        m_Instances.builtRevisions = revisions;
    }

    if (m_RenderDevice.GetDeviceSettings().bRaytracing && IsStale(m_TLASBuiltRevisions, kTLASInputs))
    {
        BuildTLAS(sceneView);
        m_TLASBuiltRevisions = revisions;
    }

    if (IsStale(m_Media.builtRevisions, kMediumInputs))
    {
        WriteMedia(sceneView, context);
        m_Media.builtRevisions         = revisions;
        m_MediumDensity.builtRevisions = revisions;
    }

    if (IsStale(m_Light.builtRevisions, kLightInputs))
    {
        m_Light.Write(m_RenderDevice, context, &sceneView.light, 1);
        m_Light.builtRevisions = revisions;
    }

    UpdateSceneGlobals(sceneView, context);
    m_LastSceneRevision = sceneView.sceneRevision;
}

void SceneResource::WriteTransforms(const SceneRenderView& sceneView, CommandContext& context)
{
    std::vector< TransformData > transforms;
    transforms.reserve(sceneView.transforms.size());
    for (auto& transformView : sceneView.transforms)
    {
        TransformData transform = {};
        transform.mLocalToWorld = transformView.mWorld;
        transform.mWorldToLocal = transformView.mWorldInverse;
        transforms.push_back(transform);
    }
    if (sceneView.voxelTerrain.bValid)
    {
        TransformData voxelTransform = {};
        voxelTransform.mLocalToWorld = mat4(1.0f);
        voxelTransform.mWorldToLocal = mat4(1.0f);
        transforms.push_back(voxelTransform);
    }
    m_Transforms.Write(m_RenderDevice, context, transforms.data(), (u32)transforms.size());
}

void SceneResource::WriteMaterials(const SceneRenderView& sceneView, CommandContext& context)
{
    std::vector< MaterialData > materials;
    materials.reserve(sceneView.materials.size());

    u64 materialTextureCount = 0;
    for (const auto& materialView : sceneView.materials)
        materialTextureCount += materialView.textures.size();

    std::vector< MaterialTextureData > materialTextures;
    materialTextures.reserve(materialTextureCount);

    for (auto& materialView : sceneView.materials)
    {
        MaterialData material = {};
        material.tint          = materialView.tint;
        material.roughness     = materialView.roughness;
        material.metallic      = materialView.metallic;
        material.ior           = materialView.ior;
        material.emissionColor = materialView.emissionColor;
        material.emissivePower = materialView.emissivePower;

        material.alphaCutoff        = materialView.alphaCutoff;
        material.opacity            = materialView.opacity;
        material.clearcoat          = materialView.clearcoat;
        material.clearcoatRoughness = materialView.clearcoatRoughness;
        material.anisotropy         = materialView.anisotropy;
        material.anisotropyRotation = materialView.anisotropyRotation;
        material.specularColor      = materialView.specularColor;
        material.specularStrength   = materialView.specularStrength;
        material.sheenColor         = materialView.sheenColor;
        material.sheenRoughness     = materialView.sheenRoughness;
        material.subsurface         = materialView.subsurface;
        material.transmission       = materialView.transmission;
        material.materialType       = materialView.materialType;
        material.materialFlags      = materialView.materialFlags;
        material.textureOffset      = static_cast<u32>(materialTextures.size());
        material.layerOffset        = materialView.layerOffset;
        material.layerCount         = materialView.layerCount;

        for (const auto& textureView : materialView.textures)
        {
            BB_ASSERT(textureView.semantic <= eMaterialTextureSemantic_Transmission, "Invalid material texture semantic: %u", textureView.semantic);
            BB_ASSERT(textureView.channel <= eMaterialTextureChannel_RGBA, "Invalid material texture channel: %u", textureView.channel);
            BB_ASSERT(textureView.colorSpace <= eMaterialTextureColorSpace_SRGB, "Invalid material texture color space: %u", textureView.colorSpace);

            const auto colorSpace = textureView.colorSpace == eMaterialTextureColorSpace_SRGB
                ? eTextureColorSpace::SRGB
                : eTextureColorSpace::Linear;
            auto pMaterialTex = GetOrLoadTexture(textureView.filepath, colorSpace);
            if (!pMaterialTex)
                continue;

            materialTextures.push_back({
                .textureID = pMaterialTex->GetShaderResourceHandle(),
                .semantic  = textureView.semantic,
                .channel   = textureView.channel,
                .padding0  = 0u
            });
        }
        material.textureCount = static_cast<u32>(materialTextures.size()) - material.textureOffset;

        materials.push_back(material);
    }
    m_Materials.Write(m_RenderDevice, context, materials.data(), (u32)materials.size());
    m_MaterialTextures.Write(m_RenderDevice, context, materialTextures.data(), (u32)materialTextures.size());
}

void SceneResource::WriteMeshes(const SceneRenderView& sceneView, CommandContext& context)
{
    ReserveGeometry(sceneView);

    m_MeshRows.clear();
    for (const auto& meshView : sceneView.meshes)
    {
        m_MeshRows.push_back(GetOrUploadMesh(meshView));
    }
    if (sceneView.voxelTerrain.bValid)
    {
        const float half = sceneView.voxelTerrain.chunkWorldSizeMeter * 0.5f;

        MeshData voxelMesh = {};
        voxelMesh.vOffset = 0;
        voxelMesh.maxLOD  = 0;
        voxelMesh.center  = float3(half);      // chunk-local until the patch CS writes world center/radius per slot
        voxelMesh.radius  = half * 1.7320508f; // cube half-diagonal
        // offsets/counts are filled by the voxel patch CS
        voxelMesh.lods[0].mCount   = 0;
        voxelMesh.lods[0].mOffset  = 0;
        voxelMesh.lods[0].mvOffset = 0;
        voxelMesh.lods[0].mtOffset = 0;
        for (u32 i = 0; i < kMaxVoxelChunkSlots; ++i)
            m_MeshRows.push_back(voxelMesh);
    }
    m_MeshData.Write(m_RenderDevice, context, m_MeshRows.data(), (u32)m_MeshRows.size());
    m_SceneGlobalsData.buffers.voxelMeshBase = (u32)sceneView.meshes.size();
}

void SceneResource::WriteInstances(const SceneRenderView& sceneView, CommandContext& context)
{
    const u32 numVoxelSlots = sceneView.voxelTerrain.bValid ? kMaxVoxelChunkSlots : 0u;

    m_NumMeshletVisibilitySlots = 0;

    std::vector< InstanceData > instances;
    for (auto& [id, data] : sceneView.draws)
    {
        UNUSED(id);
        if (!IsValidIndex(data.mesh))
            continue;

        assert(data.mesh < sceneView.meshes.size());
        auto& meshView = sceneView.meshes[data.mesh];

        // Medium and TLAS instance IDs share this index, offset by the prepended voxel slots.
        const u32 instanceIndex = numVoxelSlots + static_cast<u32>(instances.size());

        InstanceData instance = {};
        instance.meshID = data.mesh;

        assert(IsValidIndex(data.transform) && data.transform < sceneView.transforms.size());
        instance.transformID = data.transform;

        instance.materialID = kInvalidIndex;
        instance.mediumID   = kInvalidIndex;
        if (IsValidIndex(data.material))
        {
            assert(data.material < sceneView.materials.size());
            instance.materialID = data.material;
            instance.mediumID   = instanceIndex;
        }

        instance.visOffset = m_NumMeshletVisibilitySlots;

        u32 maxLodMeshletCount = 0;
        for (u8 i = 0; i <= meshView.maxLOD; ++i)
            maxLodMeshletCount = std::max(maxLodMeshletCount, m_MeshRows[data.mesh].lods[i].mCount);
        m_NumMeshletVisibilitySlots += maxLodMeshletCount;

        instances.push_back(instance);
    }
    // Voxel chunk slots: prepend at the head of instance buffer (i-th chunk lives at instance base+i)
    if (sceneView.voxelTerrain.bValid)
    {
        const u32 voxelMeshBase    = (u32)sceneView.meshes.size();
        const u32 voxelTransformID = (u32)sceneView.transforms.size();

        std::vector< InstanceData > voxelInstances(kMaxVoxelChunkSlots);
        for (u32 i = 0; i < kMaxVoxelChunkSlots; ++i)
        {
            voxelInstances[i].meshID      = voxelMeshBase + i;
            voxelInstances[i].transformID = voxelTransformID;
            voxelInstances[i].materialID  = kInvalidIndex;
            voxelInstances[i].mediumID    = kInvalidIndex;
            voxelInstances[i].visOffset   = 0; // unused: voxel skips per-meshlet cull
            voxelInstances[i].isVoxel     = 1;
        }
        instances.insert(instances.begin() + kVoxelChunkInstanceBase, voxelInstances.begin(), voxelInstances.end());
    }
    m_SceneGlobalsData.buffers.instanceCount = (u32)instances.size();
    m_Instances.Write(m_RenderDevice, context, instances.data(), (u32)instances.size());
}

void SceneResource::BuildTLAS(const SceneRenderView& sceneView)
{
    m_pTLAS->Reset();
    m_PendingBLASBuilds.clear();

    // Same order and offset as the instance rows.
    u32 instanceIndex = sceneView.voxelTerrain.bValid ? kMaxVoxelChunkSlots : 0u;
    for (auto& [id, data] : sceneView.draws)
    {
        UNUSED(id);
        if (!IsValidIndex(data.mesh))
            continue;

        const mat4& m = sceneView.transforms[data.transform].mWorld;

        // glm::mat4 (column-major) => 3x4 row-major
        AccelerationStructureInstanceDesc inst = {};
        inst.transform[0][0] = m[0][0]; inst.transform[0][1] = m[1][0]; inst.transform[0][2] = m[2][0]; inst.transform[0][3] = m[3][0];
        inst.transform[1][0] = m[0][1]; inst.transform[1][1] = m[1][1]; inst.transform[1][2] = m[2][1]; inst.transform[1][3] = m[3][1];
        inst.transform[2][0] = m[0][2]; inst.transform[2][1] = m[1][2]; inst.transform[2][2] = m[2][2]; inst.transform[2][3] = m[3][2];

        inst.instanceID                          = instanceIndex++;
        inst.pBLAS                               = GetOrCreateBLAS(sceneView.meshes[data.mesh]).get();
        inst.instanceContributionToHitGroupIndex = 0;
        if (IsValidIndex(data.material))
        {
            const u32 materialFlags = sceneView.materials[data.material].materialFlags;
            if ((materialFlags & (MATERIAL_FLAG_ALPHA_MASK | MATERIAL_FLAG_ALPHA_BLEND)) != 0u)
            {
                inst.flags = eASInstanceFlag_ForceNonOpaque;
            }
        }

        m_pTLAS->AddInstance(inst);
    }

    if (m_pTLAS->NumInstances() > 0)
    {
        m_pTLAS->Prepare();
    }
    m_RenderDevice.GetResourceManager().BuildAccelerationStructures(m_PendingBLASBuilds, m_pTLAS.get());
    m_PendingBLASBuilds.clear();
}

void SceneResource::WriteMedia(const SceneRenderView& sceneView, CommandContext& context)
{
    const auto mediumBuffers = BuildMediumBuffers(sceneView);
    m_Media.Write(m_RenderDevice, context, mediumBuffers.media.data(), static_cast<u32>(mediumBuffers.media.size()));
    m_MediumDensity.Write(m_RenderDevice, context, mediumBuffers.densityWords.data(), static_cast<u32>(mediumBuffers.densityWords.size()));
    m_SceneGlobalsData.buffers.mediumCount = static_cast<u32>(mediumBuffers.media.size());
}

void SceneResource::ReserveGeometry(const SceneRenderView& sceneView)
{
    u32 vertexEnd          = m_Vertices.used;
    u32 indexEnd           = m_Indices.used;
    u32 meshletEnd         = m_Meshlets.used;
    u32 meshletVertexEnd   = m_MeshletVertices.used;
    u32 meshletTriangleEnd = m_MeshletTriangles.used;
    for (const auto& meshView : sceneView.meshes)
    {
        if (!m_VertexCache.contains(meshView.tag))
            vertexEnd += meshView.vCount;

        for (u8 i = 0; i <= meshView.maxLOD; ++i)
        {
            if (m_LodCache.contains(MakeLodCacheKey(meshView.tag, i)))
                continue;

            const auto& lodView = meshView.lods[i];
            indexEnd           += lodView.iCount;
            meshletEnd         += lodView.mCount;
            meshletVertexEnd   += lodView.mvCount;
            meshletTriangleEnd += lodView.mtCount;
        }
    }

    m_Vertices.Reserve(m_RenderDevice, vertexEnd);
    m_Indices.Reserve(m_RenderDevice, indexEnd);
    m_Meshlets.Reserve(m_RenderDevice, meshletEnd);
    m_MeshletVertices.Reserve(m_RenderDevice, meshletVertexEnd);
    m_MeshletTriangles.Reserve(m_RenderDevice, meshletTriangleEnd);
}

MeshData SceneResource::GetOrUploadMesh(const StaticMeshRenderView& meshView)
{
    auto vIter = m_VertexCache.find(meshView.tag);
    if (vIter == m_VertexCache.end())
        vIter = m_VertexCache.emplace(meshView.tag, m_Vertices.Append(m_RenderDevice, meshView.vData, meshView.vCount)).first;

    MeshData mesh = {};
    mesh.vOffset = vIter->second.offset;
    mesh.maxLOD  = meshView.maxLOD;

    for (u8 i = 0; i <= meshView.maxLOD; ++i)
    {
        const auto& lodView = meshView.lods[i];

        const std::string key = MakeLodCacheKey(meshView.tag, i);
        auto lIter = m_LodCache.find(key);
        if (lIter == m_LodCache.end())
        {
            LodHandles handles = {};
            handles.indices          = m_Indices.Append(m_RenderDevice, lodView.iData, lodView.iCount);
            handles.meshlets         = m_Meshlets.Append(m_RenderDevice, lodView.mData, lodView.mCount);
            handles.meshletVertices  = m_MeshletVertices.Append(m_RenderDevice, lodView.mvData, lodView.mvCount);
            handles.meshletTriangles = m_MeshletTriangles.Append(m_RenderDevice, lodView.mtData, lodView.mtCount);
            lIter = m_LodCache.emplace(key, handles).first;
        }
        const LodHandles& handles = lIter->second;

        mesh.lods[i].iOffset = handles.indices.offset;

        mesh.lods[i].mCount   = handles.meshlets.count;
        mesh.lods[i].mOffset  = handles.meshlets.offset;
        mesh.lods[i].mvOffset = handles.meshletVertices.offset;
        mesh.lods[i].mtOffset = handles.meshletTriangles.offset;

        mesh.lods[i].simplifyError = lodView.simplifyError;
    }

    mesh.center = meshView.sphere.Center();
    mesh.radius = meshView.sphere.Radius();
    return mesh;
}

Arc< BottomLevelAccelerationStructure > SceneResource::GetOrCreateBLAS(const StaticMeshRenderView& meshView)
{
    auto iter = m_BLASCache.find(meshView.tag);
    if (iter != m_BLASCache.end())
        return iter->second;

    const BufferHandle& vertices = m_VertexCache.at(meshView.tag);
    const BufferHandle& indices  = m_LodCache.at(MakeLodCacheKey(meshView.tag, 0)).indices;

    auto pBLAS = BottomLevelAccelerationStructure::Create(m_RenderDevice, meshView.tag.c_str());

    GeometryDesc geom = {};
    geom.vertexBufferAddress = m_Vertices.pBuffer->GetGPUVirtualAddress() + vertices.offset * m_Vertices.stride;
    geom.vertexCount         = vertices.count;
    geom.vertexStride        = static_cast<u32>(m_Vertices.stride);
    geom.indexBufferAddress  = m_Indices.pBuffer->GetGPUVirtualAddress() + indices.offset * m_Indices.stride;
    geom.indexCount          = indices.count;
    geom.geometryFlags       = eGeometryFlag_Opaque;
    pBLAS->AddGeometry(geom);
    pBLAS->Prepare();

    m_BLASCache.emplace(meshView.tag, pBLAS);
    m_PendingBLASBuilds.push_back(pBLAS.get());
    return pBLAS;
}

Arc< Texture > SceneResource::GetOrLoadTexture(const std::string& filepath, eTextureColorSpace colorSpace)
{
    const std::string cacheKey = MakeTextureCacheKey(filepath, colorSpace);
    auto iter = m_TextureCache.find(cacheKey);
    if (iter != m_TextureCache.end())
        return iter->second;

    auto pTexture = m_RenderDevice.GetResourceManager().LoadTexture(filepath, true, colorSpace);

    m_TextureCache.emplace(cacheKey, pTexture);
    return pTexture;
}

// The project uses /fp:fast; preserve the outward-rounding operations below.
#ifdef _MSC_VER
#pragma float_control(precise, on, push)
#endif
SceneResource::MediumBuffers SceneResource::BuildMediumBuffers(const SceneRenderView& renderView)
{
    MediumBuffers buffers;
    buffers.media.resize(renderView.voxelTerrain.bValid ? kMaxVoxelChunkSlots : 0u);

    std::unordered_map< const MediumDensityAsset*, u32 > assetOffsets;
    for (const auto& [entity, draw] : renderView.draws)
    {
        UNUSED(entity);
        if (!IsValidIndex(draw.mesh))
            continue;

        MediumData data = {};
        if (IsValidIndex(draw.medium))
        {
            BB_ASSERT(draw.medium < renderView.media.size(), "Medium render-view index is out of range");
            BB_ASSERT(draw.transform < renderView.transforms.size(), "Medium transform index is out of range");

            const auto& medium = renderView.media[draw.medium];
            data = medium.data;

            data.mWorldToDensity    = renderView.transforms[draw.transform].mWorldInverse;
            data.densityByteOffset  = kInvalidIndex;

            if (medium.pDensity)
            {
                const auto* pAsset = medium.pDensity.get();
                data.mWorldToDensity = pAsset->mLocalToIndex * data.mWorldToDensity;
                auto assetIt = assetOffsets.find(pAsset);
                if (assetIt == assetOffsets.end())
                {
                    BB_ASSERT(!pAsset->words.empty() && pAsset->words.size() % 8u == 0u, "Density payload must have 32-byte alignment");

                    const u64 totalWords = static_cast<u64>(buffers.densityWords.size()) + pAsset->words.size();
                    BB_ASSERT(totalWords <= std::numeric_limits<u32>::max() / sizeof(u32),
                        "Density pool must fit its 32-bit byte offsets");

                    const u32 offset = static_cast<u32>(buffers.densityWords.size() * sizeof(u32));
                    buffers.densityWords.insert(buffers.densityWords.end(), pAsset->words.begin(), pAsset->words.end());
                    assetIt = assetOffsets.emplace(pAsset, offset).first;
                }
                data.densityByteOffset = assetIt->second;
            }

            const auto roundUp = [](double value)
            {
                float bound = static_cast<float>(value);
                if (static_cast<double>(bound) < value)
                    bound = std::nextafter(bound, std::numeric_limits<float>::infinity());

                BB_ASSERT(std::isfinite(bound), "Medium bound exceeds finite float32 range");
                return bound;
            };
            const float maxDensity = medium.pDensity ? medium.pDensity->maxDensity : 1.0f;

            const float densityBound = roundUp(static_cast<double>(maxDensity) * data.densityScale);
            data.majorant = 0.0f;
            for (u32 axis = 0; axis < 3; ++axis)
            {
                const float sigmaA = roundUp(static_cast<double>(data.sigmaA[axis]) * densityBound);
                const float sigmaS = roundUp(static_cast<double>(data.sigmaS[axis]) * densityBound);
                data.majorant = std::max(data.majorant, roundUp(static_cast<double>(sigmaA) + sigmaS));
            }

            const float determinant = glm::determinant(mat3(data.mWorldToDensity));
            BB_ASSERT(std::isfinite(determinant) && determinant != 0.0f &&
                data.mWorldToDensity[0][3] == 0.0f && data.mWorldToDensity[1][3] == 0.0f &&
                data.mWorldToDensity[2][3] == 0.0f && data.mWorldToDensity[3][3] == 1.0f,
                "Medium transform must be affine and invertible");
        }

        // Pure boundaries inherit the incident IOR during traversal
        data.ior = 1.0f;
        if (IsValidIndex(draw.material))
        {
            BB_ASSERT(draw.material < renderView.materials.size(), "Medium boundary material index is out of range");

            const auto& root = renderView.materials[draw.material];
            const bool bMediumBoundary = (root.materialFlags & MATERIAL_FLAG_MEDIUM_BOUNDARY) != 0u;

            u32 terminalMaterial = draw.material;
            if (root.layerCount > 1u)
            {
                const u64 terminal = static_cast<u64>(root.layerOffset) + root.layerCount - 1u;
                terminalMaterial = renderView.materialSlabs[terminal].materialID;
            }
            BB_ASSERT(terminalMaterial < renderView.materials.size(), "Medium terminal material is out of range");
            const float ior = renderView.materials[terminalMaterial].ior;
            if (!bMediumBoundary)
            {
                data.ior = std::max(ior, 1.0e-4f);
            }
        }
        buffers.media.push_back(data);
    }

    // Bind nonempty buffers even when the scene has no media or density grids.
    if (buffers.media.empty())
        buffers.media.emplace_back();
    if (buffers.densityWords.empty())
        buffers.densityWords.push_back(0u);
    return buffers;
}
#ifdef _MSC_VER
#pragma float_control(pop)
#endif

} // namespace render
