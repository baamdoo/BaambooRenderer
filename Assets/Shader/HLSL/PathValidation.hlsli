#ifndef _HLSL_PATHVALIDATION_HEADER
#define _HLSL_PATHVALIDATION_HEADER

#ifndef PT_VALIDATION
#define PT_VALIDATION 1
#endif

#if PT_VALIDATION

struct PathValidationSums
{
    float3 albedo;
    float3 normal;
    float3 depth;
    float3 geometricNormal;
    float3 materialParams;
    float3 materialExtra;
    float3 materialSpecularColor;
    float3 emission;
    float3 diffuseRadiance;
    float3 specularRadiance;
    float3 transmissionRadiance;
    float3 surfaceLobeMask;
    float3 surfaceLobeWeight;
    float3 sampledLobeFrequency;
    float3 primaryId;
};

PathValidationSums ZeroPathValidationSums()
{
    PathValidationSums sums;
    sums.albedo                 = float3(0.0, 0.0, 0.0);
    sums.normal                 = float3(0.0, 0.0, 0.0);
    sums.depth                  = float3(0.0, 0.0, 0.0);
    sums.geometricNormal        = float3(0.0, 0.0, 0.0);
    sums.materialParams         = float3(0.0, 0.0, 0.0);
    sums.materialExtra          = float3(0.0, 0.0, 0.0);
    sums.materialSpecularColor  = float3(0.0, 0.0, 0.0);
    sums.emission               = float3(0.0, 0.0, 0.0);
    sums.diffuseRadiance        = float3(0.0, 0.0, 0.0);
    sums.specularRadiance       = float3(0.0, 0.0, 0.0);
    sums.transmissionRadiance   = float3(0.0, 0.0, 0.0);
    sums.surfaceLobeMask        = float3(0.0, 0.0, 0.0);
    sums.surfaceLobeWeight      = float3(0.0, 0.0, 0.0);
    sums.sampledLobeFrequency   = float3(0.0, 0.0, 0.0);
    sums.primaryId              = float3(0.0, 0.0, 0.0);
    return sums;
}

float3 SumLobes(PathContribution lobes)
{
    return lobes.diffuse + lobes.specular + lobes.transmission;
}

void AccumulatePathContribution(inout PathContribution contribution, uint flags, float3 value)
{
    if ((flags & PT_BSDF_FLAG_DIFFUSE) != 0u)
        contribution.diffuse += value;
    else if ((flags & PT_BSDF_FLAG_TRANSMISSION) != 0u)
        contribution.transmission += value;
    else if ((flags & PT_BSDF_FLAG_GLOSSY) != 0u)
        contribution.specular += value;
}

void AccumulateValidationContribution(inout PathValidationSums sums, PathContribution contribution)
{
    sums.diffuseRadiance      += contribution.diffuse;
    sums.specularRadiance     += contribution.specular;
    sums.transmissionRadiance += contribution.transmission;
}

void AccumulatePrimaryMissValidation(inout PathValidationSums sums)
{
    sums.normal          += float3(0.5, 0.5, 0.5);
    sums.geometricNormal += float3(0.5, 0.5, 0.5);
    sums.depth           += float3(g_Camera.zFar, g_Camera.zFar, g_Camera.zFar);
    sums.primaryId       += float3(0.0, 0.0, 0.0);
}

void PathValidationBuildONB(float3 n, out float3 t, out float3 b)
{
    const float sign = (n.z >= 0.0) ? 1.0 : -1.0;
    const float a = -1.0 / (sign + n.z);
    const float h = n.x * n.y * a;

    t = float3(1.0 + sign * n.x * n.x * a, sign * h, -sign * n.x);
    b = float3(h, sign + n.y * n.y * a, -n.y);
}
void AccumulatePrimaryHitValidation(inout PathValidationSums sums, SurfacePayload primaryHit, RayDesc primaryRay, PathBSDFSample primaryBSDFSample)
{
    SurfaceMaterial primaryMaterial = MakeSurfaceMaterial(primaryHit);
    BxDF::Frame primaryFrame = MakeSurfaceFrame(primaryHit);
    float3 primaryWo = BxDF::ToLocal(primaryFrame, -primaryRay.Direction);

    sums.surfaceLobeMask        += BxDF::Composite::SurfaceLobeMask(primaryMaterial);
    sums.surfaceLobeWeight      += BxDF::Composite::SurfaceLobeWeight(primaryMaterial, primaryWo);
    sums.sampledLobeFrequency   += BxDF::Composite::SampledLobeVector(primaryBSDFSample);
    sums.albedo                 += primaryHit.albedo;
    sums.normal                 += primaryHit.normal * 0.5 + 0.5;
    sums.geometricNormal        += primaryHit.geometricNormal * 0.5 + 0.5;
    sums.depth                  += float3(primaryHit.dist, primaryHit.dist, primaryHit.dist);
    sums.materialParams         += float3(primaryHit.roughness, primaryHit.metallic, primaryHit.transmission);
    sums.materialExtra          += float3(primaryHit.ior, IsPrincipledMaterial(primaryMaterial) ? 1.0 : 0.0, primaryHit.anisotropy);
    sums.materialSpecularColor  += primaryHit.specularColor;
    sums.emission               += primaryHit.emission;
    sums.primaryId              += float3(
        primaryHit.materialID == INVALID_INDEX ? 0.0 : float(primaryHit.materialID + 1u),
        float(primaryHit.instanceID + 1u),
        float(primaryHit.primitiveID + 1u));
}

float3 AccumulatedValidationAverage(bool bReset, float3 previousAverage, float previousSamples, float3 sampleSum)
{
    return (bReset ? float3(0.0, 0.0, 0.0) : previousAverage * previousSamples) + sampleSum;
}

#define PT_LIGHTING_CONTRIBUTION_PARAM , out PathContribution contribution
#define PT_LIGHTING_INIT_CONTRIBUTION() contribution = ZeroPathContribution()
#define PT_EVALUATE_LIGHTING_LOBES(material, wo, wi, fName) \
    PathContribution bsdfLobes = BxDF::Composite::EvaluateLobes(material, wo, wi); \
    float3 fName = SumLobes(bsdfLobes)
#define PT_ACCUMULATE_LIGHTING_CONTRIBUTION(scale) \
    contribution.diffuse += bsdfLobes.diffuse * (scale); \
    contribution.specular += bsdfLobes.specular * (scale); \
    contribution.transmission += bsdfLobes.transmission * (scale)

#else // PT_VALIDATION

#define PT_LIGHTING_CONTRIBUTION_PARAM
#define PT_LIGHTING_INIT_CONTRIBUTION()
#define PT_EVALUATE_LIGHTING_LOBES(material, wo, wi, fName) \
    uint ptUnusedEvalFlags = 0u; \
    float3 fName = BxDF::Composite::Evaluate(material, wo, wi, ptUnusedEvalFlags)
#define PT_ACCUMULATE_LIGHTING_CONTRIBUTION(scale)

#endif // PT_VALIDATION

#endif // _HLSL_PATHVALIDATION_HEADER

