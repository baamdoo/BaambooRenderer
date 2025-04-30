#include "RendererPch.h"
#include "Dx12ResourceManager.h"
#include "Dx12DescriptorPool.h"
#include "Dx12CommandList.h"
#include "Dx12CommandQueue.h"
#include "RenderResource/Dx12Shader.h"
#include "BaambooUtils/Math.hpp"

namespace dx12
{

ResourceManager::ResourceManager(RenderContext& context)
    : m_RenderContext(context)
{
    for (u32 i = 0; i < D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES; ++i)
        m_pDescriptorPools[i] = 
            new DescriptorPool(context, (D3D12_DESCRIPTOR_HEAP_TYPE)i, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, MAX_NUM_DESCRIPTOR_PER_POOL[i]);
}

ResourceManager::~ResourceManager()
{
    m_BufferPool.Release();
    m_TexturePool.Release();
    m_SamplerPool.Release();
    m_ShaderPool.Release();

    for (u32 i = 0; i < D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES; ++i)
        RELEASE(m_pDescriptorPools[i]);
}

VertexHandle ResourceManager::CreateVertexBuffer(std::wstring_view name, u32 numVertices, u64 elementSizeInBytes, void* data)
{
    auto& commandQueue = m_RenderContext.GetCommandQueue(D3D12_COMMAND_LIST_TYPE_DIRECT);
    auto sizeInBytes = (u64)elementSizeInBytes * numVertices;

    ID3D12Resource* d3d12UploadBuffer =
        m_RenderContext.CreateRHIResource(CD3DX12_RESOURCE_DESC::Buffer(sizeInBytes), D3D12_RESOURCE_STATE_COMMON, CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD));

    Buffer::CreationInfo info = {};
    info.desc = CD3DX12_RESOURCE_DESC::Buffer(sizeInBytes);
    info.heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    info.heapFlags = D3D12_HEAP_FLAG_NONE;
    info.initialState = D3D12_RESOURCE_STATE_COMMON;
    info.count = numVertices;
    info.elementSizeInBytes = elementSizeInBytes;

    auto vb = Create< VertexBuffer >(name, std::move(info));
    auto pvb = Get(vb);

    auto& cmdList = m_RenderContext.AllocateCommandList(D3D12_COMMAND_LIST_TYPE_DIRECT);
    {
        UINT8* pMappedPtr = nullptr;
        CD3DX12_RANGE readRange(0, 0);

        ThrowIfFailed(d3d12UploadBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pMappedPtr)));
        memcpy(pMappedPtr, data, sizeInBytes);
        d3d12UploadBuffer->Unmap(0, nullptr);

        cmdList.TransitionBarrier(pvb, D3D12_RESOURCE_STATE_COPY_DEST);
        cmdList.CopyBuffer(pvb->GetD3D12Resource(), d3d12UploadBuffer, sizeInBytes);
        cmdList.TransitionBarrier(pvb, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
    }
    cmdList.Close();

    auto fenceValue = commandQueue.ExecuteCommandList(&cmdList);
    commandQueue.WaitForFenceValue(fenceValue);

    d3d12UploadBuffer->Release();
    d3d12UploadBuffer = nullptr;

    VertexHandle handle = {};
    handle.vb = static_cast<u32>(vb);
    handle.vOffset = 0;
    handle.vCount = numVertices;
    return handle;
}

IndexHandle ResourceManager::CreateIndexBuffer(std::wstring_view name, u32 numIndices, u64 elementSizeInBytes, void* data)
{
    auto& commandQueue = m_RenderContext.GetCommandQueue(D3D12_COMMAND_LIST_TYPE_DIRECT);
    auto sizeInBytes = (u64)elementSizeInBytes * numIndices;

    ID3D12Resource* d3d12UploadBuffer =
        m_RenderContext.CreateRHIResource(CD3DX12_RESOURCE_DESC::Buffer(sizeInBytes), D3D12_RESOURCE_STATE_COMMON, CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD));

    Buffer::CreationInfo info = {};
    info.desc = CD3DX12_RESOURCE_DESC::Buffer(sizeInBytes);
    info.heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    info.heapFlags = D3D12_HEAP_FLAG_NONE;
    info.initialState = D3D12_RESOURCE_STATE_COMMON;
    info.count = numIndices;
    info.elementSizeInBytes = elementSizeInBytes;

    auto ib = Create< IndexBuffer >(name, std::move(info));
    auto pib = Get(ib);

    auto& cmdList = m_RenderContext.AllocateCommandList(D3D12_COMMAND_LIST_TYPE_DIRECT);
    {
        UINT8* pMappedPtr = nullptr;
        CD3DX12_RANGE readRange(0, 0);

        ThrowIfFailed(d3d12UploadBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pMappedPtr)));
        memcpy(pMappedPtr, data, sizeInBytes);
        d3d12UploadBuffer->Unmap(0, nullptr);

        cmdList.TransitionBarrier(pib, D3D12_RESOURCE_STATE_COPY_DEST);
        cmdList.CopyBuffer(pib->GetD3D12Resource(), d3d12UploadBuffer, sizeInBytes);
        cmdList.TransitionBarrier(pib, D3D12_RESOURCE_STATE_INDEX_BUFFER);
    }
    cmdList.Close();

    auto fenceValue = commandQueue.ExecuteCommandList(&cmdList);
    commandQueue.WaitForFenceValue(fenceValue);

    d3d12UploadBuffer->Release();
    d3d12UploadBuffer = nullptr;

    IndexHandle handle = {};
    handle.ib = static_cast<u32>(ib);
    handle.iOffset = 0;
    handle.iCount = numIndices;
    return handle;
}

TextureHandle ResourceManager::CreateTexture(std::string_view filepath, bool bGenerateMips)
{
    auto d3d12Device = m_RenderContext.GetD3D12Device();

    fs::path path = filepath;
    auto extension = path.extension().wstring();

    std::unique_ptr< uint8_t[] > rawData;
    ID3D12Resource* d3d12TexResource = nullptr;

    auto pTex  = CreateEmpty< Texture >(path.wstring());
    if (extension == L".dds")
    {
        std::vector< D3D12_SUBRESOURCE_DATA > subresouceData;
        ThrowIfFailed(DirectX::LoadDDSTextureFromFile(
            d3d12Device, path.c_str(), &d3d12TexResource, rawData, subresouceData));

        UINT subresouceSize = (UINT)subresouceData.size();

        pTex->SetD3D12Resource(d3d12TexResource);
        m_RenderContext.UpdateSubresources(pTex, 0, subresouceSize, subresouceData.data());
    }
    else
    {
        D3D12_SUBRESOURCE_DATA subresouceData = {};
        ThrowIfFailed(DirectX::LoadWICTextureFromFile(
            d3d12Device, path.c_str(), &d3d12TexResource, rawData, subresouceData));

        UINT subresouceSize = 1;

        pTex->SetD3D12Resource(d3d12TexResource);
        m_RenderContext.UpdateSubresources(pTex, 0, subresouceSize, &subresouceData);
    }

    return TextureHandle(Add< Texture >(pTex));
}

DescriptorAllocation ResourceManager::AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE type, u32 numDescriptors)
{
    return m_pDescriptorPools[type]->Allocate(numDescriptors);
}

}