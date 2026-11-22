#include "ShaderShared/Scene.h"
#include "Common/Atmosphere.slangh"

struct AtmosphereAerialPerspectivePush
{
    uint ID_Scene;
    uint ID_TransmittanceLUT;
    uint ID_MultiScatteringLUT;
    uint ID_OutAerialPerspectiveLUT;
};

[[vk::push_constant]] ConstantBuffer< AtmosphereAerialPerspectivePush > g_Push : register(b0, ROOT_CONSTANT_SPACE);

[numthreads(4, 4, 4)]
void main(uint3 tID : SV_DispatchThreadID)
{
    RWTexture3D< float4 > OutAerialPerspectiveLUT = GetResource(g_Push.ID_OutAerialPerspectiveLUT);

    uint3 texSize;
    OutAerialPerspectiveLUT.GetDimensions(texSize.x, texSize.y, texSize.z);
    if (any(tID >= texSize))
        return;

    StructuredBuffer< SceneGlobals > sceneBuf = GetResource(g_Push.ID_Scene);
    const AtmosphereData atmosphere = sceneBuf[0].environment.atmosphere;

    Texture2D< float3 > TransmittanceLUT   = GetResource(g_Push.ID_TransmittanceLUT);
    Texture2D< float3 > MultiScatteringLUT = GetResource(g_Push.ID_MultiScatteringLUT);

    const float3 uvw = (float3(tID) + 0.5) / float3(texSize);

    const float3 cameraPosAbovePlanet = GetViewPositionAbovePlanet(sceneBuf[0].camera.position, atmosphere.planetRadiusKm);

    // Readers invert this with w = sqrt(distanceKm / AP_KM_PER_SLICE / numSlices)
    float slice = uvw.z;
    slice *= slice;
    slice *= float(texSize.z);

    float  maxDistance = AP_KM_PER_SLICE * slice;
    float3 rayDir      = normalize(ReconstructWorldPos(uvw.xy, 0.0, sceneBuf[0].camera.mViewProjInv));
    float3 posInFroxel = cameraPosAbovePlanet + rayDir * maxDistance;

    // a froxel under the ground is pulled back onto it
    if (length(posInFroxel) < atmosphere.planetRadiusKm + EPSILON_MIN)
    {
        posInFroxel = normalize(posInFroxel) * (atmosphere.planetRadiusKm + EPSILON_MIN);
        rayDir      = normalize(posInFroxel - cameraPosAbovePlanet);
        maxDistance = length(posInFroxel - cameraPosAbovePlanet);
    }

    float4 result = float4(0.0, 0.0, 0.0, 1.0);

    float rayStart, rayLength;
    if (GetAtmosphereRaySegment(atmosphere, cameraPosAbovePlanet, rayDir, maxDistance, rayStart, rayLength))
    {
        const uint numSteps = (tID.z + 1u) * 2u;
        result = IntegrateAtmosphereInScattering(
            atmosphere, TransmittanceLUT, MultiScatteringLUT,
            cameraPosAbovePlanet, rayDir, rayStart, rayLength, numSteps);
    }

    OutAerialPerspectiveLUT[tID] = result;
}
