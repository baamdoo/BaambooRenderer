#include "RendererPch.h"
#include "Dx12RenderPipeline.h"
#include "Dx12RootSignature.h"

#include "RenderResource/Dx12Shader.h"
#include "RenderResource/Dx12Texture.h"
#include "RenderResource/Dx12RenderTarget.h"

namespace dx12
{

bool IsSucceeded(HRESULT hr, const IUnknown* d3d12Current)
{
    if (FAILED(hr) && !d3d12Current)
        ThrowIfFailed(hr);

    return SUCCEEDED(hr);
}

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

    m_pRootSignature = m_RenderDevice.GetGlobalRootSignature();
}

Dx12GraphicsPipeline::~Dx12GraphicsPipeline()
{
    m_RenderDevice.DeferRelease(m_d3d12PipelineState);
}

GraphicsPipeline& Dx12GraphicsPipeline::SetRenderTarget(Arc< render::RenderTarget > pRenderTarget)
{
    u32 numAttachments = 0;
    DXGI_SAMPLE_DESC sampleDesc = { 1, 0 };
    bool bHasSampleDesc = false;

    const auto validateSampleDesc = [&](const Dx12Texture& attachment)
    {
        const DXGI_SAMPLE_DESC attachmentSampleDesc = attachment.Desc().SampleDesc;
        if (!bHasSampleDesc)
        {
            sampleDesc = attachmentSampleDesc;
            bHasSampleDesc = true;
            return;
        }

        BB_ASSERT(sampleDesc.Count == attachmentSampleDesc.Count &&
                  sampleDesc.Quality == attachmentSampleDesc.Quality,
                  "Render target attachments must use the same sample count and quality");
    };

    const auto& pAttachments = pRenderTarget->GetAttachments();
    for (u32 i = 0; i < eAttachmentPoint::DepthStencil; ++i)
    {
        if (auto rhiAttachment = StaticCast<Dx12Texture>(pAttachments[i]))
        {
            m_PipelineDesc.RTVFormats[i] = rhiAttachment->GetFormat();

            validateSampleDesc(*rhiAttachment);
            numAttachments++;
        }
    }
    m_PipelineDesc.NumRenderTargets = numAttachments;

    if (auto rhiAttachment = StaticCast<Dx12Texture>(pAttachments[eAttachmentPoint::DepthStencil]))
    {
        validateSampleDesc(*rhiAttachment);
        m_PipelineDesc.DepthStencilState.DepthEnable = TRUE;
        m_PipelineDesc.DSVFormat = rhiAttachment->GetFormat();
    }

    m_PipelineDesc.SampleDesc = sampleDesc;

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

    switch (topology)
    {
    case ePrimitiveTopology::Point: 
        m_D3DPrimitiveTopology = D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
    	break;
    case ePrimitiveTopology::Line: 
    	m_D3DPrimitiveTopology = D3D_PRIMITIVE_TOPOLOGY_LINELIST;
        break;
    case ePrimitiveTopology::Triangle: 
    	m_D3DPrimitiveTopology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST; 
    	break;
    case ePrimitiveTopology::Patch: 
    	m_D3DPrimitiveTopology = D3D_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST; 
    	break;
    default: 
        __debugbreak();
    	m_D3DPrimitiveTopology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
    	break;
    }

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
    const BOOL bEnable = logicOp != eLogicOp::None;
    for (u32 i = 0; i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
    {
        m_PipelineDesc.BlendState.RenderTarget[i].LogicOpEnable = bEnable;
        m_PipelineDesc.BlendState.RenderTarget[i].LogicOp       = DX12_PIPELINE_LOGICOP(logicOp);
    }

    return *this;
}

GraphicsPipeline& Dx12GraphicsPipeline::SetBlendEnable(u32 renderTargetIndex, bool bEnable)
{
    m_PipelineDesc.BlendState.RenderTarget[renderTargetIndex].BlendEnable = bEnable;
    return *this;
}

GraphicsPipeline& Dx12GraphicsPipeline::SetColorBlending(u32 renderTargetIndex, render::eBlendFactor srcBlend, render::eBlendFactor dstBlend, render::eBlendOp blendOp)
{
    m_PipelineDesc.BlendState.RenderTarget[renderTargetIndex].BlendEnable = TRUE;
    m_PipelineDesc.BlendState.RenderTarget[renderTargetIndex].BlendOp     = DX12_PIPELINE_BLENDOP(blendOp);
    m_PipelineDesc.BlendState.RenderTarget[renderTargetIndex].SrcBlend    = DX12_PIPELINE_BLENDFACTOR(srcBlend);
    m_PipelineDesc.BlendState.RenderTarget[renderTargetIndex].DestBlend   = DX12_PIPELINE_BLENDFACTOR(dstBlend);

    return *this;
}

GraphicsPipeline& Dx12GraphicsPipeline::SetAlphaBlending(u32 renderTargetIndex, render::eBlendFactor srcBlend, render::eBlendFactor dstBlend, render::eBlendOp blendOp)
{
    m_PipelineDesc.BlendState.RenderTarget[renderTargetIndex].BlendEnable    = TRUE;
    m_PipelineDesc.BlendState.RenderTarget[renderTargetIndex].BlendOpAlpha   = DX12_PIPELINE_BLENDOP(blendOp);
    m_PipelineDesc.BlendState.RenderTarget[renderTargetIndex].SrcBlendAlpha  = DX12_PIPELINE_BLENDFACTOR(srcBlend);
    m_PipelineDesc.BlendState.RenderTarget[renderTargetIndex].DestBlendAlpha = DX12_PIPELINE_BLENDFACTOR(dstBlend);

    return *this;
}

bool Dx12GraphicsPipeline::Build()
{
    for (u32 i = 0; i < m_PipelineDesc.NumRenderTargets; ++i)
    {
        const auto& blendDesc = m_PipelineDesc.BlendState.RenderTarget[i];
        BB_ASSERT(!(blendDesc.BlendEnable && blendDesc.LogicOpEnable),
                  "Render target %u cannot enable blending and a logic operation simultaneously", i);
    }

    if (!m_bMeshShader)
    {
        auto d3d12VS = StaticCast<Dx12Shader>(m_pVS);
        auto d3d12PS = StaticCast<Dx12Shader>(m_pPS);
        assert(d3d12VS && d3d12PS);

        // InputLayout stays empty: vertex shaders fetch their data from the heap by SV_VertexID
        m_PipelineDesc.VS = CD3DX12_SHADER_BYTECODE(d3d12VS->GetShaderBufferPointer(), d3d12VS->GetShaderBufferSize());
        m_PipelineDesc.PS = CD3DX12_SHADER_BYTECODE(d3d12PS->GetShaderBufferPointer(), d3d12PS->GetShaderBufferSize());

        m_PipelineDesc.pRootSignature = m_pRootSignature->GetD3D12RootSignature();

        auto d3d12Device = m_RenderDevice.GetD3D12Device();
        ID3D12PipelineState* d3d12PipelineState = nullptr;
        if (!IsSucceeded(d3d12Device->CreateGraphicsPipelineState(&m_PipelineDesc, IID_PPV_ARGS(&d3d12PipelineState)), m_d3d12PipelineState))
            return false;

        m_ResourceBindingMap.clear();
        ParseRootParameters(*d3d12VS->GetCreationInfo().bindings);
        ParseRootParameters(*d3d12PS->GetCreationInfo().bindings);

        m_RenderDevice.DeferRelease(m_d3d12PipelineState);
        m_d3d12PipelineState = d3d12PipelineState;

        ++m_BuildCount;
        return true;
    }

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

    Stream.pRootSignature = m_pRootSignature->GetD3D12RootSignature();

    D3D12_PIPELINE_STATE_STREAM_DESC streamDesc = {};
    streamDesc.SizeInBytes                   = sizeof(MeshPipelineStream);
    streamDesc.pPipelineStateSubobjectStream = &Stream;

    auto d3d12Device = m_RenderDevice.GetD3D12Device();
    ID3D12PipelineState* d3d12PipelineState = nullptr;
    if (!IsSucceeded(d3d12Device->CreatePipelineState(&streamDesc, IID_PPV_ARGS(&d3d12PipelineState)), m_d3d12PipelineState))
        return false;

    m_ResourceBindingMap.clear();
    ParseRootParameters(*d3d12MS->GetCreationInfo().bindings);
    ParseRootParameters(*d3d12PS->GetCreationInfo().bindings);
    if (d3d12AS) ParseRootParameters(*d3d12AS->GetCreationInfo().bindings);

    m_RenderDevice.DeferRelease(m_d3d12PipelineState);
    m_d3d12PipelineState = d3d12PipelineState;

    ++m_BuildCount;
    return true;
}

void Dx12GraphicsPipeline::ParseRootParameters(const render::ShaderBindingLayout& bindings)
{
    BB_ASSERT(bindings.pushBlockSize <= render::kSlangPushBlockBytes, "PushBlockSize is overflowed!");

    for (const auto& [name, offset] : bindings.pushOffsets)
    {
        u32 rootIndex =
            m_pRootSignature->GetRootIndex(D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS, render::kSlangPushSpace, render::kSlangPushRegister);
        m_ResourceBindingMap.emplace(name, (u64(offset) << 32llu) | rootIndex);
    }
}


//-------------------------------------------------------------------------
// Compute pipeline
//-------------------------------------------------------------------------
Dx12ComputePipeline::Dx12ComputePipeline(Dx12RenderDevice& rd, const char* name)
	: render::ComputePipeline(name)
    , m_RenderDevice(rd)
{
    m_pRootSignature = m_RenderDevice.GetGlobalRootSignature();
}

Dx12ComputePipeline::~Dx12ComputePipeline()
{
    m_RenderDevice.DeferRelease(m_d3d12PipelineState);
}

bool Dx12ComputePipeline::Build()
{
    auto d3d12CS = StaticCast<Dx12Shader>(m_pCS);
    assert(d3d12CS);

    const auto& bindings = d3d12CS->GetCreationInfo().bindings;
    BB_ASSERT(bindings, "Compute pipeline '%s': the shader has no Slang bindings. Create it through the PipelineFactory.", m_Name.c_str());

    m_PipelineDesc.CS = CD3DX12_SHADER_BYTECODE(d3d12CS->GetShaderBufferPointer(), d3d12CS->GetShaderBufferSize());

    m_PipelineDesc.pRootSignature = m_pRootSignature->GetD3D12RootSignature();

    auto d3d12Device = m_RenderDevice.GetD3D12Device();
    ID3D12PipelineState* d3d12PipelineState = nullptr;
    if (!IsSucceeded(d3d12Device->CreateComputePipelineState(&m_PipelineDesc, IID_PPV_ARGS(&d3d12PipelineState)), m_d3d12PipelineState))
        return false;

    ParseRootParameters(*bindings);

    m_RenderDevice.DeferRelease(m_d3d12PipelineState);
    m_d3d12PipelineState = d3d12PipelineState;

    ++m_BuildCount;
    return true;
}

void Dx12ComputePipeline::ParseRootParameters(const render::ShaderBindingLayout& bindings)
{
    BB_ASSERT(bindings.pushBlockSize <= render::kSlangPushBlockBytes, "PushBlockSize is overflowed!");

    m_ResourceBindingMap.clear();

    for (const auto& [name, offset] : bindings.pushOffsets)
    {
        u32 rootIndex = 
            m_pRootSignature->GetRootIndex(D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS, render::kSlangPushSpace, render::kSlangPushRegister);
        m_ResourceBindingMap.emplace(name, (u64(offset) << 32llu) | rootIndex);
    }
}


//-------------------------------------------------------------------------
// DXR Pipeline
//-------------------------------------------------------------------------
Dx12RaytracingPipeline::Dx12RaytracingPipeline(Dx12RenderDevice& rd, const char* name)
    : render::RaytracingPipeline(name)
    , m_RenderDevice(rd)
{
    m_pGlobalRootSignature = m_RenderDevice.GetGlobalRootSignature();
}

Dx12RaytracingPipeline::~Dx12RaytracingPipeline()
{
    m_RenderDevice.DeferRelease(m_d3d12StateObjectProperties);
    m_RenderDevice.DeferRelease(m_d3d12StateObject);
}

bool Dx12RaytracingPipeline::Build()
{
    auto pLib = StaticCast< Dx12Shader >(m_pShaderLibrary);
    assert(pLib && "Shader library must be set before Build()!");

    const auto& bindings = pLib->GetCreationInfo().bindings;
    BB_ASSERT(bindings, "Raytracing pipeline '%s': the library has no Slang bindings. Create it through the PipelineFactory.", m_Name.c_str());

    if (!BuildStateObject())
        return false;

    ParseRootParameters(*bindings);

    ++m_BuildCount;
    return true;
}

const void* Dx12RaytracingPipeline::GetShaderIdentifier(const std::string& exportName) const
{
    auto wName = ConvertToWString(exportName);
    return m_d3d12StateObjectProperties->GetShaderIdentifier(wName.c_str());
}

bool Dx12RaytracingPipeline::BuildStateObject()
{
    auto d3d12Lib = StaticCast< Dx12Shader >(m_pShaderLibrary);

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

    auto d3d12Device = m_RenderDevice.GetD3D12Device();
    ID3D12StateObject* d3d12StateObject = nullptr;
    if (!IsSucceeded(d3d12Device->CreateStateObject(stateObjectDesc, IID_PPV_ARGS(&d3d12StateObject)), m_d3d12StateObject))
        return false;

    ID3D12StateObjectProperties* d3d12StateObjectProperties = nullptr;
    ThrowIfFailed(
        d3d12StateObject->QueryInterface(IID_PPV_ARGS(&d3d12StateObjectProperties))
    );

    m_RenderDevice.DeferRelease(m_d3d12StateObjectProperties);
    m_RenderDevice.DeferRelease(m_d3d12StateObject);
    m_d3d12StateObject           = d3d12StateObject;
    m_d3d12StateObjectProperties = d3d12StateObjectProperties;
    return true;
}

void Dx12RaytracingPipeline::ParseRootParameters(const render::ShaderBindingLayout& bindings)
{
    BB_ASSERT(bindings.pushBlockSize <= render::kSlangPushBlockBytes, "PushBlockSize is overflowed!");

    m_ResourceBindingMap.clear();

    for (const auto& [name, offset] : bindings.pushOffsets)
    {
        u32 rootIndex =
            m_pGlobalRootSignature->GetRootIndex(D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS, render::kSlangPushSpace, render::kSlangPushRegister);
        m_ResourceBindingMap.emplace(name, (u64(offset) << 32llu) | rootIndex);
    }
}

}