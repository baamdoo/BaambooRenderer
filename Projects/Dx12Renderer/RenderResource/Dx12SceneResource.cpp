#include "RendererPch.h"
#include "Dx12SceneResource.h"
#include "Dx12Buffer.h"
#include "Dx12Texture.h"
#include "Dx12AccelerationStructure.h"

#include "RenderDevice/Dx12CommandContext.h"
#include "RenderDevice/Dx12Rootsignature.h"
#include "RenderDevice/Dx12CommandSignature.h"
#include "RenderDevice/Dx12BufferAllocator.h"
#include "RenderDevice/Dx12ResourceManager.h"

#include "SceneRenderView.h"
#include "Utils/Math.hpp"

namespace dx12
{

static std::string MakeTextureCacheKey(const std::string& filepath, render::eTextureColorSpace colorSpace)
{
    return filepath + (colorSpace == render::eTextureColorSpace::SRGB ? "|srgb" : "|linear");
}

void Dx12SceneResource::PerFrameData::Reset()
{
    if (pMeshDataAllocator)
        pMeshDataAllocator->Reset();
    if (pInstanceAllocator)
        pInstanceAllocator->Reset();

    if (pTransformAllocator)
        pTransformAllocator->Reset();
    if (pMaterialAllocator)
        pMaterialAllocator->Reset();
    if (pMaterialTextureAllocator)
        pMaterialTextureAllocator->Reset();
    if (pLightAllocator)
        pLightAllocator->Reset();
}

Dx12SceneResource::Dx12SceneResource(Dx12RenderDevice& rd)
    : m_RenderDevice(rd)
{
    // **
    // scene buffers
    // **

    m_pVertexAllocator          = MakeBox< StaticBufferAllocator >(m_RenderDevice, "VertexPool", sizeof(Vertex), _MB(8LL));
    m_pIndexAllocator           = MakeBox< StaticBufferAllocator >(m_RenderDevice, "IndexPool", sizeof(u32), _MB(8LL));
    m_pMeshletAllocator         = MakeBox< StaticBufferAllocator >(m_RenderDevice, "MeshletPool", sizeof(Meshlet), _MB(8LL));
    m_pMeshletVertexAllocator   = MakeBox< StaticBufferAllocator >(m_RenderDevice, "MeshletVertexPool", sizeof(u32), _MB(8LL));
    m_pMeshletTriangleAllocator = MakeBox< StaticBufferAllocator >(m_RenderDevice, "MeshletTrianglePool", sizeof(u32), _MB(8LL));

    for (auto& frameData : m_FrameData)
    {
        frameData.pMeshDataAllocator        = MakeBox< StaticBufferAllocator >(m_RenderDevice, "MeshDataPool", sizeof(MeshData), _KB(1LL), render::eBufferUsage_Storage); // Storage/UAV: voxel mCount patch CS writes lods[0].mCount
        frameData.pInstanceAllocator        = MakeBox< StaticBufferAllocator >(m_RenderDevice, "InstancePool", sizeof(InstanceData), kMaxEntityCount);

        frameData.pTransformAllocator       = MakeBox< StaticBufferAllocator >(m_RenderDevice, "TransformPool", sizeof(TransformData), kMaxEntityCount);
        frameData.pMaterialAllocator        = MakeBox< StaticBufferAllocator >(m_RenderDevice, "MaterialPool", sizeof(MaterialData), kMaxEntityCount);
        frameData.pMaterialTextureAllocator = MakeBox< StaticBufferAllocator >(m_RenderDevice, "MaterialTexturePool", sizeof(MaterialTextureData), kMaxEntityCount);
        frameData.pLightAllocator           = MakeBox< StaticBufferAllocator >(m_RenderDevice, "LightPool", sizeof(LightData), 1);

        frameData.pCameraBuffer           = Dx12ConstantBuffer::Create(m_RenderDevice, "CameraBuffer", sizeof(CameraData));
        frameData.pCullBuffer             = Dx12ConstantBuffer::Create(m_RenderDevice, "CullBuffer", sizeof(CullData));
        frameData.pSceneEnvironmentBuffer = Dx12ConstantBuffer::Create(m_RenderDevice, "SceneEnvironmentBuffer", sizeof(SceneEnvironmentData));
        frameData.pFrozenCameraBuffer     = Dx12ConstantBuffer::Create(m_RenderDevice, "FrozenCameraBuffer", sizeof(FrozenCameraData));
        frameData.pMeshStreamsBuffer      = Dx12ConstantBuffer::Create(m_RenderDevice, "MeshStreamsCBV", sizeof(u32) * 5);
    }

    m_pTLAS = Dx12TopLevelAS::Create(m_RenderDevice, "SceneTLAS");

    // **
    // command signature
    // **
    auto& rm = static_cast<Dx12ResourceManager&>(m_RenderDevice.GetResourceManager());
    m_pRootSignature = rm.GetGlobalRootSignature();

    if (!m_RenderDevice.GetDeviceSettings().bMeshShader)
    {
        __debugbreak();

        m_pIndirectDrawSignature = new CommandSignature(
            m_RenderDevice,
            CommandSignatureDesc(5, sizeof(IndirectDrawData))
                .AddConstant(0, 0, 1)
                .AddConstant(0, 1, 1)
                .AddVertexBufferView(0)
                .AddIndexBufferView()
                .AddDrawIndexed(),
            m_pRootSignature->GetD3D12RootSignature());
    }
    else
    {
        m_pIndirectDispatchSignature = new CommandSignature(
            m_RenderDevice,
            CommandSignatureDesc(2, sizeof(IndirectCommandData))
                .AddConstant(0, 0, 1)
                .AddDispatchMesh(),
            m_pRootSignature->GetD3D12RootSignature()
        );
    }
}

Dx12SceneResource::~Dx12SceneResource()
{
    RELEASE(m_pIndirectDrawSignature);
    RELEASE(m_pIndirectDispatchSignature);
}

void Dx12SceneResource::UpdateCameraAndEnvironment(const SceneRenderView& sceneView, Dx12CommandContext& ctx)
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

    FrozenCameraData frozen = {};
    frozen.mView        = frozenCam.mView;
    frozen.mProj        = frozenCam.mProj;
    frozen.mViewProj    = frozen.mProj * frozen.mView;
    frozen.mViewProjInv = glm::inverse(frozen.mViewProj);
    frozen.position     = frozenCam.pos;
    frozen.zNear        = frozenCam.zNear;
    frozen.zFar         = frozenCam.zFar;
    frozen.viewport     = frozenVp;
    memcpy(m_FrameData[m_ContextIndex].pFrozenCameraBuffer->MappedMemory(), &frozen, sizeof(frozen));

    // 2) Compute observer with jittering for TAA.
    CameraData camera = {};
    camera.mView = sceneView.camera.mView;
    camera.mProj = sceneView.postProcess.effectBits & (1 << ePostProcess::AntiAliasing) ?
        ApplyJittering(sceneView.camera.mProj, baamboo::math::GetHaltonSequence((u32)ctx.RenderSequence())) : sceneView.camera.mProj;
    camera.mViewProj               = camera.mProj * camera.mView;
    camera.mViewProjInv            = glm::inverse(camera.mViewProj);
    camera.mViewProjUnjittered     = sceneView.camera.mProj * camera.mView;
    camera.mViewProjUnjitteredPrev =
        m_CameraCache.mViewProjUnjittered == glm::identity< mat4 >() ? camera.mViewProjUnjittered : m_CameraCache.mViewProjUnjittered;
    camera.position = sceneView.camera.pos;
    camera.zNear    = sceneView.camera.zNear;
    camera.zFar     = sceneView.camera.zFar;

    // uv-space jitter delta of the final proj vs unjittered proj
    const glm::vec4 cj = camera.mProj * glm::vec4(0.0f, 0.0f, -1.0f, 1.0f);
    const glm::vec4 cu = sceneView.camera.mProj * glm::vec4(0.0f, 0.0f, -1.0f, 1.0f);
    camera.jitterUV = 0.5f * (float2(cj.x / cj.w, cj.y / cj.w) - float2(cu.x / cu.w, cu.y / cu.w));

    m_CameraCache   = std::move(camera);
    memcpy(m_FrameData[m_ContextIndex].pCameraBuffer->MappedMemory(), &m_CameraCache, sizeof(m_CameraCache));

    // 3) CullData — derive frustum planes from the FROZEN camera so mesh frustum cull is consistent with the cluster grid and lighting decisions.
    mat4 mViewProjectionT = glm::transpose(frozen.mViewProj);

    m_CullData = {};
    m_CullData.frustum[0] = baamboo::math::NormalizePlane(mViewProjectionT[3] + mViewProjectionT[0]); // w + x < 0
    m_CullData.frustum[1] = baamboo::math::NormalizePlane(mViewProjectionT[3] - mViewProjectionT[0]); // w - x < 0
    m_CullData.frustum[2] = baamboo::math::NormalizePlane(mViewProjectionT[3] + mViewProjectionT[1]); // w + y < 0
    m_CullData.frustum[3] = baamboo::math::NormalizePlane(mViewProjectionT[3] - mViewProjectionT[1]); // w - y < 0
    m_CullData.frustum[4] = baamboo::math::NormalizePlane(mViewProjectionT[3] - mViewProjectionT[2]); // w - z < 0 (reversed-z)
    m_CullData.frustum[5] = float4();                                                                 // z < 0 (reversed-z, infinite far plane)

    m_CullData.sseThresholdPx = sceneView.sseThresholdPx;
    m_CullData.viewportHeight = frozenVp.y;
    m_CullData.cullFlags      = sceneView.cullFlags;
    m_CullData.hiZMipCount    = sceneView.hiZMipCount;
    m_CullData.hiZWidth       = sceneView.hiZWidth;
    m_CullData.hiZHeight      = sceneView.hiZHeight;
    memcpy(m_FrameData[m_ContextIndex].pCullBuffer->MappedMemory(), &m_CullData, sizeof(CullData));

    SceneEnvironmentData sceneEnvironmentData =
    {
        .atmosphere = sceneView.atmosphere.data,
        .cloud      = sceneView.cloud.data
    };
    memcpy(m_FrameData[m_ContextIndex].pSceneEnvironmentBuffer->MappedMemory(), &sceneEnvironmentData, sizeof(sceneEnvironmentData));
}

void Dx12SceneResource::UpdateSceneResources(const SceneRenderView& sceneView, render::CommandContext& context)
{
    using namespace render;

    auto& ctx = static_cast<Dx12CommandContext&>(context);
    if (sceneView.sceneRevision != m_LastSceneRevision)
    {
        for (auto& frameData : m_FrameData)
        {
            frameData.bInitialized = false;
        }
    }
    else if (m_FrameData[m_ContextIndex].bInitialized)
    {
        UpdateCameraAndEnvironment(sceneView, ctx);
        return;
    }

    ResetFrameBuffers();

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
        const float3& originWS = sceneView.voxelTerrain.originWorld;
        TransformData voxelTransform = {};
        voxelTransform.mLocalToWorld = mat4(1.0f); voxelTransform.mLocalToWorld[3] = float4( originWS, 1.0f);
        voxelTransform.mWorldToLocal = mat4(1.0f); voxelTransform.mWorldToLocal[3] = float4(-originWS, 1.0f);
        transforms.push_back(voxelTransform);
    }
    UpdateFrameBuffer(ctx, transforms.data(), (u32)transforms.size(), sizeof(TransformData), *m_FrameData[m_ContextIndex].pTransformAllocator, BarrierStates::ShaderResource);

    std::vector< MaterialData > materials;
    materials.reserve(sceneView.materials.size());
    std::vector< MaterialTextureData > materialTextures;
    materialTextures.reserve(sceneView.materials.size() * 4u);
    std::unordered_map< Dx12Texture*, u32 > srvIndexCache;
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
                ? render::eTextureColorSpace::SRGB
                : render::eTextureColorSpace::Linear;
            auto pMaterialTex = GetOrLoadTexture(textureView.filepath, colorSpace);
            if (!pMaterialTex)
                continue;

            auto [it, bInserted] = srvIndexCache.try_emplace(pMaterialTex.get(), pMaterialTex->GetShaderResourceHandle());
            UNUSED(bInserted);
            materialTextures.push_back({
                .textureID = it->second,
                .semantic  = textureView.semantic,
                .channel   = textureView.channel,
                .padding0  = 0u
            });
        }
        material.textureCount = static_cast<u32>(materialTextures.size()) - material.textureOffset;

        materials.push_back(material);
    }
    UpdateFrameBuffer(ctx, materials.data(), (u32)materials.size(), sizeof(MaterialData), *m_FrameData[m_ContextIndex].pMaterialAllocator, BarrierStates::ShaderResource);
    UpdateFrameBuffer(ctx, materialTextures.data(), (u32)materialTextures.size(), sizeof(MaterialTextureData), *m_FrameData[m_ContextIndex].pMaterialTextureAllocator, BarrierStates::ShaderResource);

    u32 vTotalCount  = 0;
    u32 iTotalCount  = 0;
    u32 mTotalCount  = 0;
    u32 mvTotalCount = 0;
    u32 mtTotalCount = 0;
    std::vector< MeshData > meshes;
    for (const auto& meshView : sceneView.meshes)
    {
        auto vHandle = GetOrUpdateVertex(meshView.id, meshView.tag, meshView.vData, meshView.vCount);

        MeshData mesh = {};
        mesh.vOffset = vHandle.offset;
        mesh.maxLOD  = meshView.maxLOD;

        for (u8 i = 0; i <= meshView.maxLOD; ++i)
        {
            std::string tag = meshView.tag + "_LOD" + std::to_string(i);

            auto iHandle  = GetOrUpdateIndex(meshView.id, tag, meshView.lods[i].iData, meshView.lods[i].iCount);
            auto mHandle  = GetOrUpdateMeshlets(meshView.id, tag, meshView.lods[i].mData, meshView.lods[i].mCount);
            auto mvHandle = GetOrUpdateMeshletVertices(meshView.id, tag, meshView.lods[i].mvData, meshView.lods[i].mvCount);
            auto mtHandle = GetOrUpdateMeshletTriangles(meshView.id, tag, meshView.lods[i].mtData, meshView.lods[i].mtCount);

            mesh.lods[i].iOffset = iHandle.offset;

            mesh.lods[i].mCount   = mHandle.count;
            mesh.lods[i].mOffset  = mHandle.offset;
            mesh.lods[i].mvOffset = mvHandle.offset;
            mesh.lods[i].mtOffset = mtHandle.offset;

            mesh.lods[i].simplifyError = meshView.lods[i].simplifyError;
        }

        mesh.center = meshView.sphere.Center();
        mesh.radius = meshView.sphere.Radius();

        meshes.push_back(mesh);
    }
    if (sceneView.voxelTerrain.bValid)
    {
        const float half = sceneView.voxelTerrain.chunkWorldSizeMeter * 0.5f; // origin is applied by the voxel transform, not here

        MeshData voxelMesh = {};
        voxelMesh.vOffset = 0;
        voxelMesh.maxLOD  = 0;
        voxelMesh.center  = float3(half);      // chunk-local
        voxelMesh.radius  = half * 1.7320508f; // cube half-diagonal
        // these fields will be filled by the voxel patch CS
        voxelMesh.lods[0].mCount   = 0;
        voxelMesh.lods[0].mOffset  = 0;
        voxelMesh.lods[0].mvOffset = 0;
        voxelMesh.lods[0].mtOffset = 0;
        meshes.push_back(voxelMesh);
    }
    UpdateFrameBuffer(ctx, meshes.data(), (u32)meshes.size(), sizeof(MeshData), *m_FrameData[m_ContextIndex].pMeshDataAllocator, BarrierStates::ShaderResource);
    
    if (m_pVertexAllocator->GetElementCount() < vTotalCount) 
        m_pVertexAllocator->Resize(vTotalCount * 2);
    if (m_pIndexAllocator->GetElementCount() < iTotalCount) 
        m_pIndexAllocator->Resize(iTotalCount * 2);
    if (m_pMeshletAllocator->GetElementCount() < mTotalCount) 
        m_pMeshletAllocator->Resize(mTotalCount * 2);
    if (m_pMeshletVertexAllocator->GetElementCount() < mvTotalCount) 
        m_pMeshletVertexAllocator->Resize(mvTotalCount * 2);
    if (m_pMeshletTriangleAllocator->GetElementCount() < mtTotalCount) 
        m_pMeshletTriangleAllocator->Resize(mtTotalCount * 2);

    u32 instID = 0;
    std::vector< InstanceData > instances;
    for (auto& [id, data] : sceneView.draws)
    {
        InstanceData instance = {};
        if (IsValidIndex(data.mesh))
        {
            assert(data.mesh < sceneView.meshes.size());
            auto& meshView = sceneView.meshes[data.mesh];

            {
                auto vHandle = GetOrUpdateVertex(meshView.id, meshView.tag, meshView.vData, meshView.vCount);
                auto iHandle = GetOrUpdateIndex(meshView.id, meshView.tag, meshView.lods[0].iData, meshView.lods[0].iCount);
                GetOrCreateBLAS(meshView.tag, vHandle, iHandle);

                instance.meshID = data.mesh;

                assert(IsValidIndex(data.transform) && data.transform < sceneView.transforms.size());
                instance.transformID = data.transform;

                instance.materialID = kInvalidIndex;
                if (IsValidIndex(data.material))
                {
                    assert(data.material < sceneView.materials.size());
                    instance.materialID = data.material;
                }

                instance.visOffset = m_NumMeshletVisibilitySlots;

                u32 maxLodMeshletCount = 0;
                for (u8 i = 0; i <= meshView.maxLOD; ++i)
                    maxLodMeshletCount = std::max(maxLodMeshletCount, meshes[data.mesh].lods[i].mCount);
                m_NumMeshletVisibilitySlots += maxLodMeshletCount;

                instances.push_back(instance);

                m_NumInstances++;
            }
            {
                auto& transformView = sceneView.transforms[data.transform];

                auto blasIter = m_BLASCache.find(meshView.tag);
                if (blasIter == m_BLASCache.end())
                    continue;

                const mat4& m = transformView.mWorld;

                // glm::mat4 (column-major) => 3x4 row-major
                render::AccelerationStructureInstanceDesc inst = {};
                inst.transform[0][0] = m[0][0]; inst.transform[0][1] = m[1][0]; inst.transform[0][2] = m[2][0]; inst.transform[0][3] = m[3][0];
                inst.transform[1][0] = m[0][1]; inst.transform[1][1] = m[1][1]; inst.transform[1][2] = m[2][1]; inst.transform[1][3] = m[3][1];
                inst.transform[2][0] = m[0][2]; inst.transform[2][1] = m[1][2]; inst.transform[2][2] = m[2][2]; inst.transform[2][3] = m[3][2];

                inst.instanceID                          = instID++;
                inst.pBLAS                               = blasIter->second.get();
                inst.instanceContributionToHitGroupIndex = 0;

                m_pTLAS->AddInstance(inst);
            }
        }
    }
    // Voxel chunk: prepend it at the head of instance buffer
    if (sceneView.voxelTerrain.bValid)
    {
        InstanceData voxelInstance = {};
        voxelInstance.meshID      = (u32)sceneView.meshes.size();      // the appended voxel MeshData
        voxelInstance.transformID = (u32)sceneView.transforms.size();  // the appended voxel TransformData
        voxelInstance.materialID  = kInvalidIndex;
        voxelInstance.visOffset   = 0;                                 // unused: voxel skips per-meshlet cull
        voxelInstance.isVoxel     = 1;
        instances.insert(instances.begin() + kVoxelChunkInstanceBase, voxelInstance); // instanceID == chunkID
    }
    m_NumInstances = (u32)instances.size();

    if (m_pTLAS->NumInstances() > 0)
    {
        m_pTLAS->Prepare();
    }
    BuildAccelerationStructures();

    UpdateFrameBuffer(ctx, instances.data(), (u32)instances.size(), sizeof(InstanceData), *m_FrameData[m_ContextIndex].pInstanceAllocator, BarrierStates::ShaderResource);
    UpdateFrameBuffer(ctx, &sceneView.light, 1, sizeof(LightData), *m_FrameData[m_ContextIndex].pLightAllocator, BarrierStates::ConstantBuffer);

    UpdateCameraAndEnvironment(sceneView, ctx);
    m_FrameData[m_ContextIndex].bInitialized = true;
    m_LastSceneRevision = sceneView.sceneRevision;
}

void Dx12SceneResource::BindSceneResources(render::CommandContext& context)
{
    auto& rm = static_cast<Dx12ResourceManager&>(m_RenderDevice.GetResourceManager());
    const auto& pGlobalRootSignature = rm.GetGlobalRootSignature();

    Dx12CommandContext& rhicontext = static_cast<Dx12CommandContext&>(context);
    const auto& d3d12CommandList2 = rhicontext.GetD3D12CommandList();

    auto cameraRootIdx = pGlobalRootSignature->GetRootIndex(D3D12_ROOT_PARAMETER_TYPE_CBV, kGlobalDescriptorSpace, 0);
    d3d12CommandList2->SetComputeRootConstantBufferView(cameraRootIdx, m_FrameData[m_ContextIndex].pCameraBuffer->GpuAddress());
    d3d12CommandList2->SetGraphicsRootConstantBufferView(cameraRootIdx, m_FrameData[m_ContextIndex].pCameraBuffer->GpuAddress());

    const u32 meshStreams[5] = {
        m_pVertexAllocator->GetBuffer()->GetShaderResourceHandle(),
        m_pIndexAllocator->GetBuffer()->GetShaderResourceHandle(),
        m_pMeshletAllocator->GetBuffer()->GetShaderResourceHandle(),
        m_pMeshletVertexAllocator->GetBuffer()->GetShaderResourceHandle(),
        m_pMeshletTriangleAllocator->GetBuffer()->GetShaderResourceHandle(),
    };
    memcpy(m_FrameData[m_ContextIndex].pMeshStreamsBuffer->MappedMemory(), meshStreams, sizeof(meshStreams));
    auto meshStreamsRootIdx = pGlobalRootSignature->GetRootIndex(D3D12_ROOT_PARAMETER_TYPE_CBV, kGlobalDescriptorSpace, 1);
    d3d12CommandList2->SetComputeRootConstantBufferView(meshStreamsRootIdx, m_FrameData[m_ContextIndex].pMeshStreamsBuffer->GpuAddress());
    d3d12CommandList2->SetGraphicsRootConstantBufferView(meshStreamsRootIdx, m_FrameData[m_ContextIndex].pMeshStreamsBuffer->GpuAddress());

    auto mdRootIdx = pGlobalRootSignature->GetRootIndex(D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS, kGlobalDescriptorSpace, 6);
    d3d12CommandList2->SetComputeRoot32BitConstant(mdRootIdx, m_FrameData[m_ContextIndex].pMeshDataAllocator->GetBuffer()->GetShaderResourceHandle(), 0);
    d3d12CommandList2->SetGraphicsRoot32BitConstant(mdRootIdx, m_FrameData[m_ContextIndex].pMeshDataAllocator->GetBuffer()->GetShaderResourceHandle(), 0);

    auto instRootIdx = pGlobalRootSignature->GetRootIndex(D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS, kGlobalDescriptorSpace, 7);
    d3d12CommandList2->SetComputeRoot32BitConstant(instRootIdx, m_FrameData[m_ContextIndex].pInstanceAllocator->GetBuffer()->GetShaderResourceHandle(), 0);
    d3d12CommandList2->SetGraphicsRoot32BitConstant(instRootIdx, m_FrameData[m_ContextIndex].pInstanceAllocator->GetBuffer()->GetShaderResourceHandle(), 0);

    auto cullRootIdx = pGlobalRootSignature->GetRootIndex(D3D12_ROOT_PARAMETER_TYPE_CBV, kGlobalDescriptorSpace, 8);
    d3d12CommandList2->SetComputeRootConstantBufferView(cullRootIdx, m_FrameData[m_ContextIndex].pCullBuffer->GpuAddress());
    d3d12CommandList2->SetGraphicsRootConstantBufferView(cullRootIdx, m_FrameData[m_ContextIndex].pCullBuffer->GpuAddress());

    auto transformBufferIdx = pGlobalRootSignature->GetRootIndex(D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS, kGlobalDescriptorSpace, 9);
    d3d12CommandList2->SetComputeRoot32BitConstant(transformBufferIdx, GetTransformBuffer()->GetShaderResourceHandle(), 0);
    d3d12CommandList2->SetGraphicsRoot32BitConstant(transformBufferIdx, GetTransformBuffer()->GetShaderResourceHandle(), 0);

    auto materialBufferIdx = pGlobalRootSignature->GetRootIndex(D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS, kGlobalDescriptorSpace, 10);
    d3d12CommandList2->SetComputeRoot32BitConstant(materialBufferIdx, GetMaterialBuffer()->GetShaderResourceHandle(), 0);
    d3d12CommandList2->SetGraphicsRoot32BitConstant(materialBufferIdx, GetMaterialBuffer()->GetShaderResourceHandle(), 0);

    auto materialTextureBufferIdx = pGlobalRootSignature->GetRootIndex(D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS, kGlobalDescriptorSpace, 14);
    d3d12CommandList2->SetComputeRoot32BitConstant(materialTextureBufferIdx, GetMaterialTextureBuffer()->GetShaderResourceHandle(), 0);
    d3d12CommandList2->SetGraphicsRoot32BitConstant(materialTextureBufferIdx, GetMaterialTextureBuffer()->GetShaderResourceHandle(), 0);

    auto lightRootIdx = pGlobalRootSignature->GetRootIndex(D3D12_ROOT_PARAMETER_TYPE_CBV, kGlobalDescriptorSpace, 11);
    d3d12CommandList2->SetComputeRootConstantBufferView(lightRootIdx, GetLightBuffer()->GpuAddress());
    d3d12CommandList2->SetGraphicsRootConstantBufferView(lightRootIdx, GetLightBuffer()->GpuAddress());

    auto sceneEnvironmentRootIdx = pGlobalRootSignature->GetRootIndex(D3D12_ROOT_PARAMETER_TYPE_CBV, kGlobalDescriptorSpace, 12);
    d3d12CommandList2->SetComputeRootConstantBufferView(sceneEnvironmentRootIdx, m_FrameData[m_ContextIndex].pSceneEnvironmentBuffer->GpuAddress());
    d3d12CommandList2->SetGraphicsRootConstantBufferView(sceneEnvironmentRootIdx, m_FrameData[m_ContextIndex].pSceneEnvironmentBuffer->GpuAddress());

    auto frozenCameraRootIdx = pGlobalRootSignature->GetRootIndex(D3D12_ROOT_PARAMETER_TYPE_CBV, kGlobalDescriptorSpace, 13);
    d3d12CommandList2->SetComputeRootConstantBufferView(frozenCameraRootIdx, m_FrameData[m_ContextIndex].pFrozenCameraBuffer->GpuAddress());
    d3d12CommandList2->SetGraphicsRootConstantBufferView(frozenCameraRootIdx, m_FrameData[m_ContextIndex].pFrozenCameraBuffer->GpuAddress());
}

BufferHandle Dx12SceneResource::GetOrUpdateVertex(u64 entity, const std::string& filepath, const void* pData, u32 count)
{
    std::string f = filepath.data();
    if (m_VertexCache.contains(f))
    {
        return m_VertexCache.find(f)->second;
    }
    auto& rm = static_cast<Dx12ResourceManager&>(m_RenderDevice.GetResourceManager());

    auto allocation = m_pVertexAllocator->Allocate(count);
    rm.UploadData(allocation.pBuffer, pData, allocation.sizeInBytes, allocation.offsetInBytes, BarrierStates::NonPixelShaderResource);

    BufferHandle handle = {};
    handle.gpuHandle          = allocation.gpuHandle;
    handle.elementSizeInBytes = m_pVertexAllocator->GetElementSize();
    handle.offset             = u32(allocation.offsetInBytes / handle.elementSizeInBytes);
    handle.count              = count;

    m_VertexCache.emplace(filepath, handle);
    return handle;
}

BufferHandle Dx12SceneResource::GetOrUpdateIndex(u64 entity, const std::string& filepath, const void* pData, u32 count)
{
    std::string f = filepath.data();
    if (m_IndexCache.contains(f))
    {
        return m_IndexCache.find(f)->second;
    }

    auto& rm = static_cast<Dx12ResourceManager&>(m_RenderDevice.GetResourceManager());

    auto allocation = m_pIndexAllocator->Allocate(count);
    rm.UploadData(allocation.pBuffer, pData, allocation.sizeInBytes, allocation.offsetInBytes, BarrierStates::NonPixelShaderResource);

    BufferHandle handle = {};
    handle.gpuHandle          = allocation.gpuHandle;
    handle.elementSizeInBytes = m_pIndexAllocator->GetElementSize();
    handle.offset             = u32(allocation.offsetInBytes / handle.elementSizeInBytes);
    handle.count              = count;

    m_IndexCache.emplace(filepath, handle);
    return handle;
}

BufferHandle Dx12SceneResource::GetOrUpdateMeshlets(u64 entity, const std::string& filepath, const void* pData, u32 count)
{
    std::string f = filepath.data();
    if (m_MeshletCache.contains(f))
    {
        return m_MeshletCache.find(f)->second;
    }
    auto& rm = static_cast<Dx12ResourceManager&>(m_RenderDevice.GetResourceManager());

    auto allocation = m_pMeshletAllocator->Allocate(count);
    rm.UploadData(allocation.pBuffer, pData, allocation.sizeInBytes, allocation.offsetInBytes, BarrierStates::NonPixelShaderResource);

    BufferHandle handle = {};
    handle.gpuHandle          = allocation.pBuffer->GetD3D12Resource()->GetGPUVirtualAddress();
    handle.elementSizeInBytes = m_pMeshletAllocator->GetElementSize();
    handle.offset             = u32(allocation.offsetInBytes / handle.elementSizeInBytes);
    handle.count              = count;

    m_MeshletCache.emplace(filepath, handle);
    return handle;
}

BufferHandle Dx12SceneResource::GetOrUpdateMeshletVertices(u64 entity, const std::string& filepath, const void* pData, u32 count)
{
    std::string f = filepath.data();
    if (m_MeshletVertexCache.contains(f))
    {
        return m_MeshletVertexCache.find(f)->second;
    }
    auto& rm = static_cast<Dx12ResourceManager&>(m_RenderDevice.GetResourceManager());

    auto allocation = m_pMeshletVertexAllocator->Allocate(count);
    rm.UploadData(allocation.pBuffer, pData, allocation.sizeInBytes, allocation.offsetInBytes, BarrierStates::NonPixelShaderResource);

    BufferHandle handle = {};
    handle.gpuHandle          = allocation.pBuffer->GetD3D12Resource()->GetGPUVirtualAddress();
    handle.elementSizeInBytes = m_pMeshletVertexAllocator->GetElementSize();
    handle.offset             = u32(allocation.offsetInBytes / handle.elementSizeInBytes);
    handle.count              = count;

    m_MeshletVertexCache.emplace(filepath, handle);
    return handle;
}

BufferHandle Dx12SceneResource::GetOrUpdateMeshletTriangles(u64 entity, const std::string& filepath, const void* pData, u32 count)
{
    std::string f = filepath.data();
    if (m_MeshletTriangleCache.contains(f))
    {
        return m_MeshletTriangleCache.find(f)->second;
    }
    auto& rm = static_cast<Dx12ResourceManager&>(m_RenderDevice.GetResourceManager());

    auto allocation = m_pMeshletTriangleAllocator->Allocate(count, sizeof(u32));
    rm.UploadData(allocation.pBuffer, pData, allocation.sizeInBytes, allocation.offsetInBytes, BarrierStates::NonPixelShaderResource);

    BufferHandle handle = {};
    handle.gpuHandle          = allocation.pBuffer->GetD3D12Resource()->GetGPUVirtualAddress();
    handle.elementSizeInBytes = m_pMeshletTriangleAllocator->GetElementSize();
    handle.offset             = u32(allocation.offsetInBytes / handle.elementSizeInBytes);
    handle.count              = count;

    m_MeshletTriangleCache.emplace(filepath, handle);
    return handle;
}

Arc< Dx12BottomLevelAS > Dx12SceneResource::GetOrCreateBLAS(const std::string& tag, const BufferHandle& vHandle, const BufferHandle& iHandle)
{
    auto iter = m_BLASCache.find(tag);
    if (iter != m_BLASCache.end())
        return iter->second;

    auto pBLAS = Dx12BottomLevelAS::Create(m_RenderDevice, tag.c_str());

    render::GeometryDesc geom = {};
    geom.vertexBufferAddress = vHandle.gpuHandle;
    geom.vertexCount         = vHandle.count;
    geom.vertexStride        = static_cast<u32>(vHandle.elementSizeInBytes);
    geom.indexBufferAddress  = iHandle.gpuHandle;
    geom.indexCount          = iHandle.count;
    geom.geometryFlags       = render::eGeometryFlag_Opaque;
    pBLAS->AddGeometry(geom);
    pBLAS->Prepare();

    m_BLASCache.emplace(tag, pBLAS);
    m_PendingBLASBuilds.push_back(pBLAS.get());
    return pBLAS;
}

Arc< Dx12Texture > Dx12SceneResource::GetOrLoadTexture(const std::string& filepath, render::eTextureColorSpace colorSpace)
{
    const std::string cacheKey = MakeTextureCacheKey(filepath, colorSpace);
    if (m_TextureCache.contains(cacheKey))
    {
        return m_TextureCache.find(cacheKey)->second;
    }

    auto& rm   = static_cast<Dx12ResourceManager&>(m_RenderDevice.GetResourceManager());
    auto  pTex = StaticCast<Dx12Texture>(rm.LoadTexture(filepath, true, colorSpace));

    m_TextureCache.emplace(cacheKey, pTex);
    return pTex;
}

void Dx12SceneResource::UpdateFrameBuffer(Dx12CommandContext& context, const void* pData, u32 count, u64 elementSizeInBytes, StaticBufferAllocator& targetBuffer, const BarrierState& stateAfter)
{
    if (count == 0 || elementSizeInBytes == 0)
        return;

    auto allocation = targetBuffer.Allocate(count);
    context.UploadData(allocation.pBuffer, pData, count, elementSizeInBytes, allocation.offsetInBytes);
    context.TransitionBarrier(allocation.pBuffer.get(), stateAfter);
}

void Dx12SceneResource::BuildAccelerationStructures()
{
    bool bHasTLAS        = m_pTLAS->NumInstances() > 0;
    bool bHasPendingBLAS = m_PendingBLASBuilds.empty() == false;
    if (!bHasPendingBLAS && !bHasTLAS)
        return;

    auto pContext = m_RenderDevice.BeginCommand(D3D12_COMMAND_LIST_TYPE_DIRECT);
    for (auto* pBLAS : m_PendingBLASBuilds)
    {
        pContext->BuildBLAS(*pBLAS);
    }
    m_PendingBLASBuilds.clear();

    if (bHasTLAS)
    {
        pContext->BuildTLAS(*m_pTLAS);
    }

    pContext->Close();
    m_RenderDevice.ExecuteCommand(std::move(pContext)).Wait();
}

ID3D12CommandSignature* Dx12SceneResource::GetSceneD3D12CommandSignature() const
{
    return !m_RenderDevice.GetDeviceSettings().bMeshShader 
        ? m_pIndirectDrawSignature->GetD3D12CommandSignature() : m_pIndirectDispatchSignature->GetD3D12CommandSignature();
}

Arc< Dx12StructuredBuffer > Dx12SceneResource::GetIndirectBuffer() const
{
    return nullptr; // m_FrameData[m_ContextIndex].pIndirectCommandAllocator->GetBuffer();
}

Arc< Dx12StructuredBuffer > Dx12SceneResource::GetTransformBuffer() const
{
    return m_FrameData[m_ContextIndex].pTransformAllocator->GetBuffer();
}

Arc< Dx12StructuredBuffer > Dx12SceneResource::GetMaterialBuffer() const
{
    return m_FrameData[m_ContextIndex].pMaterialAllocator->GetBuffer();
}

Arc< Dx12StructuredBuffer > Dx12SceneResource::GetMaterialTextureBuffer() const
{
    return m_FrameData[m_ContextIndex].pMaterialTextureAllocator->GetBuffer();
}

Arc< Dx12StructuredBuffer > Dx12SceneResource::GetLightBuffer() const
{
    return m_FrameData[m_ContextIndex].pLightAllocator->GetBuffer();
}

Arc< Dx12StructuredBuffer > Dx12SceneResource::GetMeshletBuffer() const
{
    return m_pMeshletAllocator->GetBuffer();
}

Arc< render::TopLevelAccelerationStructure > Dx12SceneResource::GetTLAS() const
{
    return StaticCast< render::TopLevelAccelerationStructure >(m_pTLAS);
}

Arc< render::Buffer > Dx12SceneResource::GetMeshDataBuffer() const
{
    return m_FrameData[m_ContextIndex].pMeshDataAllocator->GetBuffer();
}

void Dx12SceneResource::ResetFrameBuffers()
{
    m_NumInstances              = 0;
    m_NumMeshletVisibilitySlots = 0;

    m_FrameData[m_ContextIndex].Reset();

    m_pTLAS->Reset();
    m_PendingBLASBuilds.clear();
}

} // namespace dx12
