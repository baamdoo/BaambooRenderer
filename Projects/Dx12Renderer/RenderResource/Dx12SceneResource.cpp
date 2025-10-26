#include "RendererPch.h"
#include "Dx12SceneResource.h"
#include "RenderDevice/Dx12CommandContext.h"
#include "RenderDevice/Dx12CommandQueue.h"
#include "RenderDevice/Dx12Rootsignature.h"
#include "RenderDevice/Dx12CommandSignature.h"
#include "RenderDevice/Dx12BufferAllocator.h"
#include "RenderDevice/Dx12RenderPipeline.h"
#include "RenderDevice/Dx12ResourceManager.h"
#include "RenderResource/Dx12Buffer.h"
#include "RenderResource/Dx12Texture.h"
#include "RenderResource/Dx12Shader.h"
#include "SceneRenderView.h"

namespace dx12
{

static Dx12ComputePipeline* s_CombineTexturesPSO = nullptr;
Arc< Dx12Texture > CombineTextures(Dx12RenderDevice& rd, const std::string& name, Arc< Dx12Texture > pTextureR, Arc< Dx12Texture > pTextureG, Arc< Dx12Texture > pTextureB)
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

        pContext->StageDescriptors(0, 0,
            {
                pTextureR->GetShaderResourceView(),
                pTextureG->GetShaderResourceView(),
                pTextureB->GetShaderResourceView(),
                pCombinedTexture->GetUnorderedAccessView(0)
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
    m_pIndirectDrawAllocator = MakeBox< StaticBufferAllocator >(m_RenderDevice, "IndirectDrawPool", sizeof(IndirectDrawData) * _KB(8));
    m_pTransformAllocator    = MakeBox< StaticBufferAllocator >(m_RenderDevice, "TransformPool", sizeof(TransformData) * _KB(8));
    m_pMaterialAllocator     = MakeBox< StaticBufferAllocator >(m_RenderDevice, "MaterialPool", sizeof(MaterialData) * _KB(8));
    m_pLightAllocator        = MakeBox< StaticBufferAllocator >(m_RenderDevice, "LightPool", sizeof(LightData));


    // **
    // root signature for scene draw
    // **
    m_pRootSignature = MakeArc< Dx12RootSignature >(m_RenderDevice, "SceneResourceRS");

    const u32 transformRootIdx = m_pRootSignature->AddConstants(1, 1, D3D12_SHADER_VISIBILITY_VERTEX);
    const u32 materialRootIdx  = m_pRootSignature->AddConstants(1, 1, D3D12_SHADER_VISIBILITY_PIXEL);
    auto rootIndex = m_pRootSignature->AddCBV(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_NONE, D3D12_SHADER_VISIBILITY_ALL);    // g_Camera
    resourceBindingMapTemp["g_Camera"] = rootIndex;
    rootIndex = m_pRootSignature->AddSRV(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_NONE, D3D12_SHADER_VISIBILITY_VERTEX); // g_Transforms
    resourceBindingMapTemp["g_Transforms"] = rootIndex;
    rootIndex = m_pRootSignature->AddSRV(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_NONE, D3D12_SHADER_VISIBILITY_PIXEL);  // g_Materials
    resourceBindingMapTemp["g_Materials"] = rootIndex;
    m_pRootSignature->AddDescriptorTable(
        DescriptorTable()
            .AddSRVRange(0, 100, UINT_MAX, D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE | D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE, 0), // g_SceneTextures
        D3D12_SHADER_VISIBILITY_PIXEL
    );
    m_pRootSignature->AddSampler(0, 0, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_WRAP, 1);
    m_pRootSignature->Build();


    // **
    // command signature
    // **
    m_pCommandSignature = new CommandSignature(
        m_RenderDevice,
        CommandSignatureDesc(5, sizeof(IndirectDrawData))
            .AddConstant(transformRootIdx, 0, 1)
            .AddConstant(materialRootIdx, 0, 1)
            .AddVertexBufferView(0)
            .AddIndexBufferView()
            .AddDrawIndexed(),
        m_pRootSignature->GetD3D12RootSignature());
}

Dx12SceneResource::~Dx12SceneResource()
{
    RELEASE(s_CombineTexturesPSO);

    RELEASE(m_pCommandSignature);
}

void Dx12SceneResource::UpdateSceneResources(const SceneRenderView& sceneView)
{
    auto& rm = m_RenderDevice.GetResourceManager();

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

    sceneTexSRVs.clear();
    std::vector< MaterialData > materials;
    materials.reserve(sceneView.materials.size());
    std::unordered_map< Dx12Texture*, u32 > srvIndexCache;
    for (auto& materialView : sceneView.materials)
    {
        MaterialData material  = {};
        material.tint          = materialView.tint;
        material.roughness     = materialView.roughness;
        material.metallic      = materialView.metallic;
        material.ior           = materialView.ior;
        material.emissivePower = materialView.emissivePower;

        material.albedoID = INVALID_INDEX;
        if (!materialView.albedoTex.empty())
        {
            auto pAlbedo = GetOrLoadTexture(materialView.id, materialView.albedoTex);
            if (srvIndexCache.contains(pAlbedo.get()))
            {
                material.albedoID = srvIndexCache[pAlbedo.get()];
            }
            else
            {
                sceneTexSRVs.push_back(pAlbedo->GetShaderResourceView());
                material.albedoID = (u32)sceneTexSRVs.size() - 1;
                srvIndexCache.emplace(pAlbedo.get(), material.albedoID);
            }
        }

        material.normalID = INVALID_INDEX;
        if (!materialView.normalTex.empty())
        {
            auto pNormal = GetOrLoadTexture(materialView.id, materialView.normalTex);
            if (srvIndexCache.contains(pNormal.get()))
            {
                material.normalID = srvIndexCache[pNormal.get()];
            }
            else
            {
                sceneTexSRVs.push_back(pNormal->GetShaderResourceView());
                material.normalID = (u32)sceneTexSRVs.size() - 1;
                srvIndexCache.emplace(pNormal.get(), material.normalID);
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
                    = Dx12Shader::Create(m_RenderDevice, "CombineTextures", { .stage = render::eShaderStage::Compute, .filename = "CombineTextures" });
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
            sceneTexSRVs.push_back(pORM->GetShaderResourceView());
            material.metallicRoughnessAoID = (u32)sceneTexSRVs.size() - 1;
            srvIndexCache.emplace(pORM.get(), material.metallicRoughnessAoID);
        }

        material.emissionID = INVALID_INDEX;
        if (!materialView.emissionTex.empty())
        {
            auto pEmission = GetOrLoadTexture(materialView.id, materialView.emissionTex);
            if (srvIndexCache.contains(pEmission.get()))
            {
                material.emissionID = srvIndexCache[pEmission.get()];
            }
            else
            {
                sceneTexSRVs.push_back(pEmission->GetShaderResourceView());
                material.emissionID = (u32)sceneTexSRVs.size() - 1;
                srvIndexCache.emplace(pEmission.get(), material.emissionID);
            }
        }

        materials.push_back(material);
    }
    UpdateFrameBuffer(materials.data(), (u32)materials.size(), sizeof(MaterialData), *m_pMaterialAllocator, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);

    std::vector< IndirectDrawData > indirects;
    for (auto& [id, data] : sceneView.draws)
    {
        IndirectDrawData indirect = {};
        if (IsValidIndex(data.mesh))
        {
            assert(data.mesh < sceneView.meshes.size());
            auto& meshView = sceneView.meshes[data.mesh];

            auto pVB = GetOrUpdateVertex(meshView.id, meshView.tag, meshView.vData, meshView.vCount);
            auto pIB = GetOrUpdateIndex(meshView.id, meshView.tag, meshView.iData, meshView.iCount);

            indirect.draws.IndexCountPerInstance = pIB->GetBufferCount();
            indirect.draws.InstanceCount         = 1;
            indirect.draws.StartIndexLocation    = 0;
            indirect.draws.BaseVertexLocation    = 0;
            indirect.draws.StartInstanceLocation = 0;

            indirect.vbv = pVB->GetBufferView();
            indirect.ibv = pIB->GetBufferView();

            assert(IsValidIndex(data.transform) && data.transform < sceneView.transforms.size());
            indirect.transformID = data.transform;

            indirect.materialID = INVALID_INDEX;
            if (IsValidIndex(data.material))
            {
                assert(data.material < sceneView.materials.size());
                indirect.materialID = data.material;
            }

            indirects.push_back(indirect);
            m_NumMeshes++;
        }
    }
    UpdateFrameBuffer(indirects.data(), (u32)indirects.size(), sizeof(IndirectDrawData), *m_pIndirectDrawAllocator, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);

    UpdateFrameBuffer(&sceneView.light, 1, sizeof(LightData), *m_pLightAllocator, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
}

void Dx12SceneResource::BindSceneResources(render::CommandContext& context)
{
    Dx12CommandContext& rhicontext = static_cast<Dx12CommandContext&>(context);
    if (rhicontext.IsComputeContext())
    {
        rhicontext.SetComputeShaderResourceView("g_Transforms", GetTransformBuffer()->GpuAddress());
        rhicontext.SetComputeShaderResourceView("g_Materials", GetMaterialBuffer()->GpuAddress());
        rhicontext.SetComputeConstantBufferView("g_Lights", GetLightBuffer()->GpuAddress());
    }
    else
    {
        rhicontext.SetGraphicsShaderResourceView("g_Transforms", GetTransformBuffer()->GpuAddress());
        rhicontext.SetGraphicsShaderResourceView("g_Materials", GetMaterialBuffer()->GpuAddress());
        rhicontext.StageDescriptors(5, 0, std::move(sceneTexSRVs));
    }
}

Arc< Dx12VertexBuffer > Dx12SceneResource::GetOrUpdateVertex(u64 entity, const std::string& filepath, const void* pData, u32 count)
{
    std::string f = filepath.data();
    if (m_VertexCache.contains(f))
    {
        return m_VertexCache.find(f)->second;
    }

    u64 sizeInBytes = sizeof(Vertex) * count;

    auto& rm = static_cast<Dx12ResourceManager&>(m_RenderDevice.GetResourceManager());
    auto pVB = Dx12VertexBuffer::Create(m_RenderDevice, "", count);
    rm.UploadData(pVB, pData, sizeInBytes, 0, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);

    m_VertexCache.emplace(filepath, pVB);
    return pVB;
}

Arc< Dx12IndexBuffer > Dx12SceneResource::GetOrUpdateIndex(u64 entity, const std::string& filepath, const void* pData, u32 count)
{
    std::string f = filepath.data();
    if (m_IndexCache.contains(f))
    {
        return m_IndexCache.find(f)->second;
    }

    u64 sizeInBytes = sizeof(Index) * count;

    auto& rm = static_cast<Dx12ResourceManager&>(m_RenderDevice.GetResourceManager());
    auto pIB = Dx12IndexBuffer::Create(m_RenderDevice, "", count);
    rm.UploadData(pIB, pData, sizeInBytes, 0, D3D12_RESOURCE_STATE_INDEX_BUFFER);

    m_IndexCache.emplace(filepath, pIB);
    return pIB;
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

    u64 sizeInBytes = count * elementSizeInBytes;

    auto& rm = static_cast<Dx12ResourceManager&>(m_RenderDevice.GetResourceManager());

    auto allocation = targetBuffer.Allocate(count, elementSizeInBytes);
    rm.UploadData(allocation.pBuffer, pData, sizeInBytes, allocation.offset * elementSizeInBytes, stateAfter);
}

ID3D12CommandSignature* Dx12SceneResource::GetSceneD3D12CommandSignature() const
{
    return m_pCommandSignature->GetD3D12CommandSignature();
}

Arc< Dx12StructuredBuffer > Dx12SceneResource::GetIndirectBuffer() const
{
    return m_pIndirectDrawAllocator->GetBuffer();
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

void Dx12SceneResource::ResetFrameBuffers()
{
    m_NumMeshes = 0;

    m_pIndirectDrawAllocator->Reset();
    m_pTransformAllocator->Reset();
    m_pMaterialAllocator->Reset();
    m_pLightAllocator->Reset();
}

} // namespace dx12