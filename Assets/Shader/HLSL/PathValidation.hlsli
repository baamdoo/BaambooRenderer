#ifndef _HLSL_PATHVALIDATION_HEADER
#define _HLSL_PATHVALIDATION_HEADER

#if PT_VALIDATION
static const uint PT_VALIDATION_BSDF_DIFFUSE           = 1u;
static const uint PT_VALIDATION_BSDF_CONDUCTOR         = 2u;
static const uint PT_VALIDATION_BSDF_DIELECTRIC        = 3u;
static const uint PT_VALIDATION_BSDF_MIXED             = 4u;
static const uint PT_VALIDATION_BSDF_PRINCIPLED        = 5u;
static const uint PT_VALIDATION_BSDF_MIXED_DIELECTRIC  = 6u;
static const uint PT_VALIDATION_BSDF_OPAQUE_DIELECTRIC = 7u;

uint ClassifyValidationBSDF(SurfaceMaterial material)
{
    if (IsPrincipledMaterial(material))
        return PT_VALIDATION_BSDF_PRINCIPLED;

    float transmission = saturate(material.transmission);
    float opaque       = 1.0 - transmission;
    float metallic     = saturate(material.metallic);

    bool hasTransmission       = transmission > PT_LOBE_EPS;
    bool hasDiffuse            = opaque * (1.0 - metallic) > PT_LOBE_EPS;
    bool hasMetallicSpecular   = opaque * metallic > PT_LOBE_EPS;
    bool hasDielectricSpecular = HasDielectricSpecularLobe(material, max(material.ior, 1.0e-4));

    if (hasTransmission && (hasDiffuse || hasMetallicSpecular))
        return PT_VALIDATION_BSDF_MIXED_DIELECTRIC;
    if (hasTransmission)
        return PT_VALIDATION_BSDF_DIELECTRIC;
    if (hasDiffuse && hasMetallicSpecular)
        return PT_VALIDATION_BSDF_MIXED;
    if (hasMetallicSpecular)
        return PT_VALIDATION_BSDF_CONDUCTOR;
    if (hasDiffuse && hasDielectricSpecular)
        return PT_VALIDATION_BSDF_OPAQUE_DIELECTRIC;
    return PT_VALIDATION_BSDF_DIFFUSE;
}


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
    SurfaceMaterial primaryMaterial = LoadSurfaceMaterial(primaryHit.materialID, primaryHit.uv);
    BxDF::Frame primaryFrame = MakeSurfaceFrame(primaryHit);
    float3 primaryWo = BxDF::ToLocal(primaryFrame, -primaryRay.Direction);

    sums.surfaceLobeMask        += BxDF::LayerComposite::SurfaceLobeMask(primaryMaterial, 1.0, primaryMaterial.ior);
    sums.surfaceLobeWeight      += BxDF::LayerComposite::SurfaceLobeWeight(primaryMaterial, primaryWo, 1.0, primaryMaterial.ior);
    sums.sampledLobeFrequency   += BxDF::LayerComposite::SampledLobeVector(primaryBSDFSample);
    sums.albedo                 += primaryMaterial.albedo;
    sums.normal                 += primaryHit.normal * 0.5 + 0.5;
    sums.geometricNormal        += primaryHit.geometricNormal * 0.5 + 0.5;
    sums.depth                  += float3(primaryHit.dist, primaryHit.dist, primaryHit.dist);
    sums.materialParams         += float3(primaryMaterial.roughness, primaryMaterial.metallic, primaryMaterial.transmission);
    sums.materialExtra          += float3(primaryMaterial.ior, float(ClassifyValidationBSDF(primaryMaterial)), primaryMaterial.anisotropy);
    sums.materialSpecularColor  += primaryMaterial.specularColor;
    sums.emission               += primaryMaterial.emission;
    sums.primaryId              += float3(
        primaryHit.materialID == INVALID_INDEX ? 0.0 : float(primaryHit.materialID + 1u),
        float(primaryHit.instanceID + 1u),
        float(primaryHit.primitiveID + 1u));
}

float3 AccumulatedValidationAverage(bool bReset, float3 previousAverage, float previousSamples, float3 sampleSum)
{
    return (bReset ? float3(0.0, 0.0, 0.0) : previousAverage * previousSamples) + sampleSum;
}

#endif // PT_VALIDATION

#endif // _HLSL_PATHVALIDATION_HEADER

