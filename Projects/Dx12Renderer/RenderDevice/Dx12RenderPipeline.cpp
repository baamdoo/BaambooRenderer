#include "RendererPch.h"
#include "Dx12RenderPipeline.h"
#include "Dx12RootSignature.h"
#include "Dx12ResourceManager.h"

#include "RenderResource/Dx12Shader.h"
#include "RenderResource/Dx12Texture.h"
#include "RenderResource/Dx12RenderTarget.h"
#include "RenderResource/Dx12SceneResource.h"

namespace dx12
{

using namespace render;

#pragma region ConvertToDx12
#define DX12_PIPELINE_PRIMITIVETOPOLOGY(topology) ConvertToDx12PrimitiveTopology(topology)
D3D12_PRIMITIVE_TOPOLOGY_TYPE ConvertToDx12PrimitiveTopology(render::ePrimitiveTopology topology)
{
    switch (topology)
    {
    case ePrimitiveTopology::Point    : return D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
    case ePrimitiveTopology::Line     : return D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
    case ePrimitiveTopology::Triangle : return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    case ePrimitiveTopology::Patch    : return D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;

    default:
        assert(false && "Invalid primitive topology!"); break;
    }

    return D3D12_PRIMITIVE_TOPOLOGY_TYPE_UNDEFINED;
}

#define DX12_PIPELINE_CULLMODE(mode) ConvertToDx12CullMode(mode)
D3D12_CULL_MODE ConvertToDx12CullMode(render::eCullMode mode)
{
    switch (mode)
    {
    case eCullMode::None  : return D3D12_CULL_MODE_NONE;
    case eCullMode::Front : return D3D12_CULL_MODE_FRONT;
    case eCullMode::Back  : return D3D12_CULL_MODE_BACK;

    default:
        assert(false && "Invalid cull mode!"); break;
    }

    return D3D12_CULL_MODE_NONE;
}

#define DX12_PIPELINE_LOGICOP(op) ConvertToDx12LogicOp(op)
D3D12_LOGIC_OP ConvertToDx12LogicOp(render::eLogicOp op)
{
    switch (op)
    {
    case eLogicOp::None    : return D3D12_LOGIC_OP_NOOP;
    case eLogicOp::Clear   : return D3D12_LOGIC_OP_CLEAR;
    case eLogicOp::Set     : return D3D12_LOGIC_OP_SET;
    case eLogicOp::Copy    : return D3D12_LOGIC_OP_COPY;
    case eLogicOp::CopyInv : return D3D12_LOGIC_OP_COPY_INVERTED;

    default:
        assert(false && "Invalid logic op!"); break;
    }

    return D3D12_LOGIC_OP_NOOP;
}

#define DX12_PIPELINE_BLENDOP(op) ConvertToDx12BlendOp(op)
D3D12_BLEND_OP ConvertToDx12BlendOp(render::eBlendOp op)
{
    switch (op)
    {
    case eBlendOp::Add         : return D3D12_BLEND_OP_ADD;
    case eBlendOp::Subtract    : return D3D12_BLEND_OP_SUBTRACT;
    case eBlendOp::SubtractInv : return D3D12_BLEND_OP_REV_SUBTRACT;
    case eBlendOp::Min         : return D3D12_BLEND_OP_MIN;
    case eBlendOp::Max         : return D3D12_BLEND_OP_MAX;

    default:
        assert(false && "Invalid blend op!"); break;
    }

    return D3D12_BLEND_OP_ADD;
}

#define DX12_PIPELINE_BLENDFACTOR(factor) ConvertToDx12BlendFactor(factor)
D3D12_BLEND ConvertToDx12BlendFactor(render::eBlendFactor factor)
{
    switch (factor)
    {
    case eBlendFactor::Zero             : return D3D12_BLEND_ZERO;
    case eBlendFactor::One              : return D3D12_BLEND_ONE;
    case eBlendFactor::SrcColor         : return D3D12_BLEND_SRC_COLOR;
    case eBlendFactor::SrcColorInv      : return D3D12_BLEND_INV_SRC_COLOR;
    case eBlendFactor::SrcAlpha         : return D3D12_BLEND_SRC_ALPHA;
    case eBlendFactor::SrcAlphaInv      : return D3D12_BLEND_INV_SRC_ALPHA;
    case eBlendFactor::DstColor         : return D3D12_BLEND_DEST_COLOR;
    case eBlendFactor::DstColorInv      : return D3D12_BLEND_INV_DEST_COLOR;
    case eBlendFactor::DstAlpha         : return D3D12_BLEND_DEST_ALPHA;
    case eBlendFactor::DstAlphaInv      : return D3D12_BLEND_INV_DEST_ALPHA;
    case eBlendFactor::SrcAlphaSaturate : return D3D12_BLEND_SRC_ALPHA_SAT;

    default:
        assert(false && "Invalid blend factor!"); break;
    }

    return D3D12_BLEND_ZERO;
}
#pragma endregion


namespace
{

DXGI_FORMAT GetDXGIFormat(D3D_REGISTER_COMPONENT_TYPE componentType, BYTE mask)
{
    if (componentType == D3D_REGISTER_COMPONENT_FLOAT32)
    {
        switch (mask)
        {
        case 0x1: return DXGI_FORMAT_R32_FLOAT;
        case 0x3: return DXGI_FORMAT_R32G32_FLOAT;
        case 0x7: return DXGI_FORMAT_R32G32B32_FLOAT;
        case 0xF: return DXGI_FORMAT_R32G32B32A32_FLOAT;
        }
    }
    else if (componentType == D3D_REGISTER_COMPONENT_UINT32)
    {
        switch (mask)
        {
        case 0x1: return DXGI_FORMAT_R32_UINT;
        case 0x3: return DXGI_FORMAT_R32G32_UINT;
        case 0x7: return DXGI_FORMAT_R32G32B32_UINT;
        case 0xF: return DXGI_FORMAT_R32G32B32A32_UINT;
        }
    }

    return DXGI_FORMAT_UNKNOWN;
}

}

//-------------------------------------------------------------------------
// Graphics pipeline
//-------------------------------------------------------------------------
Dx12GraphicsPipeline::Dx12GraphicsPipeline(Dx12RenderDevice& rd, const char* name)
    : render::GraphicsPipeline(name)
    , m_RenderDevice(rd)
{
    // Default desc values
    m_PipelineDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);

    m_PipelineDesc.RasterizerState                   = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    m_PipelineDesc.RasterizerState.MultisampleEnable = FALSE;

    m_PipelineDesc.DepthStencilState               = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    m_PipelineDesc.DepthStencilState.DepthEnable   = FALSE;
    m_PipelineDesc.DepthStencilState.DepthFunc     = D3D12_COMPARISON_FUNC_GREATER; // Reversed-Z
    m_PipelineDesc.DepthStencilState.StencilEnable = FALSE;

    m_PipelineDesc.SampleMask            = UINT_MAX;
    m_PipelineDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    m_PipelineDesc.SampleDesc.Count      = 1;

    auto& rm = static_cast<Dx12ResourceManager&>(m_RenderDevice.GetResourceManager());
    m_pRootSignature = rm.GetGlobalRootSignature();
}

Dx12GraphicsPipeline::~Dx12GraphicsPipeline()
{
    COM_RELEASE(m_d3d12PipelineState);

    m_InputLayoutDesc.clear();
}

GraphicsPipeline& Dx12GraphicsPipeline::SetRenderTarget(Arc< render::RenderTarget > pRenderTarget)
{
    u32 numSampling    = 0;
    u32 numAttachments = 0;

    const auto& pAttachments = pRenderTarget->GetAttachments();
    for (u32 i = 0; i < eAttachmentPoint::DepthStencil; ++i)
    {
        if (auto rhiAttachment = StaticCast<Dx12Texture>(pAttachments[i]))
        {
            m_PipelineDesc.RTVFormats[i] = rhiAttachment->GetFormat();

            numSampling = std::max(numSampling, rhiAttachment->Desc().SampleDesc.Count);
            numAttachments++;
        }
    }
    m_PipelineDesc.NumRenderTargets = numAttachments;
    m_PipelineDesc.SampleDesc.Count = numSampling;

    if (auto rhiAttachment = StaticCast<Dx12Texture>(pAttachments[eAttachmentPoint::DepthStencil]))
    {
        m_PipelineDesc.DepthStencilState.DepthEnable = TRUE;
        m_PipelineDesc.DSVFormat = rhiAttachment->GetFormat();
    }

    return *this;
}

GraphicsPipeline& Dx12GraphicsPipeline::SetFillMode(bool bWireframe)
{
    m_PipelineDesc.RasterizerState.FillMode = bWireframe ? D3D12_FILL_MODE_WIREFRAME : D3D12_FILL_MODE_SOLID;
    return *this;
}

GraphicsPipeline& Dx12GraphicsPipeline::SetCullMode(render::eCullMode cullMode)
{
    m_PipelineDesc.RasterizerState.CullMode = DX12_PIPELINE_CULLMODE(cullMode);
    return *this;
}

GraphicsPipeline& Dx12GraphicsPipeline::SetTopology(ePrimitiveTopology topology)
{
    m_PipelineDesc.PrimitiveTopologyType = DX12_PIPELINE_PRIMITIVETOPOLOGY(topology);
    return *this;
}

GraphicsPipeline& Dx12GraphicsPipeline::SetDepthTestEnable(bool bEnable, eCompareOp compareOp)
{
    m_PipelineDesc.DepthStencilState.DepthEnable = bEnable;
    m_PipelineDesc.DepthStencilState.DepthFunc   = DX12_COMPAREOP(compareOp);
    return *this;
}

GraphicsPipeline& Dx12GraphicsPipeline::SetDepthWriteEnable(bool bEnable, eCompareOp compareOp)
{
    m_PipelineDesc.DepthStencilState.DepthEnable    = bEnable;
    m_PipelineDesc.DepthStencilState.DepthWriteMask = bEnable ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
    m_PipelineDesc.DepthStencilState.DepthFunc      = DX12_COMPAREOP(compareOp);
    return *this;
}

GraphicsPipeline& Dx12GraphicsPipeline::SetLogicOp(render::eLogicOp logicOp)
{
    for (u32 i = 0; i < m_PipelineDesc.NumRenderTargets; ++i)
    {
        m_PipelineDesc.BlendState.RenderTarget[i].LogicOp = DX12_PIPELINE_LOGICOP(logicOp);
    }

    return *this;
}

GraphicsPipeline& Dx12GraphicsPipeline::SetBlendEnable(u32 renderTargetIndex, bool bEnable)
{
    assert(renderTargetIndex < m_PipelineDesc.NumRenderTargets);

    m_PipelineDesc.BlendState.RenderTarget[renderTargetIndex].BlendEnable = bEnable;
    return *this;
}

GraphicsPipeline& Dx12GraphicsPipeline::SetColorBlending(u32 renderTargetIndex, render::eBlendFactor srcBlend, render::eBlendFactor dstBlend, render::eBlendOp blendOp)
{
    assert(renderTargetIndex < m_PipelineDesc.NumRenderTargets);

    m_PipelineDesc.BlendState.RenderTarget[renderTargetIndex].BlendOp   = DX12_PIPELINE_BLENDOP(blendOp);
    m_PipelineDesc.BlendState.RenderTarget[renderTargetIndex].SrcBlend  = DX12_PIPELINE_BLENDFACTOR(srcBlend);
    m_PipelineDesc.BlendState.RenderTarget[renderTargetIndex].DestBlend = DX12_PIPELINE_BLENDFACTOR(dstBlend);

    return *this;
}

GraphicsPipeline& Dx12GraphicsPipeline::SetAlphaBlending(u32 renderTargetIndex, render::eBlendFactor srcBlend, render::eBlendFactor dstBlend, render::eBlendOp blendOp)
{
    assert(renderTargetIndex < m_PipelineDesc.NumRenderTargets);

    m_PipelineDesc.BlendState.RenderTarget[renderTargetIndex].BlendOpAlpha   = DX12_PIPELINE_BLENDOP(blendOp);
    m_PipelineDesc.BlendState.RenderTarget[renderTargetIndex].SrcBlendAlpha  = DX12_PIPELINE_BLENDFACTOR(srcBlend);
    m_PipelineDesc.BlendState.RenderTarget[renderTargetIndex].DestBlendAlpha = DX12_PIPELINE_BLENDFACTOR(dstBlend);

    return *this;
}

void Dx12GraphicsPipeline::Build()
{
    if (m_bMeshShader)
    {
        auto d3d12MS = StaticCast<Dx12Shader>(m_pMS);
        auto d3d12AS = StaticCast<Dx12Shader>(m_pTS);
        auto d3d12PS = StaticCast<Dx12Shader>(m_pPS);
        assert(d3d12MS && d3d12PS);

        MeshPipelineStream Stream;

        Stream.MS = CD3DX12_SHADER_BYTECODE(d3d12MS->GetShaderBufferPointer(), d3d12MS->GetShaderBufferSize());
        Stream.PS = CD3DX12_SHADER_BYTECODE(d3d12PS->GetShaderBufferPointer(), d3d12PS->GetShaderBufferSize());
        Stream.AS = d3d12AS ?
            CD3DX12_SHADER_BYTECODE(d3d12AS->GetShaderBufferPointer(), d3d12AS->GetShaderBufferSize()) : CD3DX12_SHADER_BYTECODE();

        D3D12_RT_FORMAT_ARRAY rtvFormats = {};
        rtvFormats.NumRenderTargets = m_PipelineDesc.NumRenderTargets;
        memcpy(rtvFormats.RTFormats, m_PipelineDesc.RTVFormats, sizeof(DXGI_FORMAT) * 8);

        Stream.RTVFormats = rtvFormats;
        Stream.DSVFormat  = m_PipelineDesc.DSVFormat;
        Stream.SampleDesc = m_PipelineDesc.SampleDesc;

        Stream.RasterizerState   = CD3DX12_RASTERIZER_DESC(m_PipelineDesc.RasterizerState);
        Stream.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(m_PipelineDesc.DepthStencilState);
        Stream.BlendState        = CD3DX12_BLEND_DESC(m_PipelineDesc.BlendState);

        ParseRootParameters(d3d12MS->Reflection());
        ParseRootParameters(d3d12PS->Reflection());
        if (d3d12AS) ParseRootParameters(d3d12AS->Reflection());
        Stream.pRootSignature = m_pRootSignature->GetD3D12RootSignature();

        D3D12_PIPELINE_STATE_STREAM_DESC streamDesc = {};
        streamDesc.SizeInBytes                   = sizeof(MeshPipelineStream);
        streamDesc.pPipelineStateSubobjectStream = &Stream;

        auto d3d12Device = m_RenderDevice.GetD3D12Device();
        ThrowIfFailed(
            d3d12Device->CreatePipelineState(&streamDesc, IID_PPV_ARGS(&m_d3d12PipelineState))
        );
    }
    else
    {
        auto d3d12VS = StaticCast<Dx12Shader>(m_pVS);
        auto d3d12PS = StaticCast<Dx12Shader>(m_pPS);
        auto d3d12GS = StaticCast<Dx12Shader>(m_pGS);
        auto d3d12HS = StaticCast<Dx12Shader>(m_pHS);
        auto d3d12DS = StaticCast<Dx12Shader>(m_pDS);
        assert(d3d12VS && d3d12PS);

        m_PipelineDesc.VS = CD3DX12_SHADER_BYTECODE(d3d12VS->GetShaderBufferPointer(), d3d12VS->GetShaderBufferSize());
        m_PipelineDesc.PS = CD3DX12_SHADER_BYTECODE(d3d12PS->GetShaderBufferPointer(), d3d12PS->GetShaderBufferSize());
        m_PipelineDesc.GS = d3d12GS ?
            CD3DX12_SHADER_BYTECODE(d3d12GS->GetShaderBufferPointer(), d3d12GS->GetShaderBufferSize()) : CD3DX12_SHADER_BYTECODE();
        m_PipelineDesc.HS = d3d12HS ?
            CD3DX12_SHADER_BYTECODE(d3d12HS->GetShaderBufferPointer(), d3d12HS->GetShaderBufferSize()) : CD3DX12_SHADER_BYTECODE();
        m_PipelineDesc.DS = d3d12DS ?
            CD3DX12_SHADER_BYTECODE(d3d12DS->GetShaderBufferPointer(), d3d12DS->GetShaderBufferSize()) : CD3DX12_SHADER_BYTECODE();

        SetVertexInputLayout(d3d12VS->GetD3D12ShaderReflection());

        // Root Signature
        ParseRootParameters(d3d12VS->Reflection());
        ParseRootParameters(d3d12PS->Reflection());
        if (d3d12GS) ParseRootParameters(d3d12GS->Reflection());
        if (d3d12HS) ParseRootParameters(d3d12HS->Reflection());
        if (d3d12DS) ParseRootParameters(d3d12DS->Reflection());
        m_PipelineDesc.pRootSignature = m_pRootSignature->GetD3D12RootSignature();

        auto d3d12Device = m_RenderDevice.GetD3D12Device();
        ThrowIfFailed(
            d3d12Device->CreateGraphicsPipelineState(&m_PipelineDesc, IID_PPV_ARGS(&m_d3d12PipelineState))
        );
    }
}

void Dx12GraphicsPipeline::SetVertexInputLayout(ID3D12ShaderReflection* d3d12ShaderReflection)
{
    D3D12_SHADER_DESC d3d12ShaderDesc;
    ThrowIfFailed(d3d12ShaderReflection->GetDesc(&d3d12ShaderDesc));

    for (u32 i = 0; i < d3d12ShaderDesc.InputParameters; i++) 
    {
        D3D12_SIGNATURE_PARAMETER_DESC paramDesc;
        ThrowIfFailed(d3d12ShaderReflection->GetInputParameterDesc(i, &paramDesc));

        D3D12_INPUT_ELEMENT_DESC elementDesc = {};
        elementDesc.SemanticName         = paramDesc.SemanticName;
        elementDesc.SemanticIndex        = paramDesc.SemanticIndex;
        elementDesc.Format               = GetDXGIFormat(paramDesc.ComponentType, paramDesc.Mask);
        elementDesc.InputSlot            = 0;
        elementDesc.AlignedByteOffset    = D3D12_APPEND_ALIGNED_ELEMENT;
        elementDesc.InputSlotClass       = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
        elementDesc.InstanceDataStepRate = 0;

        m_InputLayoutDesc.push_back(elementDesc);
    }

    m_PipelineDesc.InputLayout = { m_InputLayoutDesc.data(), static_cast<u32>(m_InputLayoutDesc.size())};
}

void Dx12GraphicsPipeline::ParseRootParameters(const Dx12Shader::ShaderReflection& reflection)
{
    for (const auto& [space, descriptors] : reflection.descriptors)
    {
        for (const auto& descriptor : descriptors)
        {
            switch (descriptor.rangeType)
            {
            case D3D12_DESCRIPTOR_RANGE_TYPE_SRV:
            case D3D12_DESCRIPTOR_RANGE_TYPE_UAV:
            case D3D12_DESCRIPTOR_RANGE_TYPE_CBV:
            {
                D3D12_ROOT_PARAMETER_TYPE type = 
                    space == ROOT_CONSTANT_SPACE ? D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS : 
                    descriptor.rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_CBV ? D3D12_ROOT_PARAMETER_TYPE_CBV :
                    descriptor.rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_UAV ? D3D12_ROOT_PARAMETER_TYPE_UAV : D3D12_ROOT_PARAMETER_TYPE_SRV;

                auto rootIndex = m_pRootSignature->GetRootIndex(type, space, descriptor.baseRegister);

                m_ResourceBindingMap.emplace(descriptor.name, rootIndex);
                break;
            }
            case D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER:
                break;
            }
        }
    }
}


//-------------------------------------------------------------------------
// Compute pipeline
//-------------------------------------------------------------------------
Dx12ComputePipeline::Dx12ComputePipeline(Dx12RenderDevice& rd, const char* name)
	: render::ComputePipeline(name)
    , m_RenderDevice(rd)
{
    auto& rm = static_cast<Dx12ResourceManager&>(m_RenderDevice.GetResourceManager());
    m_pRootSignature = rm.GetGlobalRootSignature();
}

Dx12ComputePipeline::~Dx12ComputePipeline()
{
    COM_RELEASE(m_d3d12PipelineState);
}

void Dx12ComputePipeline::Build()
{
    auto d3d12CS = StaticCast<Dx12Shader>(m_pCS);
    assert(d3d12CS);

    m_PipelineDesc.CS = CD3DX12_SHADER_BYTECODE(d3d12CS->GetShaderBufferPointer(), d3d12CS->GetShaderBufferSize());

    // Root Signature
    ParseRootParameters(d3d12CS->Reflection());
    m_PipelineDesc.pRootSignature = m_pRootSignature->GetD3D12RootSignature();

    auto d3d12Device = m_RenderDevice.GetD3D12Device();
    ThrowIfFailed(
        d3d12Device->CreateComputePipelineState(&m_PipelineDesc, IID_PPV_ARGS(&m_d3d12PipelineState))
    );
}

void Dx12ComputePipeline::ParseRootParameters(const Dx12Shader::ShaderReflection& reflection)
{
    for (const auto& [space, descriptors] : reflection.descriptors)
    {
        for (const auto& descriptor : descriptors)
        {
            switch (descriptor.rangeType)
            {
            case D3D12_DESCRIPTOR_RANGE_TYPE_SRV:
            case D3D12_DESCRIPTOR_RANGE_TYPE_UAV:
            case D3D12_DESCRIPTOR_RANGE_TYPE_CBV:
            {
                D3D12_ROOT_PARAMETER_TYPE type =
                    space == ROOT_CONSTANT_SPACE ? D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS : 
                    descriptor.rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_CBV ? D3D12_ROOT_PARAMETER_TYPE_CBV :
                    descriptor.rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_UAV ? D3D12_ROOT_PARAMETER_TYPE_UAV : D3D12_ROOT_PARAMETER_TYPE_SRV;

                auto rootIndex = m_pRootSignature->GetRootIndex(type, space, descriptor.baseRegister);

                m_ResourceBindingMap.emplace(descriptor.name, rootIndex);
                break;
            }
            case D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER:
                break;
            }
        }
    }
}


//-------------------------------------------------------------------------
// DXR Pipeline
//-------------------------------------------------------------------------
namespace
{
    constexpr u64 SBT_TABLE_ALIGNMENT  = D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT;  // 64
    constexpr u64 SBT_RECORD_ALIGNMENT = D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT; // 32
}

Dx12RaytracingPipeline::Dx12RaytracingPipeline(Dx12RenderDevice& rd, const char* name)
    : render::RaytracingPipeline(name)
    , m_RenderDevice(rd)
{
    auto& rm = static_cast<Dx12ResourceManager&>(m_RenderDevice.GetResourceManager());
    m_pGlobalRootSignature = rm.GetGlobalRootSignature();
}

Dx12RaytracingPipeline::~Dx12RaytracingPipeline()
{
    COM_RELEASE(m_d3d12StateObjectProperties);
    COM_RELEASE(m_d3d12StateObject);
}

void Dx12RaytracingPipeline::Build()
{
    auto pLib = StaticCast< Dx12Shader >(m_pShaderLibrary);
    assert(pLib && "Shader library must be set before Build()!");
    assert(pLib->IsLibrary());

    ParseRootParameters(pLib->Reflection());
    BuildLocalRootSignature(pLib->Reflection());
    BuildStateObject();
}

const void* Dx12RaytracingPipeline::GetShaderIdentifier(const std::string& exportName) const
{
    auto wName = ConvertToWString(exportName);
    return m_d3d12StateObjectProperties->GetShaderIdentifier(wName.c_str());
}

void Dx12RaytracingPipeline::BuildStateObject()
{
    auto d3d12Lib = StaticCast< Dx12Shader >(m_pShaderLibrary);
    assert(d3d12Lib && "Shader library must be set before Build()!");

    ParseRootParameters(d3d12Lib->Reflection());

    std::wstring wRayGenExport = ConvertToWString(m_RayGenExport);

    std::vector< std::wstring > wMissExports; wMissExports.reserve(m_MissExports.size());
    for (const auto& name : m_MissExports)
        wMissExports.push_back(ConvertToWString(name));

    std::vector< std::wstring > wHitGroupExports;     wHitGroupExports.reserve(m_HitGroups.size());
    std::vector< std::wstring > wClosestHitExports;   wClosestHitExports.reserve(m_HitGroups.size());
    std::vector< std::wstring > wAnyHitExports;       wAnyHitExports.reserve(m_HitGroups.size());
    std::vector< std::wstring > wIntersectionExports; wIntersectionExports.reserve(m_HitGroups.size());
    for (const auto& hg : m_HitGroups)
    {
        wHitGroupExports.push_back(ConvertToWString(hg.hitGroupName));
        wClosestHitExports.push_back(ConvertToWString(hg.closestHitShaderExport));
        wAnyHitExports.push_back(ConvertToWString(hg.anyHitShaderExport));
        wIntersectionExports.push_back(ConvertToWString(hg.intersectionShaderExport));
    }

    CD3DX12_STATE_OBJECT_DESC stateObjectDesc{ D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE };
    {
        auto pLib = stateObjectDesc.CreateSubobject< CD3DX12_DXIL_LIBRARY_SUBOBJECT >();

        D3D12_SHADER_BYTECODE libBytecode = {};
        libBytecode.pShaderBytecode = d3d12Lib->GetShaderBufferPointer();
        libBytecode.BytecodeLength  = d3d12Lib->GetShaderBufferSize();
        pLib->SetDXILLibrary(&libBytecode);

        pLib->DefineExport(wRayGenExport.c_str());
        for (const auto& wMiss : wMissExports)
            pLib->DefineExport(wMiss.c_str());

        for (size_t i = 0; i < m_HitGroups.size(); ++i)
        {
            if (!wClosestHitExports[i].empty())
                pLib->DefineExport(wClosestHitExports[i].c_str());
            if (!wAnyHitExports[i].empty())
                pLib->DefineExport(wAnyHitExports[i].c_str());
            if (!wIntersectionExports[i].empty())
                pLib->DefineExport(wIntersectionExports[i].c_str());
        }
    }
    {
        for (size_t i = 0; i < m_HitGroups.size(); ++i)
        {
            auto pHitGroup = stateObjectDesc.CreateSubobject< CD3DX12_HIT_GROUP_SUBOBJECT >();
            pHitGroup->SetHitGroupExport(wHitGroupExports[i].c_str());
            pHitGroup->SetHitGroupType(
                wIntersectionExports[i].empty()
                ? D3D12_HIT_GROUP_TYPE_TRIANGLES
                : D3D12_HIT_GROUP_TYPE_PROCEDURAL_PRIMITIVE);

            if (!wClosestHitExports[i].empty())
                pHitGroup->SetClosestHitShaderImport(wClosestHitExports[i].c_str());
            if (!wAnyHitExports[i].empty())
                pHitGroup->SetAnyHitShaderImport(wAnyHitExports[i].c_str());
            if (!wIntersectionExports[i].empty())
                pHitGroup->SetIntersectionShaderImport(wIntersectionExports[i].c_str());
        }
    }
    {
        auto pShaderConfig = stateObjectDesc.CreateSubobject< CD3DX12_RAYTRACING_SHADER_CONFIG_SUBOBJECT >();
        pShaderConfig->Config(m_MaxPayloadSizeInBytes, m_MaxAttributeSizeInBytes);
    }
    {
        auto pPipelineConfig = stateObjectDesc.CreateSubobject< CD3DX12_RAYTRACING_PIPELINE_CONFIG_SUBOBJECT >();
        pPipelineConfig->Config(m_MaxRecursionDepth);
    }
    {
        auto pGlobalRS = stateObjectDesc.CreateSubobject< CD3DX12_GLOBAL_ROOT_SIGNATURE_SUBOBJECT >();
        pGlobalRS->SetRootSignature(m_pGlobalRootSignature->GetD3D12RootSignature());
    }
    if (m_pMissLocalRootSignature)
    {
        auto pLocalRS = stateObjectDesc.CreateSubobject< CD3DX12_LOCAL_ROOT_SIGNATURE_SUBOBJECT >();
        pLocalRS->SetRootSignature(m_pMissLocalRootSignature->GetD3D12RootSignature());

        auto* pAssociation = stateObjectDesc.CreateSubobject< CD3DX12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT >();
        pAssociation->SetSubobjectToAssociate(*pLocalRS);
        for (const auto& wName : wMissExports)
        {
            pAssociation->AddExport(wName.c_str());
        }
    }
    if (m_pHitGroupLocalRootSignature)
    {
        auto pLocalRS = stateObjectDesc.CreateSubobject< CD3DX12_LOCAL_ROOT_SIGNATURE_SUBOBJECT >();
        pLocalRS->SetRootSignature(m_pHitGroupLocalRootSignature->GetD3D12RootSignature());

        auto* pAssociation = stateObjectDesc.CreateSubobject< CD3DX12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT >();
        pAssociation->SetSubobjectToAssociate(*pLocalRS);
        for (const auto& wName : wHitGroupExports)
        {
            pAssociation->AddExport(wName.c_str());
        }
    }

    auto d3d12Device = m_RenderDevice.GetD3D12Device();
    ThrowIfFailed(
        d3d12Device->CreateStateObject(stateObjectDesc, IID_PPV_ARGS(&m_d3d12StateObject))
    );

    ThrowIfFailed(
        m_d3d12StateObject->QueryInterface(IID_PPV_ARGS(&m_d3d12StateObjectProperties))
    );
}

void Dx12RaytracingPipeline::BuildLocalRootSignature(const Dx12Shader::ShaderReflection& reflection)
{
    auto missIT = reflection.descriptors.find(MISS_ARGUMENT_SPACE);
    if (missIT != reflection.descriptors.end() && !missIT->second.empty())
    {
        std::vector< Dx12Shader::DescriptorInfo > localDescriptors = missIT->second;
        std::sort(localDescriptors.begin(), localDescriptors.end(),
            [](const Dx12Shader::DescriptorInfo& a, const Dx12Shader::DescriptorInfo& b)
                {
                    return a.baseRegister < b.baseRegister;
                });

        m_pMissLocalRootSignature = MakeArc< Dx12RootSignature >(
            m_RenderDevice,
            m_Name + "_MissLocalRS",
            D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE
        );

        for (const auto& desc : localDescriptors)
        {
            switch (desc.rangeType)
            {
            case D3D12_DESCRIPTOR_RANGE_TYPE_SRV:
                m_pMissLocalRootSignature->AddSRV(desc.baseRegister, MISS_ARGUMENT_SPACE);
                break;

            case D3D12_DESCRIPTOR_RANGE_TYPE_UAV:
                m_pMissLocalRootSignature->AddUAV(desc.baseRegister, MISS_ARGUMENT_SPACE);
                break;

            case D3D12_DESCRIPTOR_RANGE_TYPE_CBV:
                m_pMissLocalRootSignature->AddConstants(desc.baseRegister, MISS_ARGUMENT_SPACE, desc.numDescriptors);
                break;

            default:
                assert(false && "Invalid entry!");
                break;
            }
        }

        m_pMissLocalRootSignature->Build();
    }

    auto hgIT = reflection.descriptors.find(HITGROUP_ARGUMENT_SPACE);
    if (hgIT != reflection.descriptors.end() && !hgIT->second.empty())
    {
        std::vector< Dx12Shader::DescriptorInfo > localDescriptors = hgIT->second;
        std::sort(localDescriptors.begin(), localDescriptors.end(),
            [](const Dx12Shader::DescriptorInfo& a, const Dx12Shader::DescriptorInfo& b)
            {
                return a.baseRegister < b.baseRegister;
            });

        m_pHitGroupLocalRootSignature = MakeArc< Dx12RootSignature >(
            m_RenderDevice,
            m_Name + "_HitGroupLocalRS",
            D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE
        );

        for (const auto& desc : localDescriptors)
        {
            switch (desc.rangeType)
            {
            case D3D12_DESCRIPTOR_RANGE_TYPE_SRV:
                m_pHitGroupLocalRootSignature->AddSRV(desc.baseRegister, HITGROUP_ARGUMENT_SPACE);
                break;

            case D3D12_DESCRIPTOR_RANGE_TYPE_UAV:
                m_pHitGroupLocalRootSignature->AddUAV(desc.baseRegister, HITGROUP_ARGUMENT_SPACE);
                break;

            case D3D12_DESCRIPTOR_RANGE_TYPE_CBV:
                m_pHitGroupLocalRootSignature->AddConstants(desc.baseRegister, HITGROUP_ARGUMENT_SPACE, desc.numDescriptors);
                break;

            default:
                assert(false && "Invalid entry!");
                break;
            }
        }

        m_pHitGroupLocalRootSignature->Build();
    }
}

void Dx12RaytracingPipeline::ParseRootParameters(const Dx12Shader::ShaderReflection& reflection)
{
    for (const auto& [space, descriptors] : reflection.descriptors)
    {
        for (const auto& descriptor : descriptors)
        {
            switch (descriptor.rangeType)
            {
            case D3D12_DESCRIPTOR_RANGE_TYPE_SRV:
            case D3D12_DESCRIPTOR_RANGE_TYPE_UAV:
            case D3D12_DESCRIPTOR_RANGE_TYPE_CBV:
            {
                D3D12_ROOT_PARAMETER_TYPE type =
                    space == ROOT_CONSTANT_SPACE ? D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS :
                    descriptor.rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_CBV ? D3D12_ROOT_PARAMETER_TYPE_CBV :
                    descriptor.rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_UAV ? D3D12_ROOT_PARAMETER_TYPE_UAV : D3D12_ROOT_PARAMETER_TYPE_SRV;

                auto rootIndex = m_pGlobalRootSignature->GetRootIndex(type, space, descriptor.baseRegister);

                m_ResourceBindingMap.emplace(descriptor.name, rootIndex);
                break;
            }
            case D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER:
                break;
            }
        }
    }
}

}