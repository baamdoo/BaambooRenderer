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

	DependsOn< TransformComponent >();
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
	for (auto entity : m_ExpiredEntities)
	{
		RemoveRenderData(entt::to_integral(entity));
	}

	std::vector< u64 > markedEntities;
    if (m_DirtyEntities.empty())
        return markedEntities;

	m_bHasData = false;
    m_Registry.view< CloudComponent >().each([this, &edCamera, &markedEntities](auto entity, auto& component)
        {
            if (m_bHasData)
                return;

			m_RenderData.id = entt::to_integral(entity);

			const AtmosphereRenderView& atmosphereView = m_pAtmosphereSystem->GetRenderData();
			float3 sunDirection = atmosphereView.data.light.direction;
			float3 ray          = -sunDirection;

			float sunElevationDeg = glm::degrees(glm::asin(glm::clamp(sunDirection.y, -1.0f, 1.0f)));
			float absSunElevation = glm::abs(sunElevationDeg);

			float3 camPos = edCamera.GetPosition();

			// base settings
			m_RenderData.data.topLayerKm    = component.bottomHeightKm + component.layerThicknessKm;
			m_RenderData.data.bottomLayerKm = component.bottomHeightKm;

			float hNorm    = math::RemapClamped(camPos.y, m_RenderData.data.bottomLayerKm * 1000.0f, m_RenderData.data.topLayerKm * 1000.0f, 0.0f, 1.0f);
			float coverage = math::RemapClamped(hNorm, 0.2f, 1.0f, 1.0f, 0.0f) * component.cloudsCoverage;
			float overcast = math::RemapClamped(component.cloudsCoverage, 1.2f, 2.1f, 0.0f, 1.0f);

			m_RenderData.data.localOvercast       = overcast;
			//m_RenderData.data.atmosphereTurbidity = component.atmosphereTurbidity;

			float heightAlpha    = math::RemapClamped(glm::clamp(absSunElevation, 8.6f, 90.0f), 8.6f, 48.0f, 0.0f, 1.0f);
			float coverageAlpha  = glm::pow(math::RemapClamped(component.cloudsCoverage, 1.3f, 2.0f, 0.0f, 1.0f), 2.0f);
			float distanceFactor = glm::mix(glm::mix(1.0f, 0.25f, heightAlpha), glm::mix(3.0f, 0.35f, heightAlpha), coverageAlpha);
			m_RenderData.data.shadowTracingDistanceKm = component.cloudsScale * component.shadowTracingDistanceMultiplier * distanceFactor;

			float  alphaDayToDusk  = math::RemapClamped(abs(sunElevationDeg + 8.6f), 0.0f, 11.5f, 1.0f, 0.0f);
			float  alphaNightToDay = math::RemapClamped(absSunElevation, 0.0f, 1.5f, 0.0f, 1.0f);
			float3 albedo = component.albedo *
				glm::mix(component.albedo, float3(1.2f, 1.08076f, 0.8748f), alphaDayToDusk) *
				glm::mix(float3(0.239258f, 0.28877f, 0.510417f), component.albedo, alphaNightToDay);
			m_RenderData.data.cloudAlbedo =
				albedo * math::RemapClamped(absSunElevation, 0.0f, 9.8f, math::RemapClamped(coverage, 1.1f, 1.5f, 1.3f, 1.0f), 1.0f) * component.albedoScale;
			m_RenderData.data.groundContributionStrength = component.groundContributionStrength;

			// shape
			m_RenderData.data.cloudsScale     = component.cloudsMacroUvScale * component.cloudsScale;
			m_RenderData.data.clumpsVariation = component.clumpsVariation * math::RemapClamped(component.cloudsCoverage, 0.0f, 0.3f, 0.0f, 1.0f) * math::RemapClamped(component.cloudsCoverage, 1.75f, 1.95f, 1.0, 0.0f);

			m_RenderData.data.floorVariationClear  = component.floorVariationClear;
			m_RenderData.data.floorVariationCloudy = component.floorVariationCloudy;

			float baseErosionScale = component.baseErosionScale * 1400.0f * component.cloudsScale;
			m_RenderData.data.baseDensity         = glm::clamp((component.cloudsCoverage - math::RemapClamped(component.cloudsCoverage, 0.0f, 0.2f, 0.2f, 0.0f)) * 1.15f, -0.2f, 3.0f);
			m_RenderData.data.baseErosionScale    = float3(baseErosionScale, baseErosionScale * math::RemapClamped(component.layerThicknessKm * 0.01f, 0.0f, 0.3f, 0.6f, 0.01f), baseErosionScale);
			m_RenderData.data.baseErosionPower    = component.baseErosionPower;
			m_RenderData.data.baseErosionStrength = component.baseErosionStrength;
			m_RenderData.data.hfErosionStrength   = component.hfErosionStrength;
			m_RenderData.data.hfErosionDistortion = component.hfErosionDistortion;

			// shade-direct
			m_RenderData.data.scatteringScale = component.scatteringScale;
			m_RenderData.data.extinctionScale = component.extinctionScale * (1.0f / component.cloudsScale);

			m_RenderData.data.msContribution = component.msContribution;
			m_RenderData.data.msOcclusion    = component.msOcclusion;
			m_RenderData.data.msEccentricity = component.msEccentricity;

			m_RenderData.data.silverScatterG = component.silverScatterG;

			// shade-ambient
			m_RenderData.data.ambientIntensity   = component.ambientIntensity;
			m_RenderData.data.ambientSaturation  = component.ambientSaturation;
			m_RenderData.data.topAmbientScale    = component.topAmbientScale;
			m_RenderData.data.bottomAmbientScale = component.bottomAmbientScale * 3.0f;

			// animation
			m_RenderData.data.windDirection = component.windDirection;
			m_RenderData.data.windSpeedMps  = component.windSpeedMps;

			// others
			m_RenderData.numCloudRaymarchSteps = component.numCloudRaymarchSteps;
			m_RenderData.numLightRaymarchSteps = component.numLightRaymarchSteps;
			m_RenderData.frontDepthBias        = component.frontDepthBias;
			m_RenderData.temporalBlendAlpha    = component.temporalBlendAlpha;
			m_RenderData.uprezRatio            = component.uprezRatio;

			m_RenderData.blueNoiseTex = component.blueNoiseTex;
			m_RenderData.weatherMap   = component.weatherMap;
			m_RenderData.curlNoiseTex = component.curlNoiseTex;

			// update cloud shadow
			const float sphereRadius = 15000.0f;

			float3 camPosAbovePlanet = camPos + float3{ 0.0f, atmosphereView.data.planetRadiusKm, 0.0f } * 1000.0f;
			float2 t = math::RaySphereIntersection(camPosAbovePlanet, ray, float3(0.0f), (m_RenderData.data.topLayerKm + atmosphereView.data.planetRadiusKm) * 1000.0f);

			float3 sunLookAt   = camPosAbovePlanet;
			float3 sunPosition = camPosAbovePlanet + (t.x > 0.0f ? t.x : t.y) * ray;

			float3 upVec    = float3(0, 1, 0);
			float3 rightVec = glm::normalize(glm::cross(upVec, sunDirection));
			if (glm::abs(glm::length(rightVec)) < 0.001f)
				rightVec = glm::normalize(glm::cross(float3(0.0f, 0.0f, 1.0f), sunDirection));
			upVec = glm::normalize(glm::cross(sunDirection, rightVec));

			auto mSunView = glm::lookAtLH(sunPosition, sunLookAt, upVec);

			// Reverse-Z
			auto mSunProj = glm::orthoLH_ZO(
				-sphereRadius, sphereRadius,
				-sphereRadius, sphereRadius,
				m_RenderData.data.topLayerKm * 1000.0f,
				0.0f
			);
			m_RenderData.shadow.mSunView        = mSunView;
			m_RenderData.shadow.mSunViewInv     = glm::inverse(mSunView);
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
