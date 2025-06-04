#include "RendererPch.h"
#include "Dx12RenderPipeline.h"
#include "Dx12RootSignature.h"
#include "Dx12ResourceManager.h"
#include "RenderResource/Dx12Shader.h"
#include "RenderResource/Dx12Texture.h"
#include "RenderResource/Dx12RenderTarget.h"

namespace dx12
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

//-------------------------------------------------------------------------
// Graphics pipeline
//-------------------------------------------------------------------------
GraphicsPipeline::GraphicsPipeline(RenderDevice& device, const std::wstring& name)
    : m_RenderDevice(device)
    , m_Name(name)
{
    // Default desc values
    m_PipelineDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    m_PipelineDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    m_PipelineDesc.RasterizerState.MultisampleEnable = FALSE;
    m_PipelineDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    m_PipelineDesc.DepthStencilState.DepthEnable = FALSE;
    m_PipelineDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    m_PipelineDesc.DepthStencilState.StencilEnable = FALSE;
    m_PipelineDesc.SampleMask = UINT_MAX;
    m_PipelineDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    m_PipelineDesc.SampleDesc.Count = 1;
}

GraphicsPipeline::~GraphicsPipeline()
{
    COM_RELEASE(m_d3d12PipelineState);

    m_InputLayoutDesc.clear();
}

GraphicsPipeline& GraphicsPipeline::SetShaderModules(
    Box< Shader > vs,
    Box< Shader > ps,
    Box< Shader > gs,
    Box< Shader > hs,
    Box< Shader > ds)
{
    m_VS = std::move(vs);
    m_PS = std::move(ps);
    m_GS = std::move(gs);
    m_HS = std::move(hs);
    m_DS = std::move(ds);
    assert(m_VS);
    assert(m_PS);

    SetVertexInputLayout(m_VS->GetShaderReflection());


    // **
    // Set pipeline desc
    // **
    m_PipelineDesc.VS = CD3DX12_SHADER_BYTECODE(m_VS->GetShaderBufferPointer(), m_VS->GetShaderBufferSize());
    m_PipelineDesc.PS = CD3DX12_SHADER_BYTECODE(m_PS->GetShaderBufferPointer(), m_PS->GetShaderBufferSize());
    m_PipelineDesc.GS = m_GS ?
        CD3DX12_SHADER_BYTECODE(m_GS->GetShaderBufferPointer(), m_GS->GetShaderBufferSize()) : CD3DX12_SHADER_BYTECODE();
    m_PipelineDesc.HS = m_HS ?
        CD3DX12_SHADER_BYTECODE(m_HS->GetShaderBufferPointer(), m_HS->GetShaderBufferSize()) : CD3DX12_SHADER_BYTECODE();
    m_PipelineDesc.DS = m_DS ?
        CD3DX12_SHADER_BYTECODE(m_DS->GetShaderBufferPointer(), m_DS->GetShaderBufferSize()) : CD3DX12_SHADER_BYTECODE();

    return *this;
}

GraphicsPipeline& GraphicsPipeline::SetRootSignature(RootSignature* pRootSignature)
{
    assert(pRootSignature);
    m_PipelineDesc.pRootSignature = pRootSignature->GetD3D12RootSignature();

    return *this;
}

GraphicsPipeline& GraphicsPipeline::SetRenderTargetFormats(const RenderTarget& renderTarget)
{
    u32 numSampling = 0;
    u32 numAttachments = 0;

    const auto& pAttachments = renderTarget.GetAttachments();
    for (u32 i = 0; i < eAttachmentPoint::DepthStencil; ++i)
    {
        if (pAttachments[i])
        {
            m_PipelineDesc.RTVFormats[i] = pAttachments[i]->GetFormat();
            numSampling = std::max(numSampling, pAttachments[i]->GetResourceDesc().SampleDesc.Count);
            numAttachments++;
        }
    }
    m_PipelineDesc.NumRenderTargets = numAttachments;
    m_PipelineDesc.SampleDesc.Count = numSampling;

    if (pAttachments[(u32)eAttachmentPoint::DepthStencil])
    {
        m_PipelineDesc.DepthStencilState.DepthEnable = TRUE;
        m_PipelineDesc.DSVFormat = pAttachments[(u32)eAttachmentPoint::DepthStencil]->GetFormat();
    }

    return *this;
}

GraphicsPipeline& GraphicsPipeline::SetCullMode(D3D12_CULL_MODE cullMode)
{
    m_PipelineDesc.RasterizerState.CullMode = cullMode;
    return *this;
}

GraphicsPipeline& GraphicsPipeline::SetFillMode(bool bWireframe)
{
    m_PipelineDesc.RasterizerState.FillMode = bWireframe ? D3D12_FILL_MODE_WIREFRAME : D3D12_FILL_MODE_SOLID;
    return *this;
}

GraphicsPipeline& GraphicsPipeline::SetBlendEnable(u32 renderTargetIndex, bool bEnable)
{
    assert(renderTargetIndex < m_PipelineDesc.NumRenderTargets);

    m_PipelineDesc.BlendState.RenderTarget[renderTargetIndex].BlendEnable = bEnable;
    return *this;
}

GraphicsPipeline& GraphicsPipeline::SetBlendState(u32 renderTargetIndex, D3D12_BLEND srcBlend, D3D12_BLEND dstBlend, D3D12_BLEND srcBlendAlpha, D3D12_BLEND dstBlendAlpha)
{
    assert(renderTargetIndex < m_PipelineDesc.NumRenderTargets);

    m_PipelineDesc.BlendState.RenderTarget[renderTargetIndex].SrcBlend = srcBlend;
    m_PipelineDesc.BlendState.RenderTarget[renderTargetIndex].SrcBlendAlpha = srcBlendAlpha;
    m_PipelineDesc.BlendState.RenderTarget[renderTargetIndex].DestBlend = dstBlend;
    m_PipelineDesc.BlendState.RenderTarget[renderTargetIndex].DestBlendAlpha = dstBlendAlpha;

    return *this;
}

GraphicsPipeline& GraphicsPipeline::SetBlendOp(u32 renderTargetIndex, D3D12_BLEND_OP blendOp, D3D12_BLEND_OP blendOpAlpha, D3D12_LOGIC_OP logicOp)
{
    assert(renderTargetIndex < m_PipelineDesc.NumRenderTargets);

    m_PipelineDesc.BlendState.RenderTarget[renderTargetIndex].BlendOp = blendOp;
    m_PipelineDesc.BlendState.RenderTarget[renderTargetIndex].BlendOpAlpha = blendOpAlpha;
    m_PipelineDesc.BlendState.RenderTarget[renderTargetIndex].LogicOpEnable = logicOp != D3D12_LOGIC_OP_NOOP;
    m_PipelineDesc.BlendState.RenderTarget[renderTargetIndex].LogicOp = logicOp;

    return *this;
}

GraphicsPipeline& GraphicsPipeline::SetTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE type)
{
    m_PipelineDesc.PrimitiveTopologyType = type;
    return *this;
}

void GraphicsPipeline::Build()
{
    auto d3d12Device = m_RenderDevice.GetD3D12Device();

    ThrowIfFailed(
        d3d12Device->CreateGraphicsPipelineState(&m_PipelineDesc, IID_PPV_ARGS(&m_d3d12PipelineState))
    );

    ThrowIfFailed(m_d3d12PipelineState->SetName(m_Name.c_str()));
}

void GraphicsPipeline::SetVertexInputLayout(ID3D12ShaderReflection* d3d12ShaderReflection)
{
    D3D12_SHADER_DESC d3d12ShaderDesc;
    ThrowIfFailed(d3d12ShaderReflection->GetDesc(&d3d12ShaderDesc));

    for (u32 i = 0; i < d3d12ShaderDesc.InputParameters; i++) 
    {
        D3D12_SIGNATURE_PARAMETER_DESC paramDesc;
        ThrowIfFailed(d3d12ShaderReflection->GetInputParameterDesc(i, &paramDesc));

        D3D12_INPUT_ELEMENT_DESC elementDesc;
        elementDesc.SemanticName = paramDesc.SemanticName;
        elementDesc.SemanticIndex = paramDesc.SemanticIndex;
        elementDesc.Format = GetDXGIFormat(paramDesc.ComponentType, paramDesc.Mask);
        elementDesc.InputSlot = 0;
        elementDesc.AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
        elementDesc.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
        elementDesc.InstanceDataStepRate = 0;

        m_InputLayoutDesc.push_back(elementDesc);
    }

    m_PipelineDesc.InputLayout = { m_InputLayoutDesc.data(), static_cast<u32>(m_InputLayoutDesc.size())};
}


//-------------------------------------------------------------------------
// Compute pipeline
//-------------------------------------------------------------------------
ComputePipeline::ComputePipeline(RenderDevice& device, const std::wstring& name)
	: m_RenderDevice(device)
    , m_Name(name)
{
}

ComputePipeline::~ComputePipeline()
{
    COM_RELEASE(m_d3d12PipelineState);
}

void ComputePipeline::SetShaderModules(Box< Shader > cs)
{
    m_CS = std::move(cs);
    assert(m_CS);

    // **
    // Set pipeline desc
    // **
    m_PipelineDesc.CS = CD3DX12_SHADER_BYTECODE(m_CS->GetShaderBufferPointer(), m_CS->GetShaderBufferSize());
}

void ComputePipeline::SetRootSignature(RootSignature* pRootSignature)
{
    assert(pRootSignature);
    m_PipelineDesc.pRootSignature = pRootSignature->GetD3D12RootSignature();
}

void ComputePipeline::Build()
{
    auto d3d12Device = m_RenderDevice.GetD3D12Device();

    ThrowIfFailed(
        d3d12Device->CreateComputePipelineState(&m_PipelineDesc, IID_PPV_ARGS(&m_d3d12PipelineState))
    );

    ThrowIfFailed(m_d3d12PipelineState->SetName(m_Name.c_str()));
}

}