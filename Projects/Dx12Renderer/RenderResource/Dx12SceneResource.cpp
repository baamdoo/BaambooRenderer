#include "RendererPch.h"
#include "Dx12SceneResource.h"
#include "Dx12Buffer.h"
#include "Dx12Texture.h"
#include "Dx12Shader.h"
#include "Dx12AccelerationStructure.h"

#include "RenderDevice/Dx12CommandContext.h"
#include "RenderDevice/Dx12CommandQueue.h"
#include "RenderDevice/Dx12Rootsignature.h"
#include "RenderDevice/Dx12CommandSignature.h"
#include "RenderDevice/Dx12BufferAllocator.h"
#include "RenderDevice/Dx12RenderPipeline.h"
#include "RenderDevice/Dx12ResourceManager.h"

#include "SceneRenderView.h"
#include "Utils/Math.hpp"

namespace dx12
{

static Dx12ComputePipeline* s_CombineTexturesPSO = nullptr;

static std::string MakeTextureCacheKey(const std::string& filepath, render::eTextureColorSpace colorSpace)
{
    return filepath + (colorSpace == render::eTextureColorSpace::SRGB ? "|srgb" : "|linear");
}

Arc< Dx12Texture > CombineTextures(Dx12RenderDevice& rd, const char* name, Arc< Dx12Texture > pTextureR, Arc< Dx12Texture > pTextureG, Arc< Dx12Texture > pTextureB)
{
    using namespace render;

    u32 width
        = (u32)std::max({ pTextureR->Desc().Width, pTextureG->Desc().Width, pTextureB->Desc().Width });
    u32 height
        = (u32)std::max({ pTextureR->Desc().Height, pTextureG->Desc().Height, pTextureB->Desc().Height });
    auto pCombinedTexture =
        Dx12Texture::Create(
            rd,
            name,
            {
                .resolution = { width, height, 1 },
                .imageUsage = eTextureUsage_Sample | eTextureUsage_Storage
            });

    auto& cmdQueue = rd.GraphicsQueue();
    auto  pContext = cmdQueue.Allocate(); // use direct queue for state management
    {
        pContext->SetRenderPipeline(s_CombineTexturesPSO);
        
        pContext->TransitionBarrier(pTextureR.get(), BarrierStates::NonPixelShaderResource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, false);
        pContext->TransitionBarrier(pTextureG.get(), BarrierStates::NonPixelShaderResource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, false);
        pContext->TransitionBarrier(pTextureB.get(), BarrierStates::NonPixelShaderResource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, false);
        pContext->TransitionBarrier(pCombinedTexture.get(), BarrierStates::UnorderedAccess);

        pContext->StageDescriptors(
            {
                { "g_TextureR", pTextureR->GetShaderResourceHandle() },
                { "g_TextureG", pTextureG->GetShaderResourceHandle() },
                { "g_TextureB", pTextureB->GetShaderResourceHandle() },
                { "g_OutCombinedTexture", pCombinedTexture->GetUnorderedAccessHandle(0) }
            });

        pContext->Dispatch2D< 16, 16 >(width, height);

        pContext->TransitionBarrier(pCombinedTexture.get(), BarrierStates::PixelShaderResource);
        pContext->Close();
    }
    auto fenceValue = cmdQueue.ExecuteCommandList(pContext);
    cmdQueue.WaitForFenceValue(fenceValue);

    return pCombinedTexture;
}

void Dx12SceneResource::PerFrameData::Reset()
{
    bInitialized = true;

    if (pMeshDataAllocator)
        pMeshDataAllocator->Reset();
    if (pInstanceAllocator)
        pInstanceAllocator->Reset();

    if (pTransformAllocator)
        pTransformAllocator->Reset();
    if (pMaterialAllocator)
        pMaterialAllocator->Reset();
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
        frameData.pMeshDataAllocator        = MakeBox< StaticBufferAllocator >(m_RenderDevice, "MeshDataPool", sizeof(MeshData), _MB(8LL));
        frameData.pInstanceAllocator        = MakeBox< StaticBufferAllocator >(m_RenderDevice, "InstancePool", sizeof(InstanceData), _MB(8LL));

        frameData.pTransformAllocator = MakeBox< StaticBufferAllocator >(m_RenderDevice, "TransformPool", sizeof(TransformData), _MB(8LL));
        frameData.pMaterialAllocator  = MakeBox< StaticBufferAllocator >(m_RenderDevice, "MaterialPool", sizeof(MaterialData), _MB(8LL));
        frameData.pLightAllocator     = MakeBox< StaticBufferAllocator >(m_RenderDevice, "LightPool", sizeof(LightData), 1);

        frameData.pCameraBuffer           = Dx12ConstantBuffer::Create(m_RenderDevice, "CameraBuffer", sizeof(CameraData));
        frameData.pCullBuffer             = Dx12ConstantBuffer::Create(m_RenderDevice, "CullBuffer", sizeof(CullData));
        frameData.pSceneEnvironmentBuffer = Dx12ConstantBuffer::Create(m_RenderDevice, "SceneEnvironmentBuffer", sizeof(SceneEnvironmentData));
        frameData.pFrozenCameraBuffer     = Dx12ConstantBuffer::Create(m_RenderDevice, "FrozenCameraBuffer", sizeof(FrozenCameraData));
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
    RELEASE(s_CombineTexturesPSO);

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
        ApplyJittering(sceneView.camera.mProj, baamboo::math::GetHaltonSequence((u32)sceneView.frame)) : sceneView.camera.mProj;
    camera.mViewProj               = camera.mProj * camera.mView;
    camera.mViewProjInv            = glm::inverse(camera.mViewProj);
    camera.mViewProjUnjittered     = sceneView.camera.mProj * camera.mView;
    camera.mViewProjUnjitteredPrev =
        m_CameraCache.mViewProjUnjittered == glm::identity< mat4 >() ? camera.mViewProjUnjittered : m_CameraCache.mViewProjUnjittered;
    camera.position = sceneView.camera.pos;
    camera.zNear    = sceneView.camera.zNear;
    camera.zFar     = sceneView.camera.zFar;
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

    auto& rm  = static_cast<Dx12ResourceManager&>(m_RenderDevice.GetResourceManager());
    auto& ctx = static_cast<Dx12CommandContext&>(context);
    if (sceneView.pEntityDirtyMarks != nullptr)
    {
        for (auto& frameData : m_FrameData)
        {
            frameData.bInitialized = false;
        }
    }
    /*else if (m_FrameData[m_ContextIndex].bInitialized)
    {
        UpdateCameraAndEnvironment(sceneView, ctx);
        return;
    }*/

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
    UpdateFrameBuffer(ctx, transforms.data(), (u32)transforms.size(), sizeof(TransformData), *m_FrameData[m_ContextIndex].pTransformAllocator, BarrierStates::ShaderResource);

    std::vector< MaterialData > materials;
    materials.reserve(sceneView.materials.size());
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

        material.albedoID = INVALID_INDEX;
        if (!materialView.albedoTex.empty())
        {
            auto pMaterialTex = GetOrLoadTexture(materialView.id, materialView.albedoTex, render::eTextureColorSpace::SRGB);
            if (srvIndexCache.contains(pMaterialTex.get()))
            {
                material.albedoID = srvIndexCache[pMaterialTex.get()];
            }
            else if (pMaterialTex)
            {
                material.albedoID = pMaterialTex->GetShaderResourceHandle();
                srvIndexCache.emplace(pMaterialTex.get(), material.albedoID);
            }
        }

        material.normalID = INVALID_INDEX;
        if (!materialView.normalTex.empty())
        {
            auto pMaterialTex = GetOrLoadTexture(materialView.id, materialView.normalTex);
            if (srvIndexCache.contains(pMaterialTex.get()))
            {
                material.normalID = srvIndexCache[pMaterialTex.get()];
            }
			else if (pMaterialTex)
            {
                material.normalID = pMaterialTex->GetShaderResourceHandle();
                srvIndexCache.emplace(pMaterialTex.get(), material.normalID);
            }
        }

        material.specularID = INVALID_INDEX;
        if (!materialView.specularTex.empty())
        {
            auto pMaterialTex = GetOrLoadTexture(materialView.id, materialView.specularTex, render::eTextureColorSpace::SRGB);
            if (srvIndexCache.contains(pMaterialTex.get()))
            {
                material.specularID = srvIndexCache[pMaterialTex.get()];
            }
            else if (pMaterialTex)
            {
                material.specularID = pMaterialTex->GetShaderResourceHandle();
                srvIndexCache.emplace(pMaterialTex.get(), material.specularID);
            }
        }

        // combine orm
        std::string aoStr        = materialView.aoTex;
        std::string roughnessStr = materialView.roughnessTex;
        std::string metallicStr  = materialView.metallicTex;
        std::string ormStr       = aoStr + roughnessStr + metallicStr;

        material.metallicRoughnessAoID = INVALID_INDEX;
        auto pORM = GetTexture(ormStr);
        if (!pORM)
        {
            if (s_CombineTexturesPSO == nullptr)
            {
                s_CombineTexturesPSO = new Dx12ComputePipeline(m_RenderDevice, "CombineTextures");
                Arc< Dx12Shader > pCS
                    = Dx12Shader::Create(m_RenderDevice, "CombineTexturesCS", { .stage = render::eShaderStage::Compute, .filename = "CombineTexturesCS" });
                s_CombineTexturesPSO->SetComputeShader(std::move(pCS)).Build();
            }

            Arc< Dx12Texture > pCombiningTextures[3] = {};
            if (!materialView.aoTex.empty())
                pCombiningTextures[0] = GetOrLoadTexture(materialView.id, materialView.aoTex);
            else
                pCombiningTextures[0] = StaticCast<Dx12Texture>(rm.GetFlatWhiteTexture());

            if (!materialView.roughnessTex.empty())
                pCombiningTextures[1] = GetOrLoadTexture(materialView.id, materialView.roughnessTex);
            else
                pCombiningTextures[1] = StaticCast<Dx12Texture>(rm.GetFlatWhiteTexture());

            if (!materialView.metallicTex.empty())
                pCombiningTextures[2] = GetOrLoadTexture(materialView.id, materialView.metallicTex);
            else
                pCombiningTextures[2] = StaticCast<Dx12Texture>(rm.GetFlatWhiteTexture());

            pORM = CombineTextures(m_RenderDevice, "ORM", pCombiningTextures[0], pCombiningTextures[1], pCombiningTextures[2]);
            m_TextureCache.emplace(ormStr, pORM);
        }

        if (srvIndexCache.contains(pORM.get()))
        {
            material.metallicRoughnessAoID = srvIndexCache[pORM.get()];
        }
        else if (pORM)
        {
            material.metallicRoughnessAoID = pORM->GetShaderResourceHandle();
            srvIndexCache.emplace(pORM.get(), material.metallicRoughnessAoID);
        }

        material.emissiveID = INVALID_INDEX;
        if (!materialView.emissionTex.empty())
        {
            auto pMaterialTex = GetOrLoadTexture(materialView.id, materialView.emissionTex, render::eTextureColorSpace::SRGB);
            if (srvIndexCache.contains(pMaterialTex.get()))
            {
                material.emissiveID = srvIndexCache[pMaterialTex.get()];
            }
            else if (pMaterialTex)
            {
                material.emissiveID = pMaterialTex->GetShaderResourceHandle();
                srvIndexCache.emplace(pMaterialTex.get(), material.emissiveID);
            }
        }

        material.clearcoatID = INVALID_INDEX;
        if (!materialView.clearcoatTex.empty())
        {
            auto pMaterialTex = GetOrLoadTexture(materialView.id, materialView.clearcoatTex);
            if (srvIndexCache.contains(pMaterialTex.get()))
            {
                material.clearcoatID = srvIndexCache[pMaterialTex.get()];
            }
            else if (pMaterialTex)
            {
                material.clearcoatID = pMaterialTex->GetShaderResourceHandle();
                srvIndexCache.emplace(pMaterialTex.get(), material.clearcoatID);
            }
        }

        material.sheenID = INVALID_INDEX;
        if (!materialView.sheenTex.empty())
        {
            auto pMaterialTex = GetOrLoadTexture(materialView.id, materialView.sheenTex, render::eTextureColorSpace::SRGB);
            if (srvIndexCache.contains(pMaterialTex.get()))
            {
                material.sheenID = srvIndexCache[pMaterialTex.get()];
            }
            else if (pMaterialTex)
            {
                material.sheenID = pMaterialTex->GetShaderResourceHandle();
                srvIndexCache.emplace(pMaterialTex.get(), material.sheenID);
            }
        }

        material.anisotropyID = INVALID_INDEX;
        if (!materialView.anisotropyTex.empty())
        {
            auto pMaterialTex = GetOrLoadTexture(materialView.id, materialView.anisotropyTex);
            if (srvIndexCache.contains(pMaterialTex.get()))
            {
                material.anisotropyID = srvIndexCache[pMaterialTex.get()];
            }
            else if (pMaterialTex)
            {
                material.anisotropyID = pMaterialTex->GetShaderResourceHandle();
                srvIndexCache.emplace(pMaterialTex.get(), material.anisotropyID);
            }
        }

        material.subsurfaceID = INVALID_INDEX;
        if (!materialView.subsurfaceTex.empty())
        {
            auto pMaterialTex = GetOrLoadTexture(materialView.id, materialView.subsurfaceTex);
            if (srvIndexCache.contains(pMaterialTex.get()))
            {
                material.subsurfaceID = srvIndexCache[pMaterialTex.get()];
            }
            else if (pMaterialTex)
            {
                material.subsurfaceID = pMaterialTex->GetShaderResourceHandle();
                srvIndexCache.emplace(pMaterialTex.get(), material.subsurfaceID);
            }
        }

        material.transmissionID = INVALID_INDEX;
        if (!materialView.transmissionTex.empty())
        {
            auto pMaterialTex = GetOrLoadTexture(materialView.id, materialView.transmissionTex);
            if (srvIndexCache.contains(pMaterialTex.get()))
            {
                material.transmissionID = srvIndexCache[pMaterialTex.get()];
            }
            else if (pMaterialTex)
            {
                material.transmissionID = pMaterialTex->GetShaderResourceHandle();
                srvIndexCache.emplace(pMaterialTex.get(), material.transmissionID);
            }
        }

        materials.push_back(material);
    }
    UpdateFrameBuffer(ctx, materials.data(), (u32)materials.size(), sizeof(MaterialData), *m_FrameData[m_ContextIndex].pMaterialAllocator, BarrierStates::ShaderResource);

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

                instance.materialID = INVALID_INDEX;
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
    if (m_pTLAS->NumInstances() > 0)
    {
        m_pTLAS->Prepare();
    }
    BuildAccelerationStructures();

    UpdateFrameBuffer(ctx, instances.data(), (u32)instances.size(), sizeof(InstanceData), *m_FrameData[m_ContextIndex].pInstanceAllocator, BarrierStates::ShaderResource);
    UpdateFrameBuffer(ctx, &sceneView.light, 1, sizeof(LightData), *m_FrameData[m_ContextIndex].pLightAllocator, BarrierStates::PixelShaderResource);

    UpdateCameraAndEnvironment(sceneView, ctx);
}

void Dx12SceneResource::BindSceneResources(render::CommandContext& context)
{
    auto& rm = static_cast<Dx12ResourceManager&>(m_RenderDevice.GetResourceManager());
    const auto& pGlobalRootSignature = rm.GetGlobalRootSignature();

    Dx12CommandContext& rhicontext = static_cast<Dx12CommandContext&>(context);
    const auto& d3d12CommandList2 = rhicontext.GetD3D12CommandList();

    auto cameraRootIdx = pGlobalRootSignature->GetRootIndex(D3D12_ROOT_PARAMETER_TYPE_CBV, GLOBAL_DESCRIPTOR_SPACE, 0);
    d3d12CommandList2->SetComputeRootConstantBufferView(cameraRootIdx, m_FrameData[m_ContextIndex].pCameraBuffer->GpuAddress());
    d3d12CommandList2->SetGraphicsRootConstantBufferView(cameraRootIdx, m_FrameData[m_ContextIndex].pCameraBuffer->GpuAddress());

    auto vRootIdx = pGlobalRootSignature->GetRootIndex(D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS, GLOBAL_DESCRIPTOR_SPACE, 1);
    d3d12CommandList2->SetComputeRoot32BitConstant(vRootIdx, m_pVertexAllocator->GetBuffer()->GetShaderResourceHandle(), 0);
    d3d12CommandList2->SetGraphicsRoot32BitConstant(vRootIdx, m_pVertexAllocator->GetBuffer()->GetShaderResourceHandle(), 0);

    auto iRootIdx = pGlobalRootSignature->GetRootIndex(D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS, GLOBAL_DESCRIPTOR_SPACE, 2);
    d3d12CommandList2->SetComputeRoot32BitConstant(iRootIdx, m_pIndexAllocator->GetBuffer()->GetShaderResourceHandle(), 0);
    d3d12CommandList2->SetGraphicsRoot32BitConstant(iRootIdx, m_pIndexAllocator->GetBuffer()->GetShaderResourceHandle(), 0);

    auto mRootIdx = pGlobalRootSignature->GetRootIndex(D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS, GLOBAL_DESCRIPTOR_SPACE, 3);
    d3d12CommandList2->SetComputeRoot32BitConstant(mRootIdx, m_pMeshletAllocator->GetBuffer()->GetShaderResourceHandle(), 0);
    d3d12CommandList2->SetGraphicsRoot32BitConstant(mRootIdx, m_pMeshletAllocator->GetBuffer()->GetShaderResourceHandle(), 0);

    auto mvRootIdx = pGlobalRootSignature->GetRootIndex(D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS, GLOBAL_DESCRIPTOR_SPACE, 4);
    d3d12CommandList2->SetComputeRoot32BitConstant(mvRootIdx, m_pMeshletVertexAllocator->GetBuffer()->GetShaderResourceHandle(), 0);
    d3d12CommandList2->SetGraphicsRoot32BitConstant(mvRootIdx, m_pMeshletVertexAllocator->GetBuffer()->GetShaderResourceHandle(), 0);

    auto mtRootIdx = pGlobalRootSignature->GetRootIndex(D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS, GLOBAL_DESCRIPTOR_SPACE, 5);
    d3d12CommandList2->SetComputeRoot32BitConstant(mtRootIdx, m_pMeshletTriangleAllocator->GetBuffer()->GetShaderResourceHandle(), 0);
    d3d12CommandList2->SetGraphicsRoot32BitConstant(mtRootIdx, m_pMeshletTriangleAllocator->GetBuffer()->GetShaderResourceHandle(), 0);

    auto mdRootIdx = pGlobalRootSignature->GetRootIndex(D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS, GLOBAL_DESCRIPTOR_SPACE, 6);
    d3d12CommandList2->SetComputeRoot32BitConstant(mdRootIdx, m_FrameData[m_ContextIndex].pMeshDataAllocator->GetBuffer()->GetShaderResourceHandle(), 0);
    d3d12CommandList2->SetGraphicsRoot32BitConstant(mdRootIdx, m_FrameData[m_ContextIndex].pMeshDataAllocator->GetBuffer()->GetShaderResourceHandle(), 0);

    auto instRootIdx = pGlobalRootSignature->GetRootIndex(D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS, GLOBAL_DESCRIPTOR_SPACE, 7);
    d3d12CommandList2->SetComputeRoot32BitConstant(instRootIdx, m_FrameData[m_ContextIndex].pInstanceAllocator->GetBuffer()->GetShaderResourceHandle(), 0);
    d3d12CommandList2->SetGraphicsRoot32BitConstant(instRootIdx, m_FrameData[m_ContextIndex].pInstanceAllocator->GetBuffer()->GetShaderResourceHandle(), 0);

    auto cullRootIdx = pGlobalRootSignature->GetRootIndex(D3D12_ROOT_PARAMETER_TYPE_CBV, GLOBAL_DESCRIPTOR_SPACE, 8);
    d3d12CommandList2->SetComputeRootConstantBufferView(cullRootIdx, m_FrameData[m_ContextIndex].pCullBuffer->GpuAddress());
    d3d12CommandList2->SetGraphicsRootConstantBufferView(cullRootIdx, m_FrameData[m_ContextIndex].pCullBuffer->GpuAddress());

    auto transformBufferIdx = pGlobalRootSignature->GetRootIndex(D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS, GLOBAL_DESCRIPTOR_SPACE, 9);
    d3d12CommandList2->SetComputeRoot32BitConstant(transformBufferIdx, GetTransformBuffer()->GetShaderResourceHandle(), 0);
    d3d12CommandList2->SetGraphicsRoot32BitConstant(transformBufferIdx, GetTransformBuffer()->GetShaderResourceHandle(), 0);

    auto materialBufferIdx = pGlobalRootSignature->GetRootIndex(D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS, GLOBAL_DESCRIPTOR_SPACE, 10);
    d3d12CommandList2->SetComputeRoot32BitConstant(materialBufferIdx, GetMaterialBuffer()->GetShaderResourceHandle(), 0);
    d3d12CommandList2->SetGraphicsRoot32BitConstant(materialBufferIdx, GetMaterialBuffer()->GetShaderResourceHandle(), 0);

    auto lightRootIdx = pGlobalRootSignature->GetRootIndex(D3D12_ROOT_PARAMETER_TYPE_CBV, GLOBAL_DESCRIPTOR_SPACE, 11);
    d3d12CommandList2->SetComputeRootConstantBufferView(lightRootIdx, GetLightBuffer()->GpuAddress());
    d3d12CommandList2->SetGraphicsRootConstantBufferView(lightRootIdx, GetLightBuffer()->GpuAddress());

    auto sceneEnvironmentRootIdx = pGlobalRootSignature->GetRootIndex(D3D12_ROOT_PARAMETER_TYPE_CBV, GLOBAL_DESCRIPTOR_SPACE, 12);
    d3d12CommandList2->SetComputeRootConstantBufferView(sceneEnvironmentRootIdx, m_FrameData[m_ContextIndex].pSceneEnvironmentBuffer->GpuAddress());
    d3d12CommandList2->SetGraphicsRootConstantBufferView(sceneEnvironmentRootIdx, m_FrameData[m_ContextIndex].pSceneEnvironmentBuffer->GpuAddress());

    auto frozenCameraRootIdx = pGlobalRootSignature->GetRootIndex(D3D12_ROOT_PARAMETER_TYPE_CBV, GLOBAL_DESCRIPTOR_SPACE, 13);
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

Arc< Dx12Texture > Dx12SceneResource::GetOrLoadTexture(u64 entity, const std::string& filepath, render::eTextureColorSpace colorSpace)
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

Arc< Dx12Texture > Dx12SceneResource::GetTexture(const std::string& filepath)
{
    std::string f = filepath.data();
    if (m_TextureCache.contains(f))
    {
        return m_TextureCache.find(f)->second;
    }

    return nullptr;
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

void Dx12SceneResource::ResetFrameBuffers()
{
    m_NumInstances              = 0;
    m_NumMeshletVisibilitySlots = 0;

    m_FrameData[m_ContextIndex].Reset();

    m_pTLAS->Reset();
    m_PendingBLASBuilds.clear();
}

} // namespace dx12
