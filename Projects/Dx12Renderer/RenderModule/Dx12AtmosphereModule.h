#pragma once
#include "Dx12RenderModule.h"

namespace dx12
{

class AtmosphereModule : public RenderModule
{
using Super = RenderModule;
public:
	AtmosphereModule(RenderDevice& device);
	virtual ~AtmosphereModule();

	virtual void Apply(CommandContext& context) override;

private:
	Arc< Texture > m_pTransmittanceLUT;
	Arc< Texture > m_pMultiScatteringLUT;
	Arc< Texture > m_pSkyViewLUT;
	Arc< Texture > m_pAerialPerspectiveLUT;

	Box< RootSignature >   m_pTransmittanceRS;
	Box< RootSignature >   m_pMultiScatteringRS;
	Box< RootSignature >   m_pSkyViewRS;
	Box< RootSignature >   m_pAerialPerspectiveRS;

	Box< ComputePipeline > m_pTransmittancePSO;
	Box< ComputePipeline > m_pMultiScatteringPSO;
	Box< ComputePipeline > m_pSkyViewPSO;
	Box< ComputePipeline > m_pAerialPerspectivePSO;
};

} // namespace dx12