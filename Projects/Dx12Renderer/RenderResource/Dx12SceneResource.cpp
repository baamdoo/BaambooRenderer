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
        
        pContext->TransitionBarrier(pTextureR.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, false);
        pContext->TransitionBarrier(pTextureG.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, false);
        pContext->TransitionBarrier(pTextureB.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, false);
        pContext->TransitionBarrier(pCombinedTexture.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        pContext->StageDescriptors(
            {
                { "g_TextureR", pTextureR->GetShaderResourceHandle() },
                { "g_TextureG", pTextureG->GetShaderResourceHandle() },
                { "g_TextureB", pTextureB->GetShaderResourceHandle() },
                { "g_OutCombinedTexture", pCombinedTexture->GetUnorderedAccessHandle(0) }
            });

        pContext->Dispatch2D< 16, 16 >(width, height);

        pContext->TransitionBarrier(pCombinedTexture.get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        pContext->Close();
    }
    auto fenceValue = cmdQueue.ExecuteCommandList(pContext);
    cmdQueue.WaitForFenceValue(fenceValue);

    return pCombinedTexture;
}

Dx12SceneResource::Dx12SceneResource(Dx12RenderDevice& rd)
    : m_RenderDevice(rd)
{
    // **
    // scene buffers
    // **
    m_pCameraBuffer           = Dx12ConstantBuffer::Create(m_RenderDevice, "CameraBuffer", sizeof(CameraData), render::eBufferUsage_TransferDest);
    m_pSceneEnvironmentBuffer = Dx12ConstantBuffer::Create(m_RenderDevice, "SceneEnvironmentBuffer", sizeof(SceneEnvironmentData), render::eBufferUsage_TransferDest);

    m_pTransformAllocator = MakeBox< StaticBufferAllocator >(m_RenderDevice, "TransformPool", sizeof(TransformData), _KB(8));
    m_pMaterialAllocator  = MakeBox< StaticBufferAllocator >(m_RenderDevice, "MaterialPool", sizeof(MaterialData), _KB(8));
    m_pLightAllocator     = MakeBox< StaticBufferAllocator >(m_RenderDevice, "LightPool", sizeof(LightData), 1);

    m_pVertexAllocator          = MakeBox< StaticBufferAllocator >(m_RenderDevice, "VertexPool", sizeof(Vertex), _KB(8LL));
    m_pIndexAllocator           = MakeBox< StaticBufferAllocator >(m_RenderDevice, "IndexPool", sizeof(u32), _KB(8LL));
    m_pInstanceAllocator        = MakeBox< StaticBufferAllocator >(m_RenderDevice, "InstancePool", sizeof(InstanceData), _KB(8LL));
    m_pMeshletAllocator         = MakeBox< StaticBufferAllocator >(m_RenderDevice, "MeshletPool", sizeof(Meshlet), _MB(8LL));
    m_pMeshletVertexAllocator   = MakeBox< StaticBufferAllocator >(m_RenderDevice, "MeshletVertexPool", sizeof(u32), _MB(8LL));
    m_pMeshletTriangleAllocator = MakeBox< StaticBufferAllocator >(m_RenderDevice, "MeshletTrianglePool", sizeof(u32), _MB(8LL) * 3 / 4);

    m_pTLAS = Dx12TopLevelAS::Create(m_RenderDevice, "SceneTLAS");

    // **
    // command signature
    // **
    auto& rm = static_cast<Dx12ResourceManager&>(m_RenderDevice.GetResourceManager());
    m_pRootSignature = rm.GetGlobalRootSignature();

    if (!m_RenderDevice.GetDeviceSettings().bMeshShader)
    {
        m_pIndirectDataAllocator = MakeBox< StaticBufferAllocator >(m_RenderDevice, "IndirectDataPool", sizeof(IndirectDrawData), _KB(8));
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
        m_pIndirectDataAllocator     = MakeBox< StaticBufferAllocator >(m_RenderDevice, "IndirectDataPool", sizeof(IndirectDispatchMeshData), _KB(8));
        m_pIndirectDispatchSignature = new CommandSignature(
            m_RenderDevice,
            CommandSignatureDesc(2, sizeof(IndirectDispatchMeshData))
                .AddConstant(0, 0, 6)
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

void Dx12SceneResource::UpdateSceneResources(const SceneRenderView& sceneView)
{
    using namespace render;

    auto& rm = static_cast<Dx12ResourceManager&>(m_RenderDevice.GetResourceManager());

    ResetFrameBuffers();

    std::vector< TransformData > transforms;
    transforms.reserve(sceneView.transforms.size());
    for (auto& transformView : sceneView.transforms)
    {
        TransformData transform = {};
        transform.mWorldToView = transformView.mWorld;
        transform.mViewToWorld = glm::inverse(transformView.mWorld);
        transforms.push_back(transform);
    }
    UpdateFrameBuffer(transforms.data(), (u32)transforms.size(), sizeof(TransformData), *m_pTransformAllocator, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);

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
        material.emissivePower = materialView.emissivePower;

        material.alphaCutoff        = materialView.alphaCutoff;
        material.clearcoat          = materialView.clearcoat;
        material.clearcoatRoughness = materialView.clearcoatRoughness;
        material.anisotropy         = materialView.anisotropy;
        material.anisotropyRotation = materialView.anisotropyRotation;
        material.sheenColor         = materialView.sheenColor;
        material.sheenRoughness     = materialView.sheenRoughness;
        material.subsurface         = materialView.subsurface;
        material.transmission       = materialView.transmission;
        material.specularStrength   = materialView.specularStrength;
        material.materialType       = 0; // Default

        material.albedoID = INVALID_INDEX;
        if (!materialView.albedoTex.empty())
        {
            auto pMaterialTex = GetOrLoadTexture(materialView.id, materialView.albedoTex);
            if (srvIndexCache.contains(pMaterialTex.get()))
            {
                material.albedoID = srvIndexCache[pMaterialTex.get()];
            }
            else
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
            else
            {
                material.normalID = pMaterialTex->GetShaderResourceHandle();
                srvIndexCache.emplace(pMaterialTex.get(), material.normalID);
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
        else
        {
            material.metallicRoughnessAoID = pORM->GetShaderResourceHandle();
            srvIndexCache.emplace(pORM.get(), material.metallicRoughnessAoID);
        }

        material.emissiveID = INVALID_INDEX;
        if (!materialView.emissionTex.empty())
        {
            auto pMaterialTex = GetOrLoadTexture(materialView.id, materialView.emissionTex);
            if (srvIndexCache.contains(pMaterialTex.get()))
            {
                material.emissiveID = srvIndexCache[pMaterialTex.get()];
            }
            else
            {
                material.emissiveID = pMaterialTex->GetShaderResourceHandle();
                srvIndexCache.emplace(pMaterialTex.get(), material.emissiveID);
            }
        }

        material.clearcoatID = INVALID_INDEX;
        if (!materialView.clearcoatTex.empty())
        {
            auto pMaterialTex = GetOrLoadTexture(materialView.id, materialView.emissionTex);
            if (srvIndexCache.contains(pMaterialTex.get()))
            {
                material.clearcoatID = srvIndexCache[pMaterialTex.get()];
            }
            else
            {
                material.clearcoatID = pMaterialTex->GetShaderResourceHandle();
                srvIndexCache.emplace(pMaterialTex.get(), material.clearcoatID);
            }
        }

        material.sheenID = INVALID_INDEX;
        if (!materialView.sheenTex.empty())
        {
            auto pMaterialTex = GetOrLoadTexture(materialView.id, materialView.sheenTex);
            if (srvIndexCache.contains(pMaterialTex.get()))
            {
                material.sheenID = srvIndexCache[pMaterialTex.get()];
            }
            else
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
            else
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
            else
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
            else
            {
                material.transmissionID = pMaterialTex->GetShaderResourceHandle();
                srvIndexCache.emplace(pMaterialTex.get(), material.transmissionID);
            }
        }

        materials.push_back(material);
    }
    UpdateFrameBuffer(materials.data(), (u32)materials.size(), sizeof(MaterialData), *m_pMaterialAllocator, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);

    u64 totalVertexCount = 0;
    u64 totalIndexCount  = 0;
    for (auto& [id, data] : sceneView.draws)
    {
        if (IsValidIndex(data.mesh))
        {
            assert(data.mesh < sceneView.meshes.size());
            auto& meshView = sceneView.meshes[data.mesh];

            totalVertexCount += meshView.vCount;
            totalIndexCount  += meshView.iCount;
        }
    }
    if (m_pVertexAllocator->GetElementCount() < totalVertexCount)
    {
        m_pVertexAllocator->Resize(totalVertexCount * 2);
    };
    if (m_pIndexAllocator->GetElementCount() < totalIndexCount)
    {
        m_pIndexAllocator->Resize(totalIndexCount * 2);
    };

    u32 instID = 0;
    std::vector< InstanceData >             instances;
    std::vector< IndirectDispatchMeshData > indirects;
    for (auto& [id, data] : sceneView.draws)
    {
        InstanceData             instance = {};
        IndirectDispatchMeshData indirect = {};
        if (IsValidIndex(data.mesh))
        {
            assert(data.mesh < sceneView.meshes.size());
            auto& meshView = sceneView.meshes[data.mesh];

            auto vHandle = GetOrUpdateVertex(meshView.id, meshView.tag, meshView.vData, meshView.vCount);
            {
                auto mHandle  = GetOrUpdateMeshlets(meshView.id, meshView.tag, meshView.mData, meshView.mCount);
                auto mvHandle = GetOrUpdateMeshletVertices(meshView.id, meshView.tag, meshView.mvData, meshView.mvCount);
                auto mtHandle = GetOrUpdateMeshletTriangles(meshView.id, meshView.tag, meshView.mtData, meshView.mtCount);

                indirect.dispatch.ThreadGroupCountX = mHandle.count;
                indirect.dispatch.ThreadGroupCountY = 1;
                indirect.dispatch.ThreadGroupCountZ = 1;

                indirect.vOffset  = vHandle.offset;
                indirect.mOffset  = mHandle.offset;
                indirect.mvOffset = mvHandle.offset;
                indirect.mtOffset = mtHandle.offset;

                assert(IsValidIndex(data.transform) && data.transform < sceneView.transforms.size());
                indirect.transformID = data.transform;

                //indirect.sphereBounds = float4(meshView.sphere.Center(), meshView.sphere.Radius());

                indirect.materialID = INVALID_INDEX;
                if (IsValidIndex(data.material))
                {
                    assert(data.material < sceneView.materials.size());
                    indirect.materialID = data.material;
                }

                indirects.push_back(indirect);
            }
            {
                auto iHandle = GetOrUpdateIndex(meshView.id, meshView.tag, meshView.iData, meshView.iCount);
                GetOrCreateBLAS(meshView.tag, vHandle, iHandle);

                instance.vOffset = vHandle.offset;
                instance.iOffset = iHandle.offset;

                instance.materialID = INVALID_INDEX;
                if (IsValidIndex(data.material))
                {
                    assert(data.material < sceneView.materials.size());
                    instance.materialID = data.material;
                }
                instances.push_back(instance);
            }
            {
                auto& transformView = sceneView.transforms[data.transform];

                auto blasIter = m_BLASCache.find(meshView.tag);
                if (blasIter == m_BLASCache.end() || !blasIter->second->IsBuilt())
                    continue;

                const mat4& m = transformView.mWorld;

                // glm::mat4 (column-major) ¡æ 3x4 row-major
                render::AccelerationStructureInstanceDesc inst = {};
                inst.transform[0][0] = m[0][0]; inst.transform[0][1] = m[1][0]; inst.transform[0][2] = m[2][0]; inst.transform[0][3] = m[3][0];
                inst.transform[1][0] = m[0][1]; inst.transform[1][1] = m[1][1]; inst.transform[1][2] = m[2][1]; inst.transform[1][3] = m[3][1];
                inst.transform[2][0] = m[0][2]; inst.transform[2][1] = m[1][2]; inst.transform[2][2] = m[2][2]; inst.transform[2][3] = m[3][2];

                inst.instanceID                          = instID++;
                inst.pBLAS                               = blasIter->second.get();
                inst.instanceContributionToHitGroupIndex = 0;

                m_pTLAS->AddInstance(inst);
            }

            m_NumMeshes++;
        }
    }
    if (m_pTLAS->NumInstances() > 0)
    {
        m_pTLAS->Prepare();
    }
    BuildAccelerationStructures();

    UpdateFrameBuffer(instances.data(), (u32)instances.size(), sizeof(InstanceData), *m_pInstanceAllocator, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
    UpdateFrameBuffer(indirects.data(), (u32)indirects.size(), sizeof(IndirectDispatchMeshData), *m_pIndirectDataAllocator, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
    UpdateFrameBuffer(&sceneView.light, 1, sizeof(LightData), *m_pLightAllocator, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);

    auto ApplyJittering = [viewport = sceneView.viewport](const mat4& m_, float2 jitter)
        {
            mat4 m = m_;
            m[2][0] += (jitter.x * 2.0f - 1.0f) / viewport.x;
            m[2][1] += (jitter.y * 2.0f - 1.0f) / viewport.y;

            return m;
        };

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
    memcpy(m_pCameraBuffer->GetSystemMemoryAddress(), &m_CameraCache, sizeof(m_CameraCache));

    SceneEnvironmentData sceneEnvironmentData =
    {
        .atmosphere = sceneView.atmosphere.data,
        .cloud      = sceneView.cloud.data
    };
    memcpy(m_pSceneEnvironmentBuffer->GetSystemMemoryAddress(), &sceneEnvironmentData, sizeof(sceneEnvironmentData));
}

void Dx12SceneResource::BindSceneResources(render::CommandContext& context)
{
    auto& rm = static_cast<Dx12ResourceManager&>(m_RenderDevice.GetResourceManager());
    const auto& pGlobalRootSignature = rm.GetGlobalRootSignature();

    Dx12CommandContext& rhicontext = static_cast<Dx12CommandContext&>(context);
    const auto& d3d12CommandList2 = rhicontext.GetD3D12CommandList();

    auto cameraRootIdx = pGlobalRootSignature->GetRootIndex(D3D12_ROOT_PARAMETER_TYPE_CBV, GLOBAL_DESCRIPTOR_SPACE, 0);
    d3d12CommandList2->SetComputeRootConstantBufferView(cameraRootIdx, m_pCameraBuffer->GpuAddress());
    d3d12CommandList2->SetGraphicsRootConstantBufferView(cameraRootIdx, m_pCameraBuffer->GpuAddress());

    auto vRootIdx = pGlobalRootSignature->GetRootIndex(D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS, GLOBAL_DESCRIPTOR_SPACE, 1);
    d3d12CommandList2->SetComputeRoot32BitConstant(vRootIdx, m_pVertexAllocator->GetBuffer()->GetShaderResourceHandle(), 0);
    d3d12CommandList2->SetGraphicsRoot32BitConstant(vRootIdx, m_pVertexAllocator->GetBuffer()->GetShaderResourceHandle(), 0);

    auto iRootIdx = pGlobalRootSignature->GetRootIndex(D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS, GLOBAL_DESCRIPTOR_SPACE, 2);
    d3d12CommandList2->SetComputeRoot32BitConstant(iRootIdx, m_pIndexAllocator->GetBuffer()->GetShaderResourceHandle(), 0);
    d3d12CommandList2->SetGraphicsRoot32BitConstant(iRootIdx, m_pIndexAllocator->GetBuffer()->GetShaderResourceHandle(), 0);

    auto instRootIdx = pGlobalRootSignature->GetRootIndex(D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS, GLOBAL_DESCRIPTOR_SPACE, 3);
    d3d12CommandList2->SetComputeRoot32BitConstant(instRootIdx, m_pInstanceAllocator->GetBuffer()->GetShaderResourceHandle(), 0);
    d3d12CommandList2->SetGraphicsRoot32BitConstant(instRootIdx, m_pInstanceAllocator->GetBuffer()->GetShaderResourceHandle(), 0);

    auto mRootIdx = pGlobalRootSignature->GetRootIndex(D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS, GLOBAL_DESCRIPTOR_SPACE, 4);
    d3d12CommandList2->SetComputeRoot32BitConstant(mRootIdx, m_pMeshletAllocator->GetBuffer()->GetShaderResourceHandle(), 0);
    d3d12CommandList2->SetGraphicsRoot32BitConstant(mRootIdx, m_pMeshletAllocator->GetBuffer()->GetShaderResourceHandle(), 0);

    auto mvRootIdx = pGlobalRootSignature->GetRootIndex(D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS, GLOBAL_DESCRIPTOR_SPACE, 5);
    d3d12CommandList2->SetComputeRoot32BitConstant(mvRootIdx, m_pMeshletVertexAllocator->GetBuffer()->GetShaderResourceHandle(), 0);
    d3d12CommandList2->SetGraphicsRoot32BitConstant(mvRootIdx, m_pMeshletVertexAllocator->GetBuffer()->GetShaderResourceHandle(), 0);

    auto mtRootIdx = pGlobalRootSignature->GetRootIndex(D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS, GLOBAL_DESCRIPTOR_SPACE, 6);
    d3d12CommandList2->SetComputeRoot32BitConstant(mtRootIdx, m_pMeshletTriangleAllocator->GetBuffer()->GetShaderResourceHandle(), 0);
    d3d12CommandList2->SetGraphicsRoot32BitConstant(mtRootIdx, m_pMeshletTriangleAllocator->GetBuffer()->GetShaderResourceHandle(), 0);

    auto transformBufferIdx = pGlobalRootSignature->GetRootIndex(D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS, GLOBAL_DESCRIPTOR_SPACE, 7);
    d3d12CommandList2->SetComputeRoot32BitConstant(transformBufferIdx, GetTransformBuffer()->GetShaderResourceHandle(), 0);
    d3d12CommandList2->SetGraphicsRoot32BitConstant(transformBufferIdx, GetTransformBuffer()->GetShaderResourceHandle(), 0);

    auto materialBufferIdx = pGlobalRootSignature->GetRootIndex(D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS, GLOBAL_DESCRIPTOR_SPACE, 8);
    d3d12CommandList2->SetComputeRoot32BitConstant(materialBufferIdx, GetMaterialBuffer()->GetShaderResourceHandle(), 0);
    d3d12CommandList2->SetGraphicsRoot32BitConstant(materialBufferIdx, GetMaterialBuffer()->GetShaderResourceHandle(), 0);

    auto lightRootIdx = pGlobalRootSignature->GetRootIndex(D3D12_ROOT_PARAMETER_TYPE_CBV, GLOBAL_DESCRIPTOR_SPACE, 9);
    d3d12CommandList2->SetComputeRootConstantBufferView(lightRootIdx, GetLightBuffer()->GpuAddress());
    d3d12CommandList2->SetGraphicsRootConstantBufferView(lightRootIdx, GetLightBuffer()->GpuAddress());

    auto sceneEnvironmentRootIdx = pGlobalRootSignature->GetRootIndex(D3D12_ROOT_PARAMETER_TYPE_CBV, GLOBAL_DESCRIPTOR_SPACE, 10);
    d3d12CommandList2->SetComputeRootConstantBufferView(sceneEnvironmentRootIdx, m_pSceneEnvironmentBuffer->GpuAddress());
    d3d12CommandList2->SetGraphicsRootConstantBufferView(sceneEnvironmentRootIdx, m_pSceneEnvironmentBuffer->GpuAddress());
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
    rm.UploadData(allocation.pBuffer, pData, allocation.sizeInBytes, allocation.offsetInBytes, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    BufferHandle handle = {};
    handle.gpuHandle          = allocation.pBuffer->GetD3D12Resource()->GetGPUVirtualAddress();
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
    rm.UploadData(allocation.pBuffer, pData, allocation.sizeInBytes, allocation.offsetInBytes, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    BufferHandle handle = {};
    handle.gpuHandle          = allocation.pBuffer->GetD3D12Resource()->GetGPUVirtualAddress();
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
    rm.UploadData(allocation.pBuffer, pData, allocation.sizeInBytes, allocation.offsetInBytes, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

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
    rm.UploadData(allocation.pBuffer, pData, allocation.sizeInBytes, allocation.offsetInBytes, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

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

    auto allocation = m_pMeshletTriangleAllocator->Allocate(count, sizeof(u8));
    rm.UploadData(allocation.pBuffer, pData, allocation.sizeInBytes, allocation.offsetInBytes, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    BufferHandle handle = {};
    handle.gpuHandle          = allocation.pBuffer->GetD3D12Resource()->GetGPUVirtualAddress();
    handle.elementSizeInBytes = sizeof(u8);
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
    geom.vertexBufferAddress = vHandle.gpuHandle + (vHandle.offset * vHandle.elementSizeInBytes);
    geom.vertexCount         = vHandle.count;
    geom.vertexStride        = vHandle.elementSizeInBytes;
    geom.indexBufferAddress  = iHandle.gpuHandle + (iHandle.offset * iHandle.elementSizeInBytes);
    geom.indexCount          = iHandle.count;
    geom.geometryFlags       = render::eGeometryFlag_Opaque;
    pBLAS->AddGeometry(geom);
    pBLAS->Prepare();

    m_BLASCache.emplace(tag, pBLAS);
    m_PendingBLASBuilds.push_back(pBLAS.get());
    return pBLAS;
}

Arc< Dx12Texture > Dx12SceneResource::GetOrLoadTexture(u64 entity, const std::string& filepath)
{
    if (m_TextureCache.contains(filepath))
    {
        return m_TextureCache.find(filepath)->second;
    }

    auto& rm   = static_cast<Dx12ResourceManager&>(m_RenderDevice.GetResourceManager());
    auto  pTex = StaticCast<Dx12Texture>(rm.LoadTexture(filepath));

    m_TextureCache.emplace(filepath, pTex);
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

void Dx12SceneResource::UpdateFrameBuffer(const void* pData, u32 count, u64 elementSizeInBytes, StaticBufferAllocator& targetBuffer, D3D12_RESOURCE_STATES stateAfter)
{
    if (count == 0 || elementSizeInBytes == 0)
        return;

    auto& rm = static_cast<Dx12ResourceManager&>(m_RenderDevice.GetResourceManager());

    auto allocation = targetBuffer.Allocate(count);
    rm.UploadData(allocation.pBuffer, pData, allocation.sizeInBytes, allocation.offsetInBytes, stateAfter);
}

void Dx12SceneResource::BuildAccelerationStructures()
{
    bool bHasTLAS        = m_pTLAS->NumInstances() > 0;
    bool bHasPendingBLAS = m_PendingBLASBuilds.empty() == false;
    if (!bHasPendingBLAS && !bHasTLAS)
        return;

    auto pContext = m_RenderDevice.BeginCommand(D3D12_COMMAND_LIST_TYPE_DIRECT);
    auto* cmdList = pContext->GetD3D12CommandList();

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
    return m_pIndirectDataAllocator->GetBuffer();
}

Arc< Dx12StructuredBuffer > Dx12SceneResource::GetTransformBuffer() const
{
    return m_pTransformAllocator->GetBuffer();
}

Arc< Dx12StructuredBuffer > Dx12SceneResource::GetMaterialBuffer() const
{
    return m_pMaterialAllocator->GetBuffer();
}

Arc< Dx12StructuredBuffer > Dx12SceneResource::GetLightBuffer() const
{
    return m_pLightAllocator->GetBuffer();
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
    m_NumMeshes = 0;

    m_pIndirectDataAllocator->Reset();
    m_pTransformAllocator->Reset();
    m_pMaterialAllocator->Reset();
    m_pLightAllocator->Reset();

    m_pVertexAllocator->Reset();
    m_pIndexAllocator->Reset();
    m_pInstanceAllocator->Reset();
    m_pMeshletAllocator->Reset();
    m_pMeshletVertexAllocator->Reset();
    m_pMeshletTriangleAllocator->Reset();

    m_pTLAS->Reset();
    m_PendingBLASBuilds.clear();
}

} // namespace dx12