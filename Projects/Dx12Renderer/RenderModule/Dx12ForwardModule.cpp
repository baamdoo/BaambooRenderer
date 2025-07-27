#include "RendererPch.h"
#include "Dx12ForwardModule.h"
#include "RenderDevice/Dx12CommandContext.h"
#include "RenderDevice/Dx12RootSignature.h"
#include "RenderDevice/Dx12RenderPipeline.h"
#include "RenderResource/Dx12Shader.h"
#include "RenderResource/Dx12Buffer.h"
#include "RenderResource/Dx12Texture.h"
#include "RenderResource/Dx12RenderTarget.h"
#include "RenderResource/Dx12SceneResource.h"

namespace dx12
{

ForwardModule::ForwardModule(RenderDevice& device)
	: Super(device)
{
    auto pVS = Shader::Create(m_RenderDevice, L"ForwardTransparentVS",
        { .filepath = CSO_PATH.string() + "ForwardTransparentVS.cso" });
    auto pPS = Shader::Create(m_RenderDevice, L"ForwardTransparentPS",
        { .filepath = CSO_PATH.string() + "ForwardTransparentPS.cso" });

    m_pForwardRS = new RootSignature(m_RenderDevice, L"ForwardTransparentRS");
    m_pForwardRS->CopySignatureParams(*g_FrameData.pSceneResource->GetSceneRootSignature());
    m_pForwardRS->AddCBV(1, 0, D3D12_ROOT_DESCRIPTOR_FLAG_NONE, D3D12_SHADER_VISIBILITY_PIXEL);
    m_pForwardRS->Build();

    m_pForwardPSO = new GraphicsPipeline(m_RenderDevice, L"ForwardTransparentPSO");
    m_pForwardPSO->SetShaderModules(pVS, pPS).SetRootSignature(m_pForwardRS);
    m_pForwardPSO->SetBlendEnable(0, true)
                  .SetBlendState(
                      0,
                      D3D12_BLEND_SRC_ALPHA,
                      D3D12_BLEND_INV_SRC_ALPHA,
                      D3D12_BLEND_ONE,
                      D3D12_BLEND_INV_SRC_ALPHA
                  )
                  .SetCullMode(D3D12_CULL_MODE_NONE);
    m_pForwardPSO->Build();
}

ForwardModule::~ForwardModule()
{
    RELEASE(m_pForwardPSO);
    RELEASE(m_pForwardRS);
}

void ForwardModule::Apply(CommandContext& context)
{
    if (!g_FrameData.pColor.valid() || !g_FrameData.pDepth.valid())
        return;

}

} // namespace dx12