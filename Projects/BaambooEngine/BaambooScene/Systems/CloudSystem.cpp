#include "BaambooPch.h"
#include "CloudSystem.h"
#include "AtmosphereSystem.h"
#include "BaambooScene/Camera.h"
#include "Utils/Math.hpp"

namespace baamboo
{

CloudSystem::CloudSystem(entt::registry& registry, AtmosphereSystem* pAtmosphereSystem)
    : Super(registry)
	, m_pAtmosphereSystem(pAtmosphereSystem)
{
	assert(m_pAtmosphereSystem);

	DependsOn< AtmosphereComponent >();
}

void CloudSystem::OnComponentConstructed(entt::registry& registry, entt::entity entity)
{
    auto& cloud = registry.get< CloudComponent >(entity);

    cloud.weatherMap   = TEXTURE_PATH.string() + "cloud_weather.png";
    cloud.curlNoiseTex = TEXTURE_PATH.string() + "cloud_curl_noise.png";
    cloud.blueNoiseTex = TEXTURE_PATH.string() + "blue_noise.png";

    Super::OnComponentConstructed(registry, entity);
}

void CloudSystem::OnComponentUpdated(entt::registry& registry, entt::entity entity)
{
    Super::OnComponentUpdated(registry, entity);
}

void CloudSystem::OnComponentDestroyed(entt::registry& registry, entt::entity entity)
{
    Super::OnComponentDestroyed(registry, entity);
}

std::vector< u64 > CloudSystem::UpdateRenderData(const EditorCamera& edCamera)
{
	std::vector< u64 > markedEntities;
    if (m_DirtyEntities.empty())
        return markedEntities;

	m_bHasData = false;
    m_Registry.view< CloudComponent >().each([this, &edCamera, &markedEntities](auto entity, auto& component)
        {
            if (m_bHasData)
                return;

            m_RenderData.id                 = entt::to_integral(entity);
			m_RenderData.data.coverage      = component.coverage;
			m_RenderData.data.cloudType     = component.cloudType;
			m_RenderData.data.precipitation = component.precipitation;

			m_RenderData.data.topLayerKm    = component.bottomHeight_km + component.layerThickness_km;
			m_RenderData.data.bottomLayerKm = component.bottomHeight_km;

			m_RenderData.data.extinctionStrength = component.extinctionStrength;
			m_RenderData.data.extinctionScale    = component.extinctionScale;

			m_RenderData.data.msContribution             = component.msContribution;
			m_RenderData.data.msOcclusion                = component.msOcclusion;
			m_RenderData.data.msEccentricity             = component.msEccentricity;
			m_RenderData.data.groundContributionStrength = component.groundContributionStrength;

			m_RenderData.data.coverage       = component.coverage;
			m_RenderData.data.cloudType      = component.cloudType;
			m_RenderData.data.baseNoiseScale = component.baseNoiseScale;
			m_RenderData.data.baseIntensity  = component.baseIntensity;

			m_RenderData.data.erosionNoiseScale               = component.erosionNoiseScale;
			m_RenderData.data.erosionIntensity                = component.erosionIntensity;
			m_RenderData.data.erosionPower                    = component.erosionPower;
			m_RenderData.data.wispiness                       = component.wispySkewness;
			m_RenderData.data.billowiness                     = component.billowySkewness;
			m_RenderData.data.erosionHeightGradientMultiplier = component.erosionHeightGradientMultiplier;
			m_RenderData.data.erosionHeightGradientPower      = component.erosionHeightGradientPower;

			m_RenderData.data.windDirection = component.windDirection;
			m_RenderData.data.windSpeedMps  = component.windSpeedMps;

			m_RenderData.numCloudRaymarchSteps = component.numCloudRaymarchSteps;
			m_RenderData.numLightRaymarchSteps = component.numLightRaymarchSteps;
			m_RenderData.frontDepthBias        = component.frontDepthBias;
			m_RenderData.temporalBlendAlpha    = component.temporalBlendAlpha;
			m_RenderData.uprezRatio            = component.uprezRatio;

			m_RenderData.blueNoiseTex = component.blueNoiseTex;
			m_RenderData.weatherMap   = component.weatherMap;
			m_RenderData.curlNoiseTex = component.curlNoiseTex;

			// update cloud shadow
			const AtmosphereRenderView& atmosphereView = m_pAtmosphereSystem->GetRenderData();

			float sphereRadius = 100.0f;

			float3 sunDirection = atmosphereView.data.light.direction;
			float3 ray          = -sunDirection;

			float3 camPos = edCamera.GetPosition() * 0.001f + float3{ 0.0f, atmosphereView.data.planetRadiusKm + 0.00005f, 0.0f };
			float2 t      = math::RaySphereIntersection(camPos, ray, float3(0.0f), m_RenderData.data.topLayerKm + atmosphereView.data.planetRadiusKm);

			float3 sunLookAt   = camPos;
			float3 sunPosition = camPos + (t.x > 0.0f ? t.x : t.y) * ray;

			float3 upVec    = float3(0, 1, 0);
			float3 rightVec = glm::normalize(glm::cross(upVec, sunDirection));
			if (glm::length(rightVec) < 0.001f)
				rightVec = glm::normalize(glm::cross(float3(0.0f, 0.0f, 1.0f), sunDirection));
			upVec = glm::normalize(glm::cross(sunDirection, rightVec));

			auto mSunView = glm::lookAtLH(sunPosition, sunLookAt, upVec);

			// Reverse-Z
			auto mSunProj = glm::orthoLH_ZO(
				-sphereRadius, sphereRadius,
				-sphereRadius, sphereRadius,
				sphereRadius * 2.0f,
				0.0f
			);
			m_RenderData.shadow.mSunView        = mSunView;
			m_RenderData.shadow.mSunViewProj    = mSunProj * mSunView;
			m_RenderData.shadow.mSunViewProjInv = glm::inverse(m_RenderData.shadow.mSunViewProj);

            m_bHasData = true;
			markedEntities.emplace_back(m_RenderData.id);
        });

    ClearDirtyEntities();
	return markedEntities;
}

void CloudSystem::CollectRenderData(SceneRenderView& outView) const
{
	if (m_bHasData)
	{
		outView.cloud = m_RenderData;
	}
}

void CloudSystem::RemoveRenderData(u64 entityId)
{
	if (m_RenderData.id == entityId)
	{
		m_bHasData = false;
	}
}

}
