#ifndef _HLSL_PATHSAMPLING_HEADER
#define _HLSL_PATHSAMPLING_HEADER

#include "Sampling.hlsli"

float2 EnvironmentUVFromDirection(float3 directionWS)
{
    float3 d = normalize(directionWS);
    
    float u = atan2(d.x, -d.z) / (2.0 * PI); u = frac(u + 1.0);
    float v = acos(clamp(d.y, -1.0, 1.0)) / PI;
    return float2(u, v);
}

float3 EnvironmentDirectionFromUV(float2 uv)
{
    float phi   = uv.x * 2.0 * PI;
    float theta = uv.y * PI;
    
    float sinTheta = sin(theta);
    return float3(sinTheta * sin(phi), cos(theta), -sinTheta * cos(phi));
}

float3 EvaluateEnvironmentRadiance(float3 directionWS)
{
    if (g_UseEnvironmentMap == 0u)
        return g_EnvironmentRadiance;

    Texture2D< float4 > EnvMap = GetResource(g_EnvironmentMap.index);
    return max(EnvMap.SampleLevel(g_LinearClampSampler, EnvironmentUVFromDirection(directionWS), 0).rgb, float3(0.0, 0.0, 0.0));
}

// Environment importance sampling
bool HasEnvironmentDistribution()
{
    return g_UseEnvironmentMap != 0u && g_UseEnvironmentSampling != 0u &&
        g_EnvironmentDistributionWidth > 0u && g_EnvironmentDistributionHeight > 0u;
}

// Inverse-transform sampling
uint EnvironmentCDFLowerBound(uint baseIndex, uint count, float u)
{
    StructuredBuffer< float > EnvDistribution = GetResource(g_EnvironmentDistribution.index);

    float target = clamp(u, 0.0, 0.99999994);

    uint lo = 0u, hi = count;
    [loop]
    while (lo < hi)
    {
        uint mid = (lo + hi) >> 1u;

        float distribution = EnvDistribution[baseIndex + mid];
        if (distribution < target)
            lo = mid + 1;
        else
            hi = mid;
    }
    
    return min(lo, count - 1u);
}

float EnvironmentCDFSegmentProbability(uint baseIndex, uint index)
{
    StructuredBuffer<float> EnvDistribution = GetResource(g_EnvironmentDistribution.index);
    
    float cdf1 = EnvDistribution[baseIndex + index];
    float cdf0 = (index > 0u) ? EnvDistribution[baseIndex + index - 1u] : 0.0;
    return max(cdf1 - cdf0, 0.0);
}

float EnvironmentTexelSolidAngle(uint y)
{
    float width  = (float)g_EnvironmentDistributionWidth;
    float height = (float)g_EnvironmentDistributionHeight;
    
    float dPhi     = 2.0 * PI / width;
    float dTheta   = PI / height;
    float jacobian = sin(PI * (y + 0.5) / height);
    
    return max(jacobian * dPhi * dTheta, EPSILON_MIN);
}

float EnvironmentPDF(float3 directionW)
{
    if (!HasEnvironmentDistribution())
        return 0.0;

    uint width  = g_EnvironmentDistributionWidth;
    uint height = g_EnvironmentDistributionHeight;
    
    float2 uv = EnvironmentUVFromDirection(directionW);
    uint x = min((uint)(uv.x * (float)width), width - 1u);
    uint y = min((uint)(uv.y * (float)height), height - 1u);

    float rowProb = EnvironmentCDFSegmentProbability(0u, y);
    float colProb = EnvironmentCDFSegmentProbability(height + y * width, x);
    return (rowProb * colProb) / EnvironmentTexelSolidAngle(y);
}

bool SampleEnvironmentLight(float4 u, out float3 wiWS, out float3 Lenv, out float pdfW)
{
    wiWS = float3(0.0, 1.0, 0.0);
    Lenv = float3(0.0, 0.0, 0.0);
    pdfW = 0.0;
    if (!HasEnvironmentDistribution())
        return false;

    uint width  = g_EnvironmentDistributionWidth;
    uint height = g_EnvironmentDistributionHeight;
    uint y = EnvironmentCDFLowerBound(0u, height, u.x);
    uint x = EnvironmentCDFLowerBound(height + y * width, width, u.y);

    float rowProb = EnvironmentCDFSegmentProbability(0u, y);
    float colProb = EnvironmentCDFSegmentProbability(height + y * width, x);
    pdfW = (rowProb * colProb) / EnvironmentTexelSolidAngle(y);
    if (pdfW <= 0.0)
        return false;

    float2 uv = (float2((float)x, (float)y) + clamp(u.zw, 0.0, 0.99999994)) / float2((float)width, (float)height);
    wiWS = normalize(EnvironmentDirectionFromUV(uv));
    
    Lenv = EvaluateEnvironmentRadiance(wiWS);
    return any(Lenv > 0.0);
}


float3 AreaLightRadiance(AreaLight light)
{
    float3 lightColor = float3(light.colorR, light.colorG, light.colorB);
    float  area       = max(4.0 * light.halfWidth * light.halfHeight, EPSILON_MIN);
    return lightColor * light.luminousFluxLm / (PI * area);
}

float3 DiskLightRadiance(DiskLight light)
{
    float3 lightColor = float3(light.colorR, light.colorG, light.colorB);
    float  area       = max(PI * light.radius * light.radius, EPSILON_MIN);
    return lightColor * light.luminousFluxLm / (PI * area);
}

float3 SphereLightRadiance(SphereLight light)
{
    float3 lightColor = float3(light.colorR, light.colorG, light.colorB);
    float  area       = max(4.0 * PI * light.radius * light.radius, EPSILON_MIN);
    return lightColor * light.luminousFluxLm / (PI * area);
}

float3 TubeLightRadiance(TubeLight light)
{
    float3 a = float3(light.posAX, light.posAY, light.posAZ);
    float3 b = float3(light.posBX, light.posBY, light.posBZ);
    float  lengthTube = length(b - a);
    float3 lightColor = float3(light.colorR, light.colorG, light.colorB);
    float  area = max(2.0 * PI * light.radius * lengthTube, EPSILON_MIN);
    return lightColor * light.luminousFluxLm / (PI * area);
}

float3 DirectionalLightIrradiance(DirectionalLight light)
{
    return float3(light.colorR, light.colorG, light.colorB) * light.illuminanceLux;
}

float3 SpotLightIntensity(SpotLight light)
{
    float3 lightColor = float3(light.colorR, light.colorG, light.colorB);
    float  cosOuter   = cos(light.outerConeAngleRad);
    float  solidAngle = 2.0 * PI * max(1.0 - cosOuter, EPSILON_MIN);
    return lightColor * light.luminousFluxLm / solidAngle;
}

float SpotLightFalloff(SpotLight light, float3 wiW)
{
    if (light.outerConeAngleRad <= 0.0)
        return 0.0;

    float3 lightDir = normalize(float3(light.dirX, light.dirY, light.dirZ));
    float  innerAngle = min(light.innerConeAngleRad, light.outerConeAngleRad);
    float  cosTheta = dot(wiW, -lightDir);
    float  cosOuter = cos(light.outerConeAngleRad);
    float  cosInner = cos(innerAngle);

    if (cosInner <= cosOuter + 1.0e-5)
        return cosTheta >= cosOuter ? 1.0 : 0.0;

    return smoothstep(cosOuter, cosInner, cosTheta);
}

uint DirectLightCount()
{
    return g_Lights.numDirectionals + g_Lights.numSpots + g_Lights.numAreas + g_Lights.numDisks + g_Lights.numSpheres + g_Lights.numTubes;
}

float DirectLightSelectionPDF()
{
    uint lightCount = DirectLightCount();
    return lightCount > 0u ? rcp((float)lightCount) : 0.0;
}

float PdfAreaToSolidAngle(float pdfA, float distSq, float cosLight)
{
    float pdfW = pdfA * distSq / max(cosLight, EPSILON_MIN);
    return pdfW;
}


// Single NEE light sample
struct LightSample
{
    float3 wiWS;         // x -> light direction (world), normalized
    float3 shadowTarget; // light point y for the shadow ray (point/area types)
    float3 Le;           // area types: emitted radiance; delta types: intensity term
    float  deltaScale;   // delta extras (spot falloff*attenuation); 1 for directional
    float  pdfW;         // selection-weighted solid-angle pdf (area types); 0 for delta
    
    uint isDelta;       // 1 -> Dirac pdf: no MIS competition (directional/spot)
    uint isDirectional; // 1 -> visibility is a direction test (no endpoint)
    uint valid;
};

LightSample SampleOneLight(float3 p, float selectionPdf, inout RngState rng)
{
    LightSample ls = (LightSample)0;

    uint lightCount = DirectLightCount();
    uint lightIndex = min((uint)(NextFloat(rng) * (float)lightCount), lightCount - 1u);

    if (lightIndex < g_Lights.numDirectionals)
    {
        DirectionalLight light = g_Lights.directionals[lightIndex];
        ls.wiWS          = normalize(-float3(light.dirX, light.dirY, light.dirZ));
        ls.Le            = DirectionalLightIrradiance(light);
        ls.deltaScale    = 1.0;
        ls.isDelta       = 1u;
        ls.isDirectional = 1u;
        ls.valid         = 1u;
        return ls;
    }
    lightIndex -= g_Lights.numDirectionals;

    if (lightIndex < g_Lights.numSpots)
    {
        SpotLight light = g_Lights.spots[lightIndex];
        float3 lightPos = float3(light.posX, light.posY, light.posZ);
        float3 toLight  = lightPos - p;
        float  dist2    = dot(toLight, toLight);
        if (dist2 <= EPSILON_MIN)
            return ls;

        float dist = sqrt(dist2);
        ls.wiWS = toLight / dist;

        float falloff = SpotLightFalloff(light, ls.wiWS);
        if (falloff <= 0.0)
            return ls;

        float radius2     = light.radiusM * light.radiusM;
        float attenuation = 1.0 / max(dist2, radius2);

        ls.shadowTarget = lightPos;
        ls.Le           = SpotLightIntensity(light);
        ls.deltaScale   = falloff * attenuation;
        ls.isDelta      = 1u;
        ls.valid        = 1u;
        return ls;
    }
    lightIndex -= g_Lights.numSpots;

    if (lightIndex < g_Lights.numAreas)
    {
        AreaLight light = g_Lights.areas[lightIndex];

        float3 y;
        float3 lightBackNormal;
        float  pdfA;
        SampleAreaLight(NextFloat2(rng), light, y, lightBackNormal, pdfA);

        float3 toLight = y - p;
        float  dist2   = dot(toLight, toLight);
        if (dist2 <= EPSILON_MIN || pdfA <= 0.0)
            return ls;

        float dist = sqrt(dist2);
        ls.wiWS = toLight / dist;

        float cosLight = saturate(dot(-lightBackNormal, -ls.wiWS));
        if (cosLight <= 0.0)
            return ls;

        ls.shadowTarget = y;
        ls.Le           = AreaLightRadiance(light);
        ls.pdfW         = PdfAreaToSolidAngle(selectionPdf * pdfA, dist2, cosLight);
        ls.valid        = 1u;
        return ls;
    }
    lightIndex -= g_Lights.numAreas;

    if (lightIndex < g_Lights.numDisks)
    {
        DiskLight light = g_Lights.disks[lightIndex];

        float3 y;
        float3 lightBackNormal;
        float  pdfA;
        SampleDiskLight(NextFloat2(rng), light, y, lightBackNormal, pdfA);

        float3 toLight = y - p;
        float  dist2   = dot(toLight, toLight);
        if (dist2 <= EPSILON_MIN || pdfA <= 0.0)
            return ls;

        float dist = sqrt(dist2);
        ls.wiWS = toLight / dist;

        float cosLight = saturate(dot(-lightBackNormal, -ls.wiWS));
        if (cosLight <= 0.0)
            return ls;

        ls.shadowTarget = y;
        ls.Le           = DiskLightRadiance(light);
        ls.pdfW         = PdfAreaToSolidAngle(selectionPdf * pdfA, dist2, cosLight);
        ls.valid        = 1u;
        return ls;
    }
    lightIndex -= g_Lights.numDisks;

    if (lightIndex < g_Lights.numSpheres)
    {
        SphereLight light = g_Lights.spheres[lightIndex];

        float3 y;
        float3 lightNormal;
        float  pdfA;
        SampleSphereLight(NextFloat2(rng), light, y, lightNormal, pdfA);

        float3 toLight = y - p;
        float  dist2   = dot(toLight, toLight);
        if (dist2 <= EPSILON_MIN || pdfA <= 0.0)
            return ls;

        float dist = sqrt(dist2);
        ls.wiWS = toLight / dist;

        float cosLight = saturate(dot(lightNormal, -ls.wiWS));
        if (cosLight <= 0.0)
            return ls;

        ls.shadowTarget = y;
        ls.Le           = SphereLightRadiance(light);
        ls.pdfW         = PdfAreaToSolidAngle(selectionPdf * pdfA, dist2, cosLight);
        ls.valid        = 1u;
        return ls;
    }
    lightIndex -= g_Lights.numSpheres;

    if (lightIndex < g_Lights.numTubes)
    {
        TubeLight light = g_Lights.tubes[lightIndex];

        float3 y;
        float3 lightNormal;
        float  pdfA;
        SampleTubeLight(NextFloat2(rng), light, y, lightNormal, pdfA);

        float3 toLight = y - p;
        float  dist2   = dot(toLight, toLight);
        if (dist2 <= EPSILON_MIN || pdfA <= 0.0)
            return ls;

        float dist = sqrt(dist2);
        ls.wiWS = toLight / dist;

        float cosLight = saturate(dot(lightNormal, -ls.wiWS));
        if (cosLight <= 0.0)
            return ls;

        ls.shadowTarget = y;
        ls.Le           = TubeLightRadiance(light);
        ls.pdfW         = PdfAreaToSolidAngle(selectionPdf * pdfA, dist2, cosLight);
        ls.valid        = 1u;
        return ls;
    }

    return ls;
}

float3 EstimateDirectLighting(float3 p, float3 visibilityNormal, BxDF::Frame frame, float3 woWS, SurfaceMaterial material, inout RngState rng PT_LIGHTING_CONTRIBUTION_PARAM)
{
    PT_LIGHTING_INIT_CONTRIBUTION();
    float3 wo = BxDF::ToLocal(frame, woWS);

    bool bHasTransmission = HasTransmissionLobe(material);
    if ((!bHasTransmission && wo.z <= 0.0) || (bHasTransmission && wo.z == 0.0))
        return float3(0.0, 0.0, 0.0);
    if (BxDF::Composite::IsSmoothConductor(material))
        return float3(0.0, 0.0, 0.0);

    float3 direct = float3(0.0, 0.0, 0.0);
    uint lightCount = DirectLightCount();
    if (lightCount == 0u)
        return direct;

    float selectionPdf = DirectLightSelectionPDF();

    // Sample: pick one light and one point on it
    LightSample ls = SampleOneLight(p, selectionPdf, rng);
    if (ls.valid == 0u)
        return direct;

    // Shared shading tail - identical for all six light types
    // cos check -> shadow ray -> BSDF eval -> (MIS) -> contribution.
    float3 wi = BxDF::ToLocal(frame, ls.wiWS);
    float cosSurface = bHasTransmission ? BxDF::AbsCosTheta(wi) : saturate(wi.z);
    if (cosSurface <= 0.0)
        return direct;

    bool bVisible = (ls.isDirectional != 0u)
        ? IsDirectionVisible(p, visibilityNormal, ls.wiWS)
        : IsVisible(p, visibilityNormal, ls.shadowTarget);
    if (!bVisible)
        return direct;

    PT_EVALUATE_LIGHTING_LOBES(material, wo, wi, f);
    if (!any(f > 0.0))
        return direct;

    float3 lightScale;
    if (ls.isDelta != 0u)
    {
        // Dirac pdf: the whole contribution belongs to NEE (note 02, delta).
        lightScale = ls.Le * (ls.deltaScale * cosSurface) / selectionPdf;
    }
    else
    {
        float pdfBSDF   = BxDF::Composite::PDF(material, wo, wi);
        float misWeight = PowerHeuristic(ls.pdfW, pdfBSDF);
        lightScale = misWeight * ls.Le * cosSurface / ls.pdfW;
    }
    direct += f * lightScale;
    PT_ACCUMULATE_LIGHTING_CONTRIBUTION(lightScale);
    return direct;
}

float3 EstimateEnvironmentDirectLighting(float3 p, float3 visibilityNormal, BxDF::Frame frame, float3 woWS, SurfaceMaterial material, bool bUseMIS, inout RngState rng PT_LIGHTING_CONTRIBUTION_PARAM)
{
    PT_LIGHTING_INIT_CONTRIBUTION();
    if (!HasEnvironmentDistribution())
        return float3(0.0, 0.0, 0.0);
    float3 wo = BxDF::ToLocal(frame, woWS);
    
    bool bHasTransmission = HasTransmissionLobe(material);
    if ((!bHasTransmission && wo.z <= 0.0) || (bHasTransmission && wo.z == 0.0))
        return float3(0.0, 0.0, 0.0);
    if (BxDF::Composite::IsSmoothConductor(material))
        return float3(0.0, 0.0, 0.0);

    float3 wiWS;
    float3 Le;
    float  pdfLightW;
    if (!SampleEnvironmentLight(float4(NextFloat2(rng), NextFloat2(rng)), wiWS, Le, pdfLightW))
        return float3(0.0, 0.0, 0.0);

    float3 wi = BxDF::ToLocal(frame, wiWS);
    float cosSurface = bHasTransmission ? BxDF::AbsCosTheta(wi) : saturate(wi.z);
    if (cosSurface <= 0.0)
        return float3(0.0, 0.0, 0.0);

    if (!IsDirectionVisible(p, visibilityNormal, wiWS))
        return float3(0.0, 0.0, 0.0);

    PT_EVALUATE_LIGHTING_LOBES(material, wo, wi, f);
    if (!any(f > 0.0))
        return float3(0.0, 0.0, 0.0);

    float misWeight = 1.0;
    if (bUseMIS)
    {
        float pdfBSDF = BxDF::Composite::PDF(material, wo, wi);
        misWeight = PowerHeuristic(pdfLightW, pdfBSDF);
    }
    float3 lightScale = misWeight * Le * cosSurface / pdfLightW;
    PT_ACCUMULATE_LIGHTING_CONTRIBUTION(lightScale);
    return f * lightScale;
}

float AreaLightPDFAtHit(float3 refP, float3 hitP, float3 wiWS)
{
    float pdfW = 0.0;
    float selectionPdf = DirectLightSelectionPDF();
    if (selectionPdf <= 0.0)
        return pdfW;

    [loop]
    for (uint i = 0u; i < g_Lights.numAreas; ++i)
    {
        AreaLight light = g_Lights.areas[i];
        float3 position  = float3(light.posX,     light.posY,     light.posZ);
        float3 tangent   = normalize(float3(light.tangentX, light.tangentY, light.tangentZ));
        float3 normal    = normalize(float3(light.normalX,  light.normalY,  light.normalZ));
        float3 bitangent = cross(tangent, normal);
        float3 local     = hitP - position;

        if (abs(dot(local, normal)) > 1.0e-3)
            continue;

        float u = dot(local, tangent);
        float v = dot(local, bitangent);
        if (abs(u) > light.halfWidth + 1.0e-3 || abs(v) > light.halfHeight + 1.0e-3)
            continue;

        float cosLight = saturate(dot(-normal, -wiWS));
        if (cosLight <= 0.0)
            continue;

        float3 toLight = hitP - refP;
        float dist2 = dot(toLight, toLight);
        float pdfA = 1.0 / max(4.0 * light.halfWidth * light.halfHeight, EPSILON_MIN);
        pdfW += PdfAreaToSolidAngle(selectionPdf * pdfA, dist2, cosLight);
    }

    [loop]
    for (uint i = 0u; i < g_Lights.numDisks; ++i)
    {
        DiskLight light = g_Lights.disks[i];
        float3 position  = float3(light.posX,     light.posY,     light.posZ);
        float3 tangent   = normalize(float3(light.tangentX, light.tangentY, light.tangentZ));
        float3 normal    = normalize(float3(light.normalX,  light.normalY,  light.normalZ));
        float3 bitangent = cross(tangent, normal);
        float3 local     = hitP - position;

        if (abs(dot(local, normal)) > 1.0e-3)
            continue;

        float u = dot(local, tangent);
        float v = dot(local, bitangent);
        if (u * u + v * v > light.radius * light.radius + 1.0e-3)
            continue;

        float cosLight = saturate(dot(-normal, -wiWS));
        if (cosLight <= 0.0)
            continue;

        float3 toLight = hitP - refP;
        float dist2 = dot(toLight, toLight);
        float pdfA = 1.0 / max(PI * light.radius * light.radius, EPSILON_MIN);
        pdfW += PdfAreaToSolidAngle(selectionPdf * pdfA, dist2, cosLight);
    }

    [loop]
    for (uint i = 0u; i < g_Lights.numSpheres; ++i)
    {
        SphereLight light = g_Lights.spheres[i];
        float3 center = float3(light.posX, light.posY, light.posZ);
        float3 local = hitP - center;
        float distToCenter = length(local);
        if (abs(distToCenter - light.radius) > 1.0e-3)
            continue;

        float3 normal = local / max(distToCenter, EPSILON_MIN);
        float cosLight = saturate(dot(normal, -wiWS));
        if (cosLight <= 0.0)
            continue;

        float3 toLight = hitP - refP;
        float dist2 = dot(toLight, toLight);
        float pdfA = 1.0 / max(4.0 * PI * light.radius * light.radius, EPSILON_MIN);
        pdfW += PdfAreaToSolidAngle(selectionPdf * pdfA, dist2, cosLight);
    }

    [loop]
    for (uint i = 0u; i < g_Lights.numTubes; ++i)
    {
        TubeLight light = g_Lights.tubes[i];
        float3 a = float3(light.posAX, light.posAY, light.posAZ);
        float3 b = float3(light.posBX, light.posBY, light.posBZ);
        float3 axisVec = b - a;
        float  lengthTube = length(axisVec);
        if (lengthTube <= EPSILON_MIN || light.radius <= 0.0)
            continue;

        float3 axis = axisVec / lengthTube;
        float3 local = hitP - a;
        float  axial = dot(local, axis);
        if (axial < -1.0e-3 || axial > lengthTube + 1.0e-3)
            continue;

        float3 radial = local - axis * axial;
        float  radialLen = length(radial);
        if (abs(radialLen - light.radius) > 1.0e-3)
            continue;

        float3 normal = radial / max(radialLen, EPSILON_MIN);
        float cosLight = saturate(dot(normal, -wiWS));
        if (cosLight <= 0.0)
            continue;

        float3 toLight = hitP - refP;
        float dist2 = dot(toLight, toLight);
        float pdfA = 1.0 / max(2.0 * PI * light.radius * lengthTube, EPSILON_MIN);
        pdfW += PdfAreaToSolidAngle(selectionPdf * pdfA, dist2, cosLight);
    }

    return pdfW;
}


#endif // _HLSL_PATHSAMPLING_HEADER