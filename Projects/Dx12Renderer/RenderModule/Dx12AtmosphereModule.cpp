#include "RendererPch.h"
#include "Dx12AtmosphereModule.h"
#include "RenderDevice/Dx12CommandContext.h"
#include "RenderDevice/Dx12RootSignature.h"
#include "RenderDevice/Dx12RenderPipeline.h"
#include "RenderResource/Dx12Shader.h"
#include "RenderResource/Dx12Buffer.h"
#include "RenderResource/Dx12Texture.h"
#include "RenderResource/Dx12SceneResource.h"

static constexpr uint3 TRANSMITTANCE_LUT_RESOLUTION     = { 256, 64, 1 };
static constexpr uint3 MULTISCATTERING_LUT_RESOLUTION   = { 32, 32, 1 };
static constexpr uint3 SKYVIEW_LUT_RESOLUTION           = { 192, 104 , 1 };
static constexpr uint3 AERIALPERSPECTIVE_LUT_RESOLUTION = { 32, 32 , 32 };

namespace dx12
{

AtmosphereModule::AtmosphereModule(RenderDevice& device)
	: Super(device)
{
	// texture
	{
		m_pTransmittanceLUT =
			Texture::Create(
				m_RenderDevice,
				L"AtmospherePass::TransmittanceLUT",
				{
					.desc         = CD3DX12_RESOURCE_DESC::Tex2D(
										DXGI_FORMAT_R11G11B10_FLOAT,
									TRANSMITTANCE_LUT_RESOLUTION.x, TRANSMITTANCE_LUT_RESOLUTION.y, 1, 1, 1, 0,
									D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
									),
					.initialState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS
				});
		m_pMultiScatteringLUT =
			Texture::Create(
				m_RenderDevice,
				L"AtmospherePass::MultiScatteringLUT",
				{
					.desc         = CD3DX12_RESOURCE_DESC::Tex2D(
										DXGI_FORMAT_R11G11B10_FLOAT,
									MULTISCATTERING_LUT_RESOLUTION.x, MULTISCATTERING_LUT_RESOLUTION.y, 1, 1, 1, 0,
									D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
									),
					.initialState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS
				});
		m_pSkyViewLUT =
			Texture::Create(
				m_RenderDevice,
				L"AtmospherePass::SkyViewLUT",
				{
					.desc         = CD3DX12_RESOURCE_DESC::Tex2D(
										DXGI_FORMAT_R16G16B16A16_FLOAT,
									SKYVIEW_LUT_RESOLUTION.x, SKYVIEW_LUT_RESOLUTION.y, 1, 1, 1, 0,
									D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
									),
					.initialState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS
				});
		m_pAerialPerspectiveLUT =
			Texture::Create(
				m_RenderDevice,
				L"AtmospherePass::AerialPerspectiveLUT",
				{
					.desc         = CD3DX12_RESOURCE_DESC::Tex3D(
										DXGI_FORMAT_R16G16B16A16_FLOAT,
									UINT16(AERIALPERSPECTIVE_LUT_RESOLUTION.x), UINT16(AERIALPERSPECTIVE_LUT_RESOLUTION.y), UINT16(AERIALPERSPECTIVE_LUT_RESOLUTION.z), 1,
									D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
									),
					.initialState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS
				});
	}

	// root signature
	{
		m_pTransmittanceRS = MakeBox< RootSignature >(m_RenderDevice, L"TransmittanceRS");
		m_pTransmittanceRS->AddCBV(1, 0); // g_Atmosphere
		m_pTransmittanceRS->AddDescriptorTable(
			DescriptorTable()
			.AddUAVRange(0, 0, 1) // g_TransmittanceLUT
		);
		m_pTransmittanceRS->AddSampler(0, 0, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, 1);
		m_pTransmittanceRS->Build();

		m_pMultiScatteringRS = MakeBox< RootSignature >(m_RenderDevice, L"MultiScatteringRS");
		m_pMultiScatteringRS->AddConstants(0, ROOT_CONSTANT_SPACE, 2); // g_Push
		m_pMultiScatteringRS->AddCBV(1, 0); // g_Atmosphere
		m_pMultiScatteringRS->AddDescriptorTable(
			DescriptorTable()
			.AddSRVRange(0, 0, 1) // g_TransmittanceLUT
			.AddUAVRange(0, 0, 1) // g_MultiScatteringLUT
		);
		m_pMultiScatteringRS->AddSampler(0, 0, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, 1);
		m_pMultiScatteringRS->Build();

		m_pSkyViewRS = MakeBox< RootSignature >(m_RenderDevice, L"SkyViewRS");
		m_pSkyViewRS->AddConstants(0, ROOT_CONSTANT_SPACE, 2); // g_Push
		m_pSkyViewRS->AddCBV(0, 0); // g_Camera
		m_pSkyViewRS->AddCBV(1, 0); // g_Atmosphere
		m_pSkyViewRS->AddDescriptorTable(
			DescriptorTable()
			.AddSRVRange(0, 0, 1) // g_TransmittanceLUT
			.AddSRVRange(1, 0, 1) // g_MultiScatteringLUT
			.AddUAVRange(0, 0, 1) // g_SkyViewLUT
		);
		m_pSkyViewRS->AddSampler(0, 0, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, 1);
		m_pSkyViewRS->Build();

		m_pAerialPerspectiveRS = MakeBox< RootSignature >(m_RenderDevice, L"AerialPerspectiveRS");
		m_pAerialPerspectiveRS->AddCBV(0, 0); // g_Camera
		m_pAerialPerspectiveRS->AddCBV(1, 0); // g_Atmosphere
		m_pAerialPerspectiveRS->AddDescriptorTable(
			DescriptorTable()
			.AddSRVRange(0, 0, 1) // g_TransmittanceLUT
			.AddSRVRange(1, 0, 1) // g_MultiScatteringLUT
			.AddUAVRange(0, 0, 1) // g_AerialPerspectiveLUT
		);
		m_pAerialPerspectiveRS->AddSampler(0, 0, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, 1);
		m_pAerialPerspectiveRS->Build();
	}

	// compute pso
	{
		auto pTransmittanceCS =
			Shader::Create(m_RenderDevice, L"TransmittanceCS", { .filepath = CSO_PATH.string() + "TransmittanceCS.cso" });
		m_pTransmittancePSO = MakeBox< ComputePipeline >(m_RenderDevice, L"TransmittancePSO");
		m_pTransmittancePSO->SetShaderModules(pTransmittanceCS).SetRootSignature(m_pTransmittanceRS.get()).Build();

		auto pMultiScatteringCS =
			Shader::Create(m_RenderDevice, L"MultiScatteringCS", { .filepath = CSO_PATH.string() + "MultiScatteringCS.cso" });
		m_pMultiScatteringPSO = MakeBox< ComputePipeline >(m_RenderDevice, L"MultiScatteringPSO");
		m_pMultiScatteringPSO->SetShaderModules(pMultiScatteringCS).SetRootSignature(m_pMultiScatteringRS.get()).Build();

		auto pSkyViewCS =
			Shader::Create(m_RenderDevice, L"SkyViewCS", { .filepath = CSO_PATH.string() + "SkyViewCS.cso" });
		m_pSkyViewPSO = MakeBox< ComputePipeline >(m_RenderDevice, L"SkyViewPSO");
		m_pSkyViewPSO->SetShaderModules(pSkyViewCS).SetRootSignature(m_pSkyViewRS.get()).Build();

		auto pAerialPerspectiveCS =
			Shader::Create(m_RenderDevice, L"AerialPerspectiveCS", { .filepath = CSO_PATH.string() + "AerialPerspectiveCS.cso" });
		m_pAerialPerspectivePSO = MakeBox< ComputePipeline >(m_RenderDevice, L"AerialPerspectivePSO");
		m_pAerialPerspectivePSO->SetShaderModules(pAerialPerspectiveCS).SetRootSignature(m_pAerialPerspectiveRS.get()).Build();
	}
}

AtmosphereModule::~AtmosphereModule()
{
}

void AtmosphereModule::Apply(CommandContext& context)
{
	if (g_FrameData.atmosphere.bMark)
	{
		context.SetRenderPipeline(m_pTransmittancePSO.get());
		context.SetComputeRootSignature(m_pTransmittanceRS.get());
		context.TransitionBarrier(m_pTransmittanceLUT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, false);
		context.SetComputeDynamicConstantBuffer(0, g_FrameData.atmosphere.data);
		context.StageDescriptors(1, 0, 
			{
				m_pTransmittanceLUT->GetUnorderedAccessView(0)
			});
		context.Dispatch2D< 8, 8 >(TRANSMITTANCE_LUT_RESOLUTION.x, TRANSMITTANCE_LUT_RESOLUTION.y);

		context.SetRenderPipeline(m_pMultiScatteringPSO.get());
		context.SetComputeRootSignature(m_pMultiScatteringRS.get());
		context.TransitionBarrier(m_pTransmittanceLUT, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, false);
		context.TransitionBarrier(m_pMultiScatteringLUT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, false);
		context.SetComputeRootConstant(0, g_FrameData.atmosphere.msIsoSampleCount, 0);
		context.SetComputeRootConstant(0, g_FrameData.atmosphere.msNumRaySteps, 1);
		context.SetComputeDynamicConstantBuffer(1, g_FrameData.atmosphere.data);
		context.StageDescriptors(2, 0,
			{
				m_pTransmittanceLUT->GetShaderResourceView(),
				m_pMultiScatteringLUT->GetUnorderedAccessView(0)
			});
		context.Dispatch2D< 8, 8 >(MULTISCATTERING_LUT_RESOLUTION.x, MULTISCATTERING_LUT_RESOLUTION.y);
	}
	context.TransitionBarrier(m_pTransmittanceLUT, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, false);
	context.TransitionBarrier(m_pMultiScatteringLUT, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, false);

	context.SetRenderPipeline(m_pSkyViewPSO.get());
	context.SetComputeRootSignature(m_pSkyViewRS.get());
	context.TransitionBarrier(m_pSkyViewLUT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, false);
	context.SetComputeRootConstant(0, g_FrameData.atmosphere.svMinRaySteps, 0);
	context.SetComputeRootConstant(0, g_FrameData.atmosphere.svMaxRaySteps, 1);
	context.SetComputeDynamicConstantBuffer(1, g_FrameData.camera);
	context.SetComputeDynamicConstantBuffer(2, g_FrameData.atmosphere.data);
	context.StageDescriptors(3, 0,
		{
			m_pTransmittanceLUT->GetShaderResourceView(),
			m_pMultiScatteringLUT->GetShaderResourceView(),
			m_pSkyViewLUT->GetUnorderedAccessView(0)
		});
	context.Dispatch2D< 8, 8 >(SKYVIEW_LUT_RESOLUTION.x, SKYVIEW_LUT_RESOLUTION.y);

	context.SetRenderPipeline(m_pAerialPerspectivePSO.get());
	context.SetComputeRootSignature(m_pAerialPerspectiveRS.get());
	context.TransitionBarrier(m_pAerialPerspectiveLUT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, false);
	context.SetComputeDynamicConstantBuffer(0, g_FrameData.camera);
	context.SetComputeDynamicConstantBuffer(1, g_FrameData.atmosphere.data);
	context.StageDescriptors(2, 0,
		{
			m_pTransmittanceLUT->GetShaderResourceView(),
			m_pMultiScatteringLUT->GetShaderResourceView(),
			m_pAerialPerspectiveLUT->GetUnorderedAccessView(0)
		});
	context.Dispatch3D< 4, 4, 4 >(AERIALPERSPECTIVE_LUT_RESOLUTION.x, AERIALPERSPECTIVE_LUT_RESOLUTION.y, AERIALPERSPECTIVE_LUT_RESOLUTION.z);

	g_FrameData.pSkyViewLUT           = m_pSkyViewLUT;
	g_FrameData.pAerialPerspectiveLUT = m_pAerialPerspectiveLUT;
}

} // namespace dx12

