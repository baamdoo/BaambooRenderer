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
GraphicsPipeline::GraphicsPipeline(RenderContext& context, const std::wstring& name)
    : m_RenderContext(context)
    , m_Name(name)
{
    // Default desc values
    m_d3d12PipelineDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    m_d3d12PipelineDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    m_d3d12PipelineDesc.RasterizerState.MultisampleEnable = FALSE;
    m_d3d12PipelineDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    m_d3d12PipelineDesc.DepthStencilState.DepthEnable = FALSE;
    m_d3d12PipelineDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    m_d3d12PipelineDesc.DepthStencilState.StencilEnable = FALSE;
    m_d3d12PipelineDesc.SampleMask = UINT_MAX;
    m_d3d12PipelineDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    m_d3d12PipelineDesc.SampleDesc.Count = 1;
}

GraphicsPipeline::~GraphicsPipeline()
{
    COM_RELEASE(m_d3d12PipelineState);

    m_d3d12InputLayoutDesc.clear();
}

GraphicsPipeline& GraphicsPipeline::SetShaderModules(
    baamboo::ResourceHandle< Shader > vs,
    baamboo::ResourceHandle< Shader > ps,
    baamboo::ResourceHandle< Shader > gs,
    baamboo::ResourceHandle< Shader > hs,
    baamboo::ResourceHandle< Shader > ds)
{
    m_hVS = vs;
    m_hPS = ps;
    m_hGS = gs;
    m_hHS = hs;
    m_hDS = ds;
    assert(m_hVS.IsValid());
    assert(m_hPS.IsValid());

    auto& rm = m_RenderContext.GetResourceManager();
    auto pVS = rm.Get(m_hVS);
    auto pPS = rm.Get(m_hPS);
    auto pGS = rm.Get(m_hGS);
    auto pHS = rm.Get(m_hHS);
    auto pDS = rm.Get(m_hDS);

    SetVertexInputLayout(pVS->GetShaderReflection());


    // **
    // Set pipeline desc
    // **
    m_d3d12PipelineDesc.VS = CD3DX12_SHADER_BYTECODE(pVS->GetShaderBufferPointer(), pVS->GetShaderBufferSize());
    m_d3d12PipelineDesc.PS = CD3DX12_SHADER_BYTECODE(pPS->GetShaderBufferPointer(), pPS->GetShaderBufferSize());
    m_d3d12PipelineDesc.GS = pGS ?
        CD3DX12_SHADER_BYTECODE(pGS->GetShaderBufferPointer(), pGS->GetShaderBufferSize()) : CD3DX12_SHADER_BYTECODE();
    m_d3d12PipelineDesc.HS = pHS ?
        CD3DX12_SHADER_BYTECODE(pHS->GetShaderBufferPointer(), pHS->GetShaderBufferSize()) : CD3DX12_SHADER_BYTECODE();
    m_d3d12PipelineDesc.DS = pDS ?
        CD3DX12_SHADER_BYTECODE(pDS->GetShaderBufferPointer(), pDS->GetShaderBufferSize()) : CD3DX12_SHADER_BYTECODE();

    return *this;
}

GraphicsPipeline& GraphicsPipeline::SetRootSignature(RootSignature* pRootSignature)
{
    assert(pRootSignature);
    m_d3d12PipelineDesc.pRootSignature = pRootSignature->GetD3D12RootSignature();

    return *this;
}

GraphicsPipeline& GraphicsPipeline::SetRenderTargetFormats(const RenderTarget& renderTarget)
{
    u32 numSampling = 0;
    u32 numAttachments = 0;

    const auto& pAttachments = renderTarget.GetTextures();
    for (u32 i = 0; i < eAttachmentPoint::DepthStencil; ++i)
    {
        if (pAttachments[i])
        {
            m_d3d12PipelineDesc.RTVFormats[i] = pAttachments[i]->GetFormat();
            numSampling = std::max(numSampling, pAttachments[i]->GetResourceDesc().SampleDesc.Count);
            numAttachments++;
        }
    }
    m_d3d12PipelineDesc.NumRenderTargets = numAttachments;
    m_d3d12PipelineDesc.SampleDesc.Count = numSampling;

    if (pAttachments[(u32)eAttachmentPoint::DepthStencil])
    {
        m_d3d12PipelineDesc.DepthStencilState.DepthEnable = TRUE;
        m_d3d12PipelineDesc.DSVFormat = pAttachments[(u32)eAttachmentPoint::DepthStencil]->GetFormat();
    }

    return *this;
}

GraphicsPipeline& GraphicsPipeline::SetCullMode(D3D12_CULL_MODE cullMode)
{
    m_d3d12PipelineDesc.RasterizerState.CullMode = cullMode;
    return *this;
}

GraphicsPipeline& GraphicsPipeline::SetFillMode(bool bWireframe)
{
    m_d3d12PipelineDesc.RasterizerState.FillMode = bWireframe ? D3D12_FILL_MODE_WIREFRAME : D3D12_FILL_MODE_SOLID;
    return *this;
}

GraphicsPipeline& GraphicsPipeline::SetBlendEnable(u32 renderTargetIndex, bool bEnable)
{
    assert(renderTargetIndex < m_d3d12PipelineDesc.NumRenderTargets);

    m_d3d12PipelineDesc.BlendState.RenderTarget[renderTargetIndex].BlendEnable = bEnable;
    return *this;
}

GraphicsPipeline& GraphicsPipeline::SetBlendState(u32 renderTargetIndex, D3D12_BLEND srcBlend, D3D12_BLEND dstBlend, D3D12_BLEND srcBlendAlpha, D3D12_BLEND dstBlendAlpha)
{
    assert(renderTargetIndex < m_d3d12PipelineDesc.NumRenderTargets);

    m_d3d12PipelineDesc.BlendState.RenderTarget[renderTargetIndex].SrcBlend = srcBlend;
    m_d3d12PipelineDesc.BlendState.RenderTarget[renderTargetIndex].SrcBlendAlpha = srcBlendAlpha;
    m_d3d12PipelineDesc.BlendState.RenderTarget[renderTargetIndex].DestBlend = dstBlend;
    m_d3d12PipelineDesc.BlendState.RenderTarget[renderTargetIndex].DestBlendAlpha = dstBlendAlpha;

    return *this;
}

GraphicsPipeline& GraphicsPipeline::SetBlendOp(u32 renderTargetIndex, D3D12_BLEND_OP blendOp, D3D12_BLEND_OP blendOpAlpha, D3D12_LOGIC_OP logicOp)
{
    assert(renderTargetIndex < m_d3d12PipelineDesc.NumRenderTargets);

    m_d3d12PipelineDesc.BlendState.RenderTarget[renderTargetIndex].BlendOp = blendOp;
    m_d3d12PipelineDesc.BlendState.RenderTarget[renderTargetIndex].BlendOpAlpha = blendOpAlpha;
    m_d3d12PipelineDesc.BlendState.RenderTarget[renderTargetIndex].LogicOpEnable = logicOp != D3D12_LOGIC_OP_NOOP;
    m_d3d12PipelineDesc.BlendState.RenderTarget[renderTargetIndex].LogicOp = logicOp;

    return *this;
}

GraphicsPipeline& GraphicsPipeline::SetTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE type)
{
    m_d3d12PipelineDesc.PrimitiveTopologyType = type;
    return *this;
}

void GraphicsPipeline::Build()
{
    auto d3d12Device = m_RenderContext.GetD3D12Device();

    ThrowIfFailed(
        d3d12Device->CreateGraphicsPipelineState(&m_d3d12PipelineDesc, IID_PPV_ARGS(&m_d3d12PipelineState))
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

        m_d3d12InputLayoutDesc.push_back(elementDesc);
    }

    m_d3d12PipelineDesc.InputLayout = { m_d3d12InputLayoutDesc.data(), static_cast<u32>(m_d3d12InputLayoutDesc.size())};
}


//-------------------------------------------------------------------------
// Compute pipeline
//-------------------------------------------------------------------------
ComputePipeline::ComputePipeline(RenderContext& context, const std::wstring& name)
	: m_RenderContext(context)
    , m_Name(name)
{
}

ComputePipeline::~ComputePipeline()
{
    COM_RELEASE(m_d3d12PipelineState);
}

void ComputePipeline::SetShaderModules(baamboo::ResourceHandle< Shader > cs)
{
    m_hCS = cs;
    assert(m_hCS.IsValid());

    auto& rm = m_RenderContext.GetResourceManager();
    auto pCS = rm.Get(m_hCS);

    // **
    // Set pipeline desc
    // **
    m_d3d12PipelineDesc.CS = CD3DX12_SHADER_BYTECODE(pCS->GetShaderBufferPointer(), pCS->GetShaderBufferSize());
}

void ComputePipeline::SetRootSignature(RootSignature* pRootSignature)
{
    assert(pRootSignature);
    m_d3d12PipelineDesc.pRootSignature = pRootSignature->GetD3D12RootSignature();
}

void ComputePipeline::Build()
{
    auto d3d12Device = m_RenderContext.GetD3D12Device();

    ThrowIfFailed(
        d3d12Device->CreateComputePipelineState(&m_d3d12PipelineDesc, IID_PPV_ARGS(&m_d3d12PipelineState))
    );

    ThrowIfFailed(m_d3d12PipelineState->SetName(m_Name.c_str()));
}

}