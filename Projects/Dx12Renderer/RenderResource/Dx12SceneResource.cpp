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

static RootSignature*   s_CombineTexturesRS  = nullptr;
static ComputePipeline* s_CombineTexturesPSO = nullptr;
Arc< Texture > CombineTextures(RenderDevice& renderDevice, const std::wstring& name, Arc< Texture > pTextureR, Arc< Texture > pTextureG, Arc< Texture > pTextureB)
{
    u32 width
        = (u32)std::max({ pTextureR->Desc().Width, pTextureG->Desc().Width, pTextureB->Desc().Width });
    u32 height
        = (u32)std::max({ pTextureR->Desc().Height, pTextureG->Desc().Height, pTextureB->Desc().Height });
    auto pCombinedTexture =
        Texture::Create(
            renderDevice,
            name,
            {
                .desc = CD3DX12_RESOURCE_DESC::Tex2D(
                    DXGI_FORMAT_R8G8B8A8_UNORM, 
                    width, height, 
                    1, 1, 1, 0, 
                    D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
                ),
            });

    auto& context = renderDevice.BeginCommand(D3D12_COMMAND_LIST_TYPE_DIRECT); // use direct queue for state management
    {
        context.SetRenderPipeline(s_CombineTexturesPSO);
        context.SetComputeRootSignature(s_CombineTexturesRS);
        
        context.TransitionBarrier(pTextureR, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, false);
        context.TransitionBarrier(pTextureG, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, false);
        context.TransitionBarrier(pTextureB, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, false);
        context.TransitionBarrier(pCombinedTexture, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        context.StageDescriptors(0, 0,
            {
                pTextureR->GetShaderResourceView(),
                pTextureG->GetShaderResourceView(),
                pTextureB->GetShaderResourceView(),
                pCombinedTexture->GetUnorderedAccessView(0)
            });

        context.Dispatch2D< 16, 16 >(width, height);

        context.TransitionBarrier(pCombinedTexture, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        context.Close();
    }
    context.Execute().Wait();

    return pCombinedTexture;
}

SceneResource::SceneResource(RenderDevice& device)
    : m_RenderDevice(device)
{
    // **
    // scene buffers
    // **
    m_pIndirectDrawAllocator = MakeBox< StaticBufferAllocator >(m_RenderDevice, L"IndirectDrawPool", sizeof(IndirectDrawData) * _KB(8));
    m_pTransformAllocator    = MakeBox< StaticBufferAllocator >(m_RenderDevice, L"TransformPool", sizeof(TransformData) * _KB(8));
    m_pMaterialAllocator     = MakeBox< StaticBufferAllocator >(m_RenderDevice, L"MaterialPool", sizeof(MaterialData) * _KB(8));
    m_pLightAllocator        = MakeBox< StaticBufferAllocator >(m_RenderDevice, L"LightPool", sizeof(LightData));


    // **
    // root signature for scene draw
    // **
    m_pRootSignature = new RootSignature(m_RenderDevice);

    const u32 transformRootIdx = m_pRootSignature->AddConstants(1, 0, 1, D3D12_SHADER_VISIBILITY_VERTEX);
    const u32 materialRootIdx  = m_pRootSignature->AddConstants(1, 0, 1, D3D12_SHADER_VISIBILITY_PIXEL);
    m_pRootSignature->AddCBV(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_NONE, D3D12_SHADER_VISIBILITY_ALL);    // g_Camera
    m_pRootSignature->AddSRV(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_NONE, D3D12_SHADER_VISIBILITY_VERTEX); // g_Transforms
    m_pRootSignature->AddSRV(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_NONE, D3D12_SHADER_VISIBILITY_PIXEL);  // g_Materials
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

SceneResource::~SceneResource()
{
    RELEASE(s_CombineTexturesRS);
    RELEASE(s_CombineTexturesPSO);

    RELEASE(m_pCommandSignature);
    RELEASE(m_pRootSignature);
}

void SceneResource::UpdateSceneResources(const SceneRenderView& sceneView)
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
    std::unordered_map< Texture*, u32 > srvIndexCache;
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
                s_CombineTexturesRS = new RootSignature(m_RenderDevice);

                s_CombineTexturesRS->AddDescriptorTable(
                    DescriptorTable()
                        .AddSRVRange(0, 0, 1)  // g_TextureR
                        .AddSRVRange(1, 0, 1)  // g_TextureG
                        .AddSRVRange(2, 0, 1)  // g_TextureB
						.AddUAVRange(0, 0, 1), // g_CombinedTexture
                    D3D12_SHADER_VISIBILITY_ALL
                );
                s_CombineTexturesRS->AddSampler(0, 0, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_WRAP, 1);
                s_CombineTexturesRS->Build();

                s_CombineTexturesPSO = new ComputePipeline(m_RenderDevice, L"CombineTextures");
                Arc< Shader > pCS
                    = Shader::Create(m_RenderDevice, L"CombineTextures", { .filepath = CSO_PATH.string() + "CombineTexturesCS.cso" });
                s_CombineTexturesPSO->SetShaderModules(std::move(pCS)).SetRootSignature(s_CombineTexturesRS).Build();
            }

            Arc< Texture > pCombiningTextures[3] = {};
            if (!materialView.aoTex.empty())
                pCombiningTextures[0] = GetOrLoadTexture(materialView.id, materialView.aoTex);
            else
                pCombiningTextures[0] = rm.GetFlatWhiteTexture();

            if (!materialView.roughnessTex.empty())
                pCombiningTextures[1] = GetOrLoadTexture(materialView.id, materialView.roughnessTex);
            else
                pCombiningTextures[1] = rm.GetFlatWhiteTexture();

            if (!materialView.metallicTex.empty())
                pCombiningTextures[2] = GetOrLoadTexture(materialView.id, materialView.metallicTex);
            else
                pCombiningTextures[2] = rm.GetFlatWhiteTexture();

            pORM = CombineTextures(m_RenderDevice, L"ORM", pCombiningTextures[0], pCombiningTextures[1], pCombiningTextures[2]);
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
            auto pIB  = GetOrUpdateIndex(meshView.id, meshView.tag, meshView.iData, meshView.iCount);

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

    UpdateFrameBuffer(&sceneView.light.data, 1, sizeof(LightData), *m_pLightAllocator, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
}

Arc< VertexBuffer > SceneResource::GetOrUpdateVertex(u32 entity, const std::string& filepath, const void* pData, u32 count)
{
    auto& commandQueue = m_RenderDevice.GraphicsQueue();

    std::string f = filepath.data();
    if (m_VertexCache.contains(f))
    {
        return m_VertexCache.find(f)->second;
    }

    u64 sizeInBytes = sizeof(Vertex) * count;

    // staging buffer
    ID3D12Resource* d3d12UploadBuffer =
        m_RenderDevice.CreateRHIResource(CD3DX12_RESOURCE_DESC::Buffer(sizeInBytes), D3D12_RESOURCE_STATE_GENERIC_READ, CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD));

    UINT8* pMappedPtr = nullptr;
    CD3DX12_RANGE readRange(0, 0);

    DX_CHECK(d3d12UploadBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pMappedPtr)));
    memcpy(pMappedPtr, pData, sizeInBytes);
    d3d12UploadBuffer->Unmap(0, nullptr);

    // vertex buffer
    auto pVB = MakeArc< VertexBuffer >(
        m_RenderDevice, 
        L"", 
        Buffer::CreationInfo
        {
            ResourceCreationInfo
        	{
                CD3DX12_RESOURCE_DESC::Buffer(sizeInBytes),
            },
            count,
            sizeof(Vertex)
        });

    // copy
    auto& cmdList = commandQueue.Allocate();
    {
        cmdList.TransitionBarrier(pVB, D3D12_RESOURCE_STATE_COPY_DEST);
        cmdList.CopyBuffer(pVB->GetD3D12Resource(), d3d12UploadBuffer, sizeInBytes);
        cmdList.TransitionBarrier(pVB, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
    }
    cmdList.Close();

    auto fenceValue = commandQueue.ExecuteCommandList(&cmdList);
    commandQueue.WaitForFenceValue(fenceValue);

    COM_RELEASE(d3d12UploadBuffer);

    m_VertexCache.emplace(filepath, pVB);
    return pVB;
}

Arc< IndexBuffer > SceneResource::GetOrUpdateIndex(u32 entity, const std::string& filepath, const void* pData, u32 count)
{
    auto& commandQueue = m_RenderDevice.GraphicsQueue();

    std::string f = filepath.data();
    if (m_IndexCache.contains(f))
    {
        return m_IndexCache.find(f)->second;
    }

    u64 sizeInBytes = sizeof(Index) * count;

    // staging buffer
    ID3D12Resource* d3d12UploadBuffer =
        m_RenderDevice.CreateRHIResource(CD3DX12_RESOURCE_DESC::Buffer(sizeInBytes), D3D12_RESOURCE_STATE_GENERIC_READ, CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD));

    UINT8* pMappedPtr = nullptr;
    CD3DX12_RANGE readRange(0, 0);

    ThrowIfFailed(d3d12UploadBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pMappedPtr)));
    memcpy(pMappedPtr, pData, sizeInBytes);
    d3d12UploadBuffer->Unmap(0, nullptr);

    // index buffer
    Buffer::CreationInfo info = {};
    info.desc = CD3DX12_RESOURCE_DESC::Buffer(sizeInBytes);
    info.count = count;
    info.elementSizeInBytes = sizeof(Index);
    auto pIB = MakeArc< IndexBuffer >(
        m_RenderDevice,
        L"",
        Buffer::CreationInfo
        {
            ResourceCreationInfo
        	{
				CD3DX12_RESOURCE_DESC::Buffer(sizeInBytes),
            },
            count,
            sizeof(Index)
		});

    // copy
    auto& cmdList = commandQueue.Allocate();
    {
        cmdList.TransitionBarrier(pIB, D3D12_RESOURCE_STATE_COPY_DEST);
        cmdList.CopyBuffer(pIB->GetD3D12Resource(), d3d12UploadBuffer, sizeInBytes);
        cmdList.TransitionBarrier(pIB, D3D12_RESOURCE_STATE_INDEX_BUFFER);
    }
    cmdList.Close();

    const auto fenceValue = commandQueue.ExecuteCommandList(&cmdList);
    commandQueue.WaitForFenceValue(fenceValue);

    COM_RELEASE(d3d12UploadBuffer);

    m_IndexCache.emplace(filepath, pIB);
    return pIB;
}

Arc< Texture > SceneResource::GetOrLoadTexture(u32 entity, const std::string& filepath)
{
    if (m_TextureCache.contains(filepath))
    {
        return m_TextureCache.find(filepath)->second;
    }

    auto d3d12Device = m_RenderDevice.GetD3D12Device();

    fs::path path = filepath;
    auto extension = path.extension().wstring();

    std::unique_ptr< u8[] > rawData;
    ID3D12Resource* d3d12TexResource = nullptr;

    auto pEmptyTex = MakeArc< Texture >(m_RenderDevice, path.wstring());
    if (extension == L".dds")
    {
        std::vector< D3D12_SUBRESOURCE_DATA > subresourceData;
        DX_CHECK(DirectX::LoadDDSTextureFromFile(
            d3d12Device, path.c_str(), &d3d12TexResource, rawData, subresourceData));

        UINT subresourceSize = (UINT)subresourceData.size();

        pEmptyTex->SetD3D12Resource(d3d12TexResource);
        m_RenderDevice.UpdateSubresources(pEmptyTex, 0, subresourceSize, subresourceData.data());
    }
    else
    {
        D3D12_SUBRESOURCE_DATA subresouceData = {};
        DX_CHECK(DirectX::LoadWICTextureFromFile(
            d3d12Device, path.c_str(), &d3d12TexResource, rawData, subresouceData));

        pEmptyTex->SetD3D12Resource(d3d12TexResource);

        UINT subresouceSize = 1;
        m_RenderDevice.UpdateSubresources(pEmptyTex, 0, subresouceSize, &subresouceData);
    }

    m_TextureCache.emplace(filepath, pEmptyTex);
    return pEmptyTex;
}

Arc< Texture > SceneResource::GetTexture(const std::string& filepath)
{
    std::string f = filepath.data();
    if (m_TextureCache.contains(f))
    {
        return m_TextureCache.find(f)->second;
    }

    return nullptr;
}

void SceneResource::UpdateFrameBuffer(const void* pData, u32 count, u64 elementSizeInBytes, StaticBufferAllocator& targetBuffer, D3D12_RESOURCE_STATES stateAfter)
{
    if (count == 0 || elementSizeInBytes == 0)
        return;

    u64 sizeInBytes = count * elementSizeInBytes;

    // staging buffer
    ID3D12Resource* d3d12UploadBuffer =
        m_RenderDevice.CreateRHIResource(CD3DX12_RESOURCE_DESC::Buffer(sizeInBytes), D3D12_RESOURCE_STATE_GENERIC_READ, CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD));

    UINT8* pMappedPtr = nullptr;
    CD3DX12_RANGE readRange(0, 0);

    DX_CHECK(d3d12UploadBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pMappedPtr)));
    memcpy(pMappedPtr, pData, sizeInBytes);
    d3d12UploadBuffer->Unmap(0, nullptr);

    auto allocation = targetBuffer.Allocate(count, elementSizeInBytes);

    auto& cmdList = m_RenderDevice.BeginCommand(D3D12_COMMAND_LIST_TYPE_DIRECT);
    {
        cmdList.TransitionBarrier(allocation.pBuffer, D3D12_RESOURCE_STATE_COPY_DEST);
        cmdList.CopyBuffer(allocation.pBuffer->GetD3D12Resource(), d3d12UploadBuffer, sizeInBytes);
        cmdList.TransitionBarrier(allocation.pBuffer, stateAfter);
        cmdList.Close();
    }
    cmdList.Execute().Wait();

    COM_RELEASE(d3d12UploadBuffer);
}

ID3D12CommandSignature* SceneResource::GetSceneD3D12CommandSignature() const
{
    return m_pCommandSignature->GetD3D12CommandSignature();
}

Arc< StructuredBuffer > SceneResource::GetIndirectBuffer() const
{
    return m_pIndirectDrawAllocator->GetBuffer();
}

Arc< StructuredBuffer > SceneResource::GetTransformBuffer() const
{
    return m_pTransformAllocator->GetBuffer();
}

Arc< StructuredBuffer > SceneResource::GetMaterialBuffer() const
{
    return m_pMaterialAllocator->GetBuffer();
}

Arc<StructuredBuffer> SceneResource::GetLightBuffer() const
{
    return m_pLightAllocator->GetBuffer();
}

void SceneResource::ResetFrameBuffers()
{
    m_NumMeshes = 0;

    m_pIndirectDrawAllocator->Reset();
    m_pTransformAllocator->Reset();
    m_pMaterialAllocator->Reset();
    m_pLightAllocator->Reset();
}

} // namespace dx12