#include "RendererPch.h"
#include "Dx12LightingModule.h"
#include "RenderDevice/Dx12CommandContext.h"
#include "RenderDevice/Dx12RootSignature.h"
#include "RenderDevice/Dx12RenderPipeline.h"
#include "RenderResource/Dx12Shader.h"
#include "RenderResource/Dx12Buffer.h"
#include "RenderResource/Dx12Texture.h"
#include "RenderResource/Dx12SceneResource.h"

namespace dx12
{

LightingModule::LightingModule(RenderDevice& device)
	: Super(device)
{
	CD3DX12_RESOURCE_DESC texDescColor =
		CD3DX12_RESOURCE_DESC::Tex2D(
			DXGI_FORMAT_R8G8B8A8_UNORM,
			m_RenderDevice.WindowWidth(), m_RenderDevice.WindowHeight(), 1, 1, 1, 0,
			D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS | D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET
		);

	m_pOutTexture =
		Texture::Create(
			m_RenderDevice,
			L"LightingPass::Out",
			{
				.desc         = texDescColor,
				.initialState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS
			});

	m_pLightingRS = new RootSignature(m_RenderDevice, L"PBRLightingRS");
	m_Indices.camera   = m_pLightingRS->AddCBV(0, 0); // g_Camera
	m_Indices.light    = m_pLightingRS->AddCBV(1, 0); // g_Lights
	m_Indices.textures = m_pLightingRS->AddDescriptorTable(
		DescriptorTable()
			.AddSRVRange(0, 0, 5) // g_GBuffer#, g_DepthBuffer
			.AddUAVRange(0, 0, 1) // g_OutputTexture
	);
	m_Indices.material = m_pLightingRS->AddSRV(5, 0); // g_Materials
	
	m_pLightingRS->AddSampler(0, 0, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, 1);
	m_pLightingRS->Build();

	auto pCS = 
		Shader::Create(m_RenderDevice, L"DeferredPBRLightingCS", { .filepath = CSO_PATH.string() + "PBRLightingCS.cso" });
	m_pLightingPSO = new ComputePipeline(m_RenderDevice, L"LightingPSO");
	m_pLightingPSO->SetShaderModules(pCS).SetRootSignature(m_pLightingRS).Build();
}

LightingModule::~LightingModule()
{
	RELEASE(m_pLightingPSO);
	RELEASE(m_pLightingRS);
}

void LightingModule::Apply(CommandContext& context)
{
	context.TransitionBarrier(g_FrameData.pGBuffer0.lock(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, false);
	context.TransitionBarrier(g_FrameData.pGBuffer1.lock(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, false);
	context.TransitionBarrier(g_FrameData.pGBuffer2.lock(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, false);
	context.TransitionBarrier(g_FrameData.pGBuffer3.lock(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, false);
	context.TransitionBarrier(g_FrameData.pDepth.lock(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, false);

	context.SetRenderPipeline(m_pLightingPSO);
	context.SetComputeRootSignature(m_pLightingRS);

	auto pLight    = g_FrameData.pSceneResource->GetLightBuffer();
	auto pMaterial = g_FrameData.pSceneResource->GetMaterialBuffer();
	assert(pLight && pMaterial);

	context.SetComputeDynamicConstantBuffer(m_Indices.camera, g_FrameData.camera);
	context.SetComputeConstantBufferView(m_Indices.light, pLight->GpuAddress());
	context.StageDescriptors(m_Indices.textures, 0,
		{
			g_FrameData.pGBuffer0.lock()->GetShaderResourceView(),
			g_FrameData.pGBuffer1.lock()->GetShaderResourceView(),
			g_FrameData.pGBuffer2.lock()->GetShaderResourceView(),
			g_FrameData.pGBuffer3.lock()->GetShaderResourceView(),
			g_FrameData.pDepth.lock()->GetShaderResourceView(),
			m_pOutTexture->GetUnorderedAccessView(0),
		});
	context.SetComputeShaderResourceView(m_Indices.material, pMaterial->GpuAddress());

	context.Dispatch2D< 16, 16 >((u32)m_pOutTexture->Desc().Width, (u32)m_pOutTexture->Desc().Height);

	g_FrameData.pColor = m_pOutTexture;
}

} // namespace dx12