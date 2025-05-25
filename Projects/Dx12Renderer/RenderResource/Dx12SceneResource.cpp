#include "RendererPch.h"
#include "Dx12SceneResource.h"
#include "RenderDevice/Dx12CommandList.h"
#include "RenderDevice/Dx12CommandQueue.h"
#include "RenderDevice/Dx12Rootsignature.h"
#include "RenderDevice/Dx12CommandSignature.h"
#include "RenderDevice/Dx12BufferAllocator.h"

#include <Scene/SceneRenderView.h>

namespace dx12
{

SceneResource::SceneResource(RenderContext& context)
    : m_RenderContext(context)
{
    // **
    // scene buffers
    // **
    m_pIndirectDrawBufferPool = new StaticBufferAllocator(m_RenderContext, sizeof(IndirectDrawData) * _1MB);
    m_pTransformBufferPool    = new StaticBufferAllocator(m_RenderContext, sizeof(TransformData) * _1MB);
    m_pMaterialBufferPool     = new StaticBufferAllocator(m_RenderContext, sizeof(MaterialData) * _1MB);


    // **
    // root signature for scene draw
    // **
    m_pRootSignature = new RootSignature(m_RenderContext);

    u32 transformRI = m_pRootSignature->AddConstants(0, 0, 1, D3D12_SHADER_VISIBILITY_VERTEX);
    u32 materialRI  = m_pRootSignature->AddConstants(0, 0, 1, D3D12_SHADER_VISIBILITY_PIXEL);
    m_pRootSignature->AddCBV(1, 0, D3D12_ROOT_DESCRIPTOR_FLAG_NONE, D3D12_SHADER_VISIBILITY_VERTEX); // g_Camera
    m_pRootSignature->AddSRV(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_NONE, D3D12_SHADER_VISIBILITY_VERTEX); // g_Transforms
    m_pRootSignature->AddSRV(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_NONE, D3D12_SHADER_VISIBILITY_PIXEL);  // g_Materials
    m_pRootSignature->AddDescriptorTable(
        DescriptorTable()
            .AddSRVRange(0, 100, 32, D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE | D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE, 0), // g_SceneTextures
        D3D12_SHADER_VISIBILITY_PIXEL
    );
    m_pRootSignature->AddSampler(0, 0, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, 1);
    m_pRootSignature->Build();


    // **
    // indirect resources
    // **
    m_pCommandSignature = new CommandSignature(
        m_RenderContext,
        CommandSignatureDesc(5, sizeof(IndirectDrawData))
        .AddConstant(transformRI, 0, 1)
        .AddConstant(materialRI, 0, 1)
        /*.AddVertexBufferView(0)
        .AddIndexBufferView()*/
        .AddDrawIndexed(),
        m_pRootSignature->GetD3D12RootSignature());
}

SceneResource::~SceneResource()
{
    RELEASE(m_pCommandSignature);
    RELEASE(m_pRootSignature);

    RELEASE(m_pMaterialBufferPool);
    RELEASE(m_pTransformBufferPool);
    RELEASE(m_pIndirectDrawBufferPool);
}

void SceneResource::UpdateSceneResources(const SceneRenderView& sceneView)
{
    ResetFrameBuffers();

    std::vector< TransformData > transforms;
    transforms.reserve(sceneView.transforms.size());
    for (auto& transformView : sceneView.transforms)
    {
        TransformData transform = {};
        transform.mWorld = transformView.mWorld;
        transforms.push_back(transform);
    }
    UpdateFrameBuffer(transforms.data(), (u32)transforms.size(), sizeof(TransformData), m_pTransformBufferPool);

    srvs.clear();
    std::vector< MaterialData > materials;
    materials.reserve(sceneView.materials.size());
    for (auto& materialView : sceneView.materials)
    {
        MaterialData material = {};
        material.tint = materialView.tint;
        material.roughness = materialView.roughness;
        material.metallic = materialView.metallic;

        material.albedoID = INVALID_INDEX;
        if (!materialView.albedoTex.empty())
        {
            auto pAlbedo = GetOrLoadTexture(materialView.id, materialView.albedoTex);
            srvs.push_back(pAlbedo->GetShaderResourceView());
            material.albedoID = (u32)srvs.size() - 1;
        }

        material.normalID = INVALID_INDEX;
        if (!materialView.normalTex.empty())
        {
            auto pNormal = GetOrLoadTexture(materialView.id, materialView.normalTex);
            srvs.push_back(pNormal->GetShaderResourceView());
            material.normalID = (u32)srvs.size() - 1;
        }

        material.specularID = INVALID_INDEX;
        if (!materialView.specularTex.empty())
        {
            auto pSpecular = GetOrLoadTexture(materialView.id, materialView.specularTex);
            srvs.push_back(pSpecular->GetShaderResourceView());
            material.specularID = (u32)srvs.size() - 1;
        }

        material.aoID = INVALID_INDEX;
        if (!materialView.aoTex.empty())
        {
            auto pAo = GetOrLoadTexture(materialView.id, materialView.aoTex);
            srvs.push_back(pAo->GetShaderResourceView());
            material.aoID = (u32)srvs.size() - 1;
        }

        material.roughnessID = INVALID_INDEX;
        if (!materialView.roughnessTex.empty())
        {
            auto pRoughness = GetOrLoadTexture(materialView.id, materialView.roughnessTex);
            srvs.push_back(pRoughness->GetShaderResourceView());
            material.roughnessID = (u32)srvs.size() - 1;
        }

        material.metallicID = INVALID_INDEX;
        if (!materialView.metallicTex.empty())
        {
            auto pMetallic = GetOrLoadTexture(materialView.id, materialView.metallicTex);
            srvs.push_back(pMetallic->GetShaderResourceView());
            material.metallicID = (u32)srvs.size() - 1;
        }

        material.emissionID = INVALID_INDEX;
        if (!materialView.emissionTex.empty())
        {
            auto pEmission = GetOrLoadTexture(materialView.id, materialView.emissionTex);
            srvs.push_back(pEmission->GetShaderResourceView());
            material.emissionID = (u32)srvs.size() - 1;
        }

        materials.push_back(material);
    }
    UpdateFrameBuffer(materials.data(), (u32)materials.size(), sizeof(MaterialData), m_pMaterialBufferPool);

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
            indirect.draws.InstanceCount = 1;
            indirect.draws.StartIndexLocation = 0;
            indirect.draws.BaseVertexLocation = 0;
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
        }
        else
        {
            indirect.draws.InstanceCount = 0;
        }

        indirects.push_back(indirect);
        m_numMeshes++;
    }
    UpdateFrameBuffer(indirects.data(), (u32)indirects.size(), sizeof(IndirectDrawData), m_pIndirectDrawBufferPool);
}

VertexBuffer* SceneResource::GetOrUpdateVertex(u32 entity, std::string_view filepath, const void* pData, u32 count)
{
    auto& rm = m_RenderContext.GetResourceManager();
    auto& commandQueue = m_RenderContext.GetCommandQueue(D3D12_COMMAND_LIST_TYPE_DIRECT);

    std::string f = filepath.data();
    if (m_vertexCache.contains(f))
    {
        return m_vertexCache.find(f)->second;
    }

    u64 sizeInBytes = sizeof(Vertex) * count;

    // staging buffer
    ID3D12Resource* d3d12UploadBuffer =
        m_RenderContext.CreateRHIResource(CD3DX12_RESOURCE_DESC::Buffer(sizeInBytes), D3D12_RESOURCE_STATE_COMMON, CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD));

    UINT8* pMappedPtr = nullptr;
    CD3DX12_RANGE readRange(0, 0);

    DX_CHECK(d3d12UploadBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pMappedPtr)));
    memcpy(pMappedPtr, pData, sizeInBytes);
    d3d12UploadBuffer->Unmap(0, nullptr);

    // vertex buffer
    Buffer::CreationInfo info = {};
    info.desc = CD3DX12_RESOURCE_DESC::Buffer(sizeInBytes);
    info.count = count;
    info.elementSizeInBytes = sizeof(Vertex);
    auto vb = rm.Create< VertexBuffer >(L"", std::move(info));
    auto pVB = rm.Get(vb);

    // copy
    auto& cmdList = m_RenderContext.AllocateCommandList(D3D12_COMMAND_LIST_TYPE_DIRECT);
    {
        cmdList.TransitionBarrier(pVB, D3D12_RESOURCE_STATE_COPY_DEST);
        cmdList.CopyBuffer(pVB->GetD3D12Resource(), d3d12UploadBuffer, sizeInBytes);
        cmdList.TransitionBarrier(pVB, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
    }
    cmdList.Close();

    auto fenceValue = commandQueue.ExecuteCommandList(&cmdList);
    commandQueue.WaitForFenceValue(fenceValue);

    COM_RELEASE(d3d12UploadBuffer);

    m_vertexCache.emplace(filepath, pVB);
    return pVB;
}

IndexBuffer* SceneResource::GetOrUpdateIndex(u32 entity, std::string_view filepath, const void* pData, u32 count)
{
    auto& rm = m_RenderContext.GetResourceManager();
    auto& commandQueue = m_RenderContext.GetCommandQueue(D3D12_COMMAND_LIST_TYPE_DIRECT);

    std::string f = filepath.data();
    if (m_indexCache.contains(f))
    {
        return m_indexCache.find(f)->second;
    }

    u64 sizeInBytes = sizeof(Index) * count;

    // staging buffer
    ID3D12Resource* d3d12UploadBuffer =
        m_RenderContext.CreateRHIResource(CD3DX12_RESOURCE_DESC::Buffer(sizeInBytes), D3D12_RESOURCE_STATE_COMMON, CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD));

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
    auto ib = rm.Create< IndexBuffer >(L"", std::move(info));
    auto pIB = rm.Get(ib);

    // copy
    auto& cmdList = m_RenderContext.AllocateCommandList(D3D12_COMMAND_LIST_TYPE_DIRECT);
    {
        cmdList.TransitionBarrier(pIB, D3D12_RESOURCE_STATE_COPY_DEST);
        cmdList.CopyBuffer(pIB->GetD3D12Resource(), d3d12UploadBuffer, sizeInBytes);
        cmdList.TransitionBarrier(pIB, D3D12_RESOURCE_STATE_INDEX_BUFFER);
    }
    cmdList.Close();

    auto fenceValue = commandQueue.ExecuteCommandList(&cmdList);
    commandQueue.WaitForFenceValue(fenceValue);

    COM_RELEASE(d3d12UploadBuffer);

    m_indexCache.emplace(filepath, pIB);
    return pIB;
}

Texture* SceneResource::GetOrLoadTexture(u32 entity, std::string_view filepath)
{
    auto& rm = m_RenderContext.GetResourceManager();

    std::string f = filepath.data();
    if (m_textureCache.contains(f))
    {
        return m_textureCache.find(f)->second;
    }

    auto d3d12Device = m_RenderContext.GetD3D12Device();

    fs::path path = filepath;
    auto extension = path.extension().wstring();

    std::unique_ptr< uint8_t[] > rawData;
    ID3D12Resource* d3d12TexResource = nullptr;

    auto pTex = rm.CreateEmpty< Texture >(path.wstring());
    if (extension == L".dds")
    {
        std::vector< D3D12_SUBRESOURCE_DATA > subresouceData;
        DX_CHECK(DirectX::LoadDDSTextureFromFile(
            d3d12Device, path.c_str(), &d3d12TexResource, rawData, subresouceData));

        UINT subresouceSize = (UINT)subresouceData.size();

        pTex->SetD3D12Resource(d3d12TexResource);
        m_RenderContext.UpdateSubresources(pTex, 0, subresouceSize, subresouceData.data());
    }
    else
    {
        D3D12_SUBRESOURCE_DATA subresouceData = {};
        DX_CHECK(DirectX::LoadWICTextureFromFile(
            d3d12Device, path.c_str(), &d3d12TexResource, rawData, subresouceData));

        pTex->SetD3D12Resource(d3d12TexResource, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

        UINT subresouceSize = 1;
        m_RenderContext.UpdateSubresources(pTex, 0, subresouceSize, &subresouceData);
    }
    rm.Add< Texture >(pTex);

    m_textureCache.emplace(filepath, pTex);
    return pTex;
}

void SceneResource::UpdateFrameBuffer(const void* pData, u32 count, u64 elementSizeInBytes, StaticBufferAllocator* pTargetBuffer)
{
    if (count == 0 || elementSizeInBytes == 0)
        return;

    auto& rm = m_RenderContext.GetResourceManager();
    u64 sizeInBytes = count * elementSizeInBytes;

    // staging buffer
    ID3D12Resource* d3d12UploadBuffer =
        m_RenderContext.CreateRHIResource(CD3DX12_RESOURCE_DESC::Buffer(sizeInBytes), D3D12_RESOURCE_STATE_COMMON, CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD));

    UINT8* pMappedPtr = nullptr;
    CD3DX12_RANGE readRange(0, 0);

    DX_CHECK(d3d12UploadBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pMappedPtr)));
    memcpy(pMappedPtr, pData, sizeInBytes);
    d3d12UploadBuffer->Unmap(0, nullptr);

    auto allocation = pTargetBuffer->Allocate(count, elementSizeInBytes);

    auto& cmdQueue = m_RenderContext.GetCommandQueue(D3D12_COMMAND_LIST_TYPE_COPY);
    auto& cmdList = m_RenderContext.AllocateCommandList(D3D12_COMMAND_LIST_TYPE_COPY);
    cmdList.CopyBuffer(rm.Get(allocation.buffer)->GetD3D12Resource(), d3d12UploadBuffer, sizeInBytes);
    cmdList.Close();

    auto fenceValue = cmdQueue.ExecuteCommandList(&cmdList);
    cmdQueue.WaitForFenceValue(fenceValue);

    COM_RELEASE(d3d12UploadBuffer);
}

ID3D12CommandSignature* SceneResource::GetSceneD3D12CommandSignature() const
{
    return m_pCommandSignature->GetD3D12CommandSignature();
}

StructuredBuffer* SceneResource::GetIndirectBuffer() const
{
    return m_pIndirectDrawBufferPool->GetBuffer();
}

StructuredBuffer* SceneResource::GetTransformBuffer() const
{
    return m_pTransformBufferPool->GetBuffer();
}

StructuredBuffer* SceneResource::GetMaterialBuffer() const
{
    return m_pMaterialBufferPool->GetBuffer();
}

void SceneResource::ResetFrameBuffers()
{
    m_numMeshes = 0;

    m_pIndirectDrawBufferPool->Reset();
    m_pTransformBufferPool->Reset();
    m_pMaterialBufferPool->Reset();
}

} // namespace dx12