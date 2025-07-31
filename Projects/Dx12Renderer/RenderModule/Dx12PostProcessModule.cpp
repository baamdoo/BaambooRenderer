#include "RendererPch.h"
#include "Dx12PostProcessModule.h"
#include "RenderDevice/Dx12CommandContext.h"
#include "RenderDevice/Dx12RootSignature.h"
#include "RenderDevice/Dx12RenderPipeline.h"
#include "RenderDevice/Dx12ResourceManager.h"
#include "RenderResource/Dx12Shader.h"
#include "RenderResource/Dx12Texture.h"
#include "RenderResource/Dx12SceneResource.h"
#include "ComponentTypes.h"
#include "Utils/Math.hpp"

namespace dx12
{

PostProcessModule::PostProcessModule(RenderDevice& device)
	: Super(device)
{
	{
		m_TAA.applyCounter = 0;

		m_TAA.pHistoryTexture =
			Texture::Create(
				m_RenderDevice,
				L"PostProcessPass::TemporalAntiAliasingHistory",
				{
					.desc         = CD3DX12_RESOURCE_DESC::Tex2D(
										DXGI_FORMAT_R16G16B16A16_FLOAT,
										m_RenderDevice.WindowWidth(), m_RenderDevice.WindowHeight(), 1, 1, 1, 0,
										D3D12_RESOURCE_FLAG_NONE
									),
					.initialState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
				});
		m_TAA.pAntiAliasedTexture =
			Texture::Create(
				m_RenderDevice,
				L"PostProcessPass::AntiAliasing",
				{
					.desc         = CD3DX12_RESOURCE_DESC::Tex2D(
										DXGI_FORMAT_R16G16B16A16_FLOAT,
										m_RenderDevice.WindowWidth(), m_RenderDevice.WindowHeight(), 1, 1, 1, 0,
										D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
									),
					.initialState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS
				});

		m_TAA.pTemporalAntiAliasingRS = MakeBox< RootSignature >(m_RenderDevice, L"TemporalAntiAliasingRS");
		m_TAA.pTemporalAntiAliasingRS->AddConstants(0, ROOT_CONSTANT_SPACE, 2);
		m_TAA.pTemporalAntiAliasingRS->AddDescriptorTable(
			DescriptorTable()
				.AddSRVRange(0, 0, 1) // g_SceneTexture
				.AddSRVRange(1, 0, 1) // g_VelocityTexture
				.AddSRVRange(2, 0, 1) // g_HistoryTexture
				.AddUAVRange(0, 0, 1) // g_OutputTexture
		);
		m_TAA.pTemporalAntiAliasingRS->AddSampler(0, 0, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, 1); // g_LinearClampSampler
		m_TAA.pTemporalAntiAliasingRS->Build();

		auto pCS = Shader::Create(
				m_RenderDevice, 
				L"TemporalAntiAliasingCS", 
				{ .filepath = CSO_PATH.string() + "TemporalAntiAliasingCS.cso" }
			);
		m_TAA.pTemporalAntiAliasingPSO = MakeBox< ComputePipeline >(m_RenderDevice, L"TemporalAntiAliasingPSO");
		m_TAA.pTemporalAntiAliasingPSO->SetShaderModules(pCS).SetRootSignature(m_TAA.pTemporalAntiAliasingRS.get()).Build();


		m_TAA.pSharpenRS = MakeBox< RootSignature >(m_RenderDevice, L"SharpenRS");
		m_TAA.pSharpenRS->AddConstants(0, ROOT_CONSTANT_SPACE, 1);
		m_TAA.pSharpenRS->AddDescriptorTable(
			DescriptorTable()
				.AddSRVRange(0, 0, 1) // g_AntiAliasedTexture
				.AddUAVRange(0, 0, 1) // g_OutputTexture
		);
		m_TAA.pSharpenRS->AddSampler(0, 0, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, 1); // g_LinearClampSampler
		m_TAA.pSharpenRS->Build();

		auto pSharpenCS = Shader::Create(
			m_RenderDevice,
			L"SharpenCS",
			{ .filepath = CSO_PATH.string() + "SharpenCS.cso" }
		);
		m_TAA.pSharpenPSO = MakeBox< ComputePipeline >(m_RenderDevice, L"SharpenPSO");
		m_TAA.pSharpenPSO->SetShaderModules(pSharpenCS).SetRootSignature(m_TAA.pSharpenRS.get()).Build();
	}
	{
		m_ToneMapping.pResolvedTexture =
			Texture::Create(
				m_RenderDevice,
				L"PostProcessPass::ToneMapping",
				{
					.desc = CD3DX12_RESOURCE_DESC::Tex2D(
										DXGI_FORMAT_R8G8B8A8_UNORM,
										m_RenderDevice.WindowWidth(), m_RenderDevice.WindowHeight(), 1, 1, 1, 0,
										D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS | D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET
									),
					.initialState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS
				});

		m_ToneMapping.pToneMappingRS = MakeBox< RootSignature >(m_RenderDevice, L"ToneMappingRS");
		m_ToneMapping.pToneMappingRS->AddConstants(0, ROOT_CONSTANT_SPACE, 2);
		m_ToneMapping.pToneMappingRS->AddDescriptorTable(
			DescriptorTable()
				.AddSRVRange(0, 0, 1) // g_SceneTexture
				.AddUAVRange(0, 0, 1) // g_OutputTexture
		);
		m_ToneMapping.pToneMappingRS->AddSampler(0, 0, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, 1); // g_LinearClampSampler
		m_ToneMapping.pToneMappingRS->Build();

		auto pCS = Shader::Create(
			m_RenderDevice,
			L"ToneMappingCS",
			{ .filepath = CSO_PATH.string() + "ToneMappingCS.cso" }
		);
		m_ToneMapping.pToneMappingPSO = MakeBox< ComputePipeline >(m_RenderDevice, L"ToneMappingPSO");
		m_ToneMapping.pToneMappingPSO->SetShaderModules(pCS).SetRootSignature(m_ToneMapping.pToneMappingRS.get()).Build();
	}
}

PostProcessModule::~PostProcessModule()
{
}

void PostProcessModule::Apply(CommandContext& context, const SceneRenderView& renderView)
{
	if (renderView.postProcess.effectBits & (1 << ePostProcess::HeightFog))
		ApplyHeightFog(context, renderView);
	if (renderView.postProcess.effectBits & (1 << ePostProcess::Bloom))
		ApplyBloom(context, renderView);
	if (renderView.postProcess.effectBits & (1 << ePostProcess::AntiAliasing))
		ApplyAntiAliasing(context, renderView);

	// always apply tone-mapping
	ApplyToneMapping(context, renderView);
}

void PostProcessModule::Resize(u32 width, u32 height, u32 depth)
{
	RenderModule::Resize(width, height, depth);
}

void PostProcessModule::ApplyHeightFog(CommandContext& context, const SceneRenderView& renderView)
{
	// TODO
	UNUSED(context);
	UNUSED(renderView);
}

void PostProcessModule::ApplyBloom(CommandContext& context, const SceneRenderView& renderView)
{
	// TODO
	UNUSED(context);
	UNUSED(renderView);
}

void PostProcessModule::ApplyAntiAliasing(CommandContext& context, const SceneRenderView& renderView)
{
	{
		auto& rm = m_RenderDevice.GetResourceManager();
		const bool bFirstApply = m_TAA.applyCounter == 0;

		context.SetRenderPipeline(m_TAA.pTemporalAntiAliasingPSO.get());
		context.SetComputeRootSignature(m_TAA.pTemporalAntiAliasingRS.get());

		context.TransitionBarrier(g_FrameData.pColor.lock(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, false);
		context.TransitionBarrier(g_FrameData.pGBuffer3.lock(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, false);
		if (!bFirstApply)
			context.TransitionBarrier(m_TAA.pHistoryTexture, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, false);
		context.TransitionBarrier(m_TAA.pAntiAliasedTexture, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, false);

		struct
		{
			float  blendFactor;
			u32    isFirstFrame;
		} constant = { bFirstApply ? 1.0f : renderView.postProcess.aa.blendFactor, bFirstApply };
		context.SetComputeRootConstants(0, sizeof(constant), &constant);
		context.StageDescriptors(
			1, 0,
			{
				g_FrameData.pColor.lock()->GetShaderResourceView(),
				g_FrameData.pGBuffer3.lock()->GetShaderResourceView(),
				bFirstApply ? rm.GetFlatBlackTexture()->GetShaderResourceView() : m_TAA.pHistoryTexture->GetShaderResourceView(),
				m_TAA.pAntiAliasedTexture->GetUnorderedAccessView(0)
			});
		context.Dispatch2D< 16, 16 >(m_TAA.pAntiAliasedTexture->GetWidth(), m_TAA.pAntiAliasedTexture->GetHeight());

		context.CopyTexture(m_TAA.pHistoryTexture, m_TAA.pAntiAliasedTexture);
	}
	{
		context.SetRenderPipeline(m_TAA.pSharpenPSO.get());
		context.SetComputeRootSignature(m_TAA.pSharpenRS.get());

		context.TransitionBarrier(m_TAA.pAntiAliasedTexture, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, false);
		context.TransitionBarrier(g_FrameData.pColor.lock(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, false);

		struct
		{
			float sharpness;
		} constant = { renderView.postProcess.aa.sharpness };
		context.SetComputeRootConstants(0, sizeof(constant), &constant);
		context.StageDescriptors(
			1, 0,
			{
				m_TAA.pAntiAliasedTexture->GetShaderResourceView(),
				g_FrameData.pColor.lock()->GetUnorderedAccessView(0)
			});
		context.Dispatch2D< 16, 16 >(g_FrameData.pColor->GetWidth(), g_FrameData.pColor->GetHeight());
	}

	m_TAA.applyCounter++;
}

void PostProcessModule::ApplyToneMapping(CommandContext& context, const SceneRenderView& renderView)
{
	context.SetRenderPipeline(m_ToneMapping.pToneMappingPSO.get());
	context.SetComputeRootSignature(m_ToneMapping.pToneMappingRS.get());

	context.TransitionBarrier(g_FrameData.pColor.lock(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, false);
	context.TransitionBarrier(m_ToneMapping.pResolvedTexture, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, false);

	// Tone mapping push constants
	struct
	{
		u32   tonemapOperator; // 0: Reinhard, 1: ACES, 2: Uncharted2
		float gamma;
	} constant = { (u32)renderView.postProcess.tonemap.op, renderView.postProcess.tonemap.gamma };
	context.SetComputeRootConstants(0, sizeof(constant), &constant);
	context.StageDescriptors(
		1, 0,
		{
			g_FrameData.pColor.lock()->GetShaderResourceView(),
			m_ToneMapping.pResolvedTexture->GetUnorderedAccessView(0)
		});
	context.Dispatch2D< 16, 16 >(g_FrameData.pColor->GetWidth(), g_FrameData.pColor->GetHeight());

	g_FrameData.pColor = m_ToneMapping.pResolvedTexture;
}

} // namespace dx12