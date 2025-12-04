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
    m_PipelineDesc.DepthStencilState.DepthFunc     = D3D12_COMPARISON_FUNC_LESS_EQUAL;
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
                auto rootIndex = m_pRootSignature->GetRootIndex(space, descriptor.baseRegister);

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
                auto rootIndex = m_pRootSignature->GetRootIndex(space, descriptor.baseRegister);

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