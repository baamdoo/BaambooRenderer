#ifndef _HLSL_PATHSURFACE_HEADER
#define _HLSL_PATHSURFACE_HEADER

#include "PathUtils.hlsli"

struct SurfaceMaterial
{
    float3 albedo;
    float  roughness;
    float3 emission;
    float  metallic;
    float3 specularColor;
    float  anisotropy;
    float  anisotropyRotation;
    float  tangentFrameSign;
    float  ior;
    float  transmission;
    float  opacity;
    float  clearcoat;
    float  clearcoatRoughness;
    float3 sheenColor;
    float  sheenRoughness;
    float  specularStrength;
    uint   materialFlags;
    uint   isPrincipled;
    uint   isSmooth;
    uint   layerOffset;
    uint   layerCount;
};

bool IsPrincipledMaterial(SurfaceMaterial sm)
{
    return sm.isPrincipled != 0u;
}

bool HasTransmissionLobe(SurfaceMaterial sm)
{
    float wDielectric   = 1.0 - saturate(sm.metallic);
    float wTransmission = wDielectric * saturate(sm.transmission);
    return wTransmission > PT_LOBE_EPS;
}

bool HasCoatingLayers(SurfaceMaterial sm)
{
    return sm.layerCount > 1u;
}

bool HasClearcoatLobe(SurfaceMaterial sm)
{
    return saturate(sm.clearcoat) > PT_LOBE_EPS;
}

bool HasSheenLobe(SurfaceMaterial sm)
{
    return any(sm.sheenColor > PT_LOBE_EPS);
}

float SheenSamplingWeight(SurfaceMaterial sm)
{
    return max3(sm.sheenColor);
}

// Opaque non-metal materials may still have a dielectric specular interface (plastic, ceramic, lacquered wood).
bool HasDielectricSpecularLobe(SurfaceMaterial sm, float eta)
{
    eta = max(eta, 1.0e-4);

    float etaContrast = abs(eta - 1.0) / max(eta + 1.0, 1.0e-4);
    float specularColorMax = max(sm.specularColor.x, max(sm.specularColor.y, sm.specularColor.z));

    return !IsPrincipledMaterial(sm) &&
           saturate(sm.metallic) < 1.0 - PT_LOBE_EPS &&
           saturate(sm.specularStrength) > PT_LOBE_EPS &&
           specularColorMax > PT_LOBE_EPS &&
           etaContrast > PT_LOBE_EPS;
}

float ResolveMicrofacetAlpha(float perceptualRoughness)
{
    const float r = saturate(perceptualRoughness);
    const float alphaRaw = r * r;

    if (alphaRaw <= PT_SMOOTH_ALPHA_THRESHOLD)
        return 0.0;

    return max(alphaRaw, PT_MIN_ROUGHNESS_ALPHA);
}

float2 ResolveMicrofacetAlpha2(float perceptualRoughness, float anisotropy, uint isPrincipled)
{
    float r        = saturate(perceptualRoughness);
    float alpha    = isPrincipled != 0u ? r * r : r;
    float strength = saturate(anisotropy);

    float2 alpha2;
    if (isPrincipled != 0u)
    {
        alpha2 = float2(lerp(alpha, 1.0, strength * strength), alpha);
    }
    else
    {
        float aspect = 1.0 - strength;
        float alphaT = alpha * sqrt(2.0 / (1.0 + aspect * aspect));
        alpha2 = float2(alphaT, aspect * alphaT);
    }

    if (max(alpha2.x, alpha2.y) <= PT_SMOOTH_ALPHA_THRESHOLD)
        return 0.0;

    return max(alpha2, float2(PT_MIN_ROUGHNESS_ALPHA, PT_MIN_ROUGHNESS_ALPHA));
}

SurfaceMaterial LoadSurfaceMaterial(uint materialID, float2 uv, float2 ddxUV, float2 ddyUV, float tangentFrameSign)
{
    StructuredBuffer< MaterialData > Materials = GetResource(g_Materials.index);

    const bool bHasMaterial = materialID != INVALID_INDEX;

    MaterialData mat = (MaterialData)0;
    if (bHasMaterial)
        mat = Materials[materialID];

    SurfaceMaterial sm;
    sm.albedo             = bHasMaterial ? ReadBaseColor(mat, uv, ddxUV, ddyUV) : float3(1.0, 0.0, 1.0);
    sm.roughness          = bHasMaterial ? ReadRoughness(mat, uv, ddxUV, ddyUV) : 1.0;
    sm.emission           = bHasMaterial ? ReadEmission(mat, uv, ddxUV, ddyUV) : float3(0.0, 0.0, 0.0);
    sm.metallic           = bHasMaterial ? ReadMetallic(mat, uv, ddxUV, ddyUV) : 0.0;
    sm.specularColor      = bHasMaterial ? float3(mat.specularColorR, mat.specularColorG, mat.specularColorB) : float3(0.04, 0.04, 0.04);
    sm.anisotropy         = 0.0;
    sm.anisotropyRotation = 0.0;
    if (bHasMaterial)
        ReadAnisotropy(mat, uv, ddxUV, ddyUV, sm.anisotropy, sm.anisotropyRotation);
    sm.tangentFrameSign   = tangentFrameSign < 0.0 ? -1.0 : 1.0;
    sm.ior                = bHasMaterial ? max(mat.ior, 1.0e-4) : 1.0;
    sm.transmission       = bHasMaterial ? ReadTransmission(mat, uv, ddxUV, ddyUV) : 0.0;
    sm.opacity            = bHasMaterial ? ReadOpacity(mat, uv, ddxUV, ddyUV) : 1.0;
    sm.clearcoat          = bHasMaterial ? ReadClearcoat(mat, uv, ddxUV, ddyUV) : 0.0;
    sm.clearcoatRoughness = bHasMaterial ? mat.clearcoatRoughness : 0.0;
    sm.sheenColor         = bHasMaterial ? float3(mat.sheenColorR, mat.sheenColorG, mat.sheenColorB) : float3(0.0, 0.0, 0.0);
    sm.sheenRoughness     = bHasMaterial ? mat.sheenRoughness : 0.0;
    sm.specularStrength   = bHasMaterial ? mat.specularStrength : 1.0;
    sm.materialFlags      = bHasMaterial ? mat.materialFlags : 0u;
    sm.isPrincipled       = (bHasMaterial && mat.materialType == PT_BSDF_PRINCIPLED) ? 1u : 0u;

    sm.isSmooth    = all(ResolveMicrofacetAlpha2(sm.roughness, sm.anisotropy, sm.isPrincipled) == 0.0) ? 1u : 0u;
    sm.layerOffset = bHasMaterial ? mat.layerOffset : INVALID_INDEX;
    sm.layerCount  = bHasMaterial ? max(mat.layerCount, 1u) : 1u;
    return sm;
}

void BuildSurfaceONB(float3 n, out float3 t, out float3 b)
{
    const float sign = (n.z >= 0.0) ? 1.0 : -1.0;
    const float a = -1.0 / (sign + n.z);
    const float h = n.x * n.y * a;

    t = float3(1.0 + sign * n.x * n.x * a, sign * h, -sign * n.x);
    b = float3(h, sign + n.y * n.y * a, -n.y);
}

SurfaceData ResolvePathSurface(SurfacePayload hit, float3 rayDirectionWS, float footprintRadiusWS)
{
    StructuredBuffer< MeshData >      Meshes      = GetResource(g_Meshes.index);
    StructuredBuffer< InstanceData >  Instances   = GetResource(g_Instances.index);
    StructuredBuffer< TransformData > Transforms  = GetResource(g_Transforms.index);
    StructuredBuffer< uint >          IndexBuffer = GetResource(g_MeshStreams.indices);
    StructuredBuffer< Vertex >        VertexBuf   = GetResource(g_MeshStreams.vertices);
    StructuredBuffer< MaterialData >  Materials   = GetResource(g_Materials.index);

    InstanceData  instance  = Instances[hit.instanceID];
    MeshData      mesh      = Meshes[instance.meshID];
    TransformData transform = Transforms[instance.transformID];

    bool bHasMaterial = instance.materialID != INVALID_INDEX;
    MaterialData mat  = (MaterialData)0;
    if (bHasMaterial)
        mat = Materials[instance.materialID];

    bool bUseFaceNormals =
        bHasMaterial &&
        ((mat.materialFlags & MATERIAL_FLAG_FACE_NORMALS) != 0u);

    uint indexBase = mesh.lods[0].iOffset + hit.primitiveID * 3u;
    uint i0 = IndexBuffer[indexBase + 0u];
    uint i1 = IndexBuffer[indexBase + 1u];
    uint i2 = IndexBuffer[indexBase + 2u];

    Vertex v0 = VertexBuf[mesh.vOffset + i0];
    Vertex v1 = VertexBuf[mesh.vOffset + i1];
    Vertex v2 = VertexBuf[mesh.vOffset + i2];

    float3 bary = float3(
        1.0 - hit.barycentrics.x - hit.barycentrics.y,
        hit.barycentrics.x,
        hit.barycentrics.y);

    float3 p0 = float3(v0.posX, v0.posY, v0.posZ);
    float3 p1 = float3(v1.posX, v1.posY, v1.posZ);
    float3 p2 = float3(v2.posX, v2.posY, v2.posZ);

    float3 edge01          = p1 - p0;
    float3 edge02          = p2 - p0;

    float3 p0WS = mul(transform.mLocalToWorld, float4(p0, 1.0)).xyz;
    float3 p1WS = mul(transform.mLocalToWorld, float4(p1, 1.0)).xyz;
    float3 p2WS = mul(transform.mLocalToWorld, float4(p2, 1.0)).xyz;

    float3 edge01WS = p1WS - p0WS;
    float3 edge02WS = p2WS - p0WS;

    float2 uv0 = float2(v0.u, v0.v);
    float2 uv1 = float2(v1.u, v1.v);
    float2 uv2 = float2(v2.u, v2.v);

    float2 uv01 = uv1 - uv0;
    float2 uv02 = uv2 - uv0;

    float2 uv = uv0 * bary.x + uv1 * bary.y + uv2 * bary.z;

    float3 geometricNormal     = cross(edge01, edge02);
    float  geometricNormalLen2 = dot(geometricNormal, geometricNormal);

    float edgeLength = dot(edge01, edge01) * dot(edge02, edge02);
    bool bValidGeometricNormal =
        IsPathFinite(geometricNormalLen2) &&
        IsPathFinite(edgeLength) &&
        edgeLength > 0.0 &&
        geometricNormalLen2 > edgeLength * sq(EPSILON_MIN);

    float3x3 normalTransform = transpose((float3x3)transform.mWorldToLocal);
    float3 geometricNormalWS = float3(0.0, 1.0, 0.0);
    if (bValidGeometricNormal)
    {
        geometricNormal *= rsqrt(geometricNormalLen2);

        float3 transformedN    = mul(normalTransform, geometricNormal);
        float  transformedLen2 = dot(transformedN, transformedN);
        if (IsPathFinite3(transformedN) && transformedLen2 > 0.0)
            geometricNormalWS = transformedN * rsqrt(transformedLen2);
    }

    float3 shadingNormal =
        float3(v0.normalX, v0.normalY, v0.normalZ) * bary.x +
        float3(v1.normalX, v1.normalY, v1.normalZ) * bary.y +
        float3(v2.normalX, v2.normalY, v2.normalZ) * bary.z;

    float3 shadingNormalWS = mul(normalTransform, shadingNormal);
    float shadingNormalLen2 = dot(shadingNormalWS, shadingNormalWS);
    shadingNormalWS = (shadingNormalLen2 > EPSILON_MIN)
        ? shadingNormalWS * rsqrt(shadingNormalLen2)
        : geometricNormalWS;

    if (bUseFaceNormals)
        shadingNormalWS = geometricNormalWS;
    else if (dot(shadingNormalWS, geometricNormalWS) < 0.0)
        shadingNormalWS = -shadingNormalWS;

    float3 tangent =
        float3(v0.tangentX, v0.tangentY, v0.tangentZ) * bary.x +
        float3(v1.tangentX, v1.tangentY, v1.tangentZ) * bary.y +
        float3(v2.tangentX, v2.tangentY, v2.tangentZ) * bary.z;
    float tangentSign = v0.tangentW < 0.0 ? -1.0 : 1.0;

    float transformSign =
        determinant((float3x3)transform.mLocalToWorld) < 0.0
        ? -1.0
        : 1.0;
    float tangentSignWS = tangentSign * transformSign;
    float3 tangentWS = mul((float3x3)transform.mLocalToWorld, tangent);

    // Build the anisotropic uv-footprint from the geometric plane
    float NoD     = dot(geometricNormalWS, rayDirectionWS);
    float stretch = rcp(max(abs(NoD), 1.0e-4));

    float3 projectedRay  = rayDirectionWS - NoD * geometricNormalWS;
    float  projectedLen2 = dot(projectedRay, projectedRay);

    float3 majorDirectionWS;
    float3 minorDirectionWS;
    if (IsPathFinite(projectedLen2) && projectedLen2 > EPSILON_MIN)
    {
        majorDirectionWS = projectedRay * rsqrt(projectedLen2);
        minorDirectionWS = normalize(cross(geometricNormalWS, majorDirectionWS));
    }
    else
    {
        BuildSurfaceONB(geometricNormalWS, majorDirectionWS, minorDirectionWS);
        stretch = 1.0;
    }

    float footprintDiameterWS = 2.0 * footprintRadiusWS;
    float3 minorDeltaWS = footprintDiameterWS * minorDirectionWS;
    float3 majorDeltaWS = footprintDiameterWS * stretch * majorDirectionWS;

    float g00 = dot(edge01WS, edge01WS);
    float g01 = dot(edge01WS, edge02WS);
    float g11 = dot(edge02WS, edge02WS);
    float gramScale = g00 * g11;
    float gramDet   = gramScale - g01 * g01;

    float2 dUV = 0.0;
    float2 duv = 0.0;
    bool bValidUVGradient =
        IsPathFinite(gramScale) &&
        IsPathFinite(gramDet) &&
        gramScale > 0.0 &&
        gramDet > gramScale * EPSILON_MIN;
    if (bValidUVGradient)
    {
        float major0 = dot(edge01WS, majorDeltaWS);
        float major1 = dot(edge02WS, majorDeltaWS);
        float minor0 = dot(edge01WS, minorDeltaWS);
        float minor1 = dot(edge02WS, minorDeltaWS);

        float invGramDet = rcp(gramDet);
        float majorA = (g11 * major0 - g01 * major1) * invGramDet;
        float majorB = (g00 * major1 - g01 * major0) * invGramDet;
        float minorA = (g11 * minor0 - g01 * minor1) * invGramDet;
        float minorB = (g00 * minor1 - g01 * minor0) * invGramDet;

        dUV = majorA * uv01 + majorB * uv02;
        duv = minorA * uv01 + minorB * uv02;
    }

    shadingNormalWS = bHasMaterial ? 
        ReadShadingNormal(mat, uv, dUV, duv, shadingNormalWS, tangentWS, tangentSignWS) : shadingNormalWS;

    if (dot(shadingNormalWS, geometricNormalWS) < 0.0)
    {
        shadingNormalWS = -shadingNormalWS;
        tangentSignWS = -tangentSignWS;
    }

    tangentWS -= shadingNormalWS * dot(tangentWS, shadingNormalWS);
    float tangentWSLen2 = dot(tangentWS, tangentWS);
    if (tangentWSLen2 <= EPSILON_MIN)
    {
        float3 fallbackB;
        BuildSurfaceONB(shadingNormalWS, tangentWS, fallbackB);
    }
    else
    {
        tangentWS *= rsqrt(tangentWSLen2);
    }

    SurfaceData surface;
    surface.hitKind         = hit.hitKind;
    surface.normal          = shadingNormalWS;
    surface.dist            = hit.dist;
    surface.tangent         = float4(tangentWS, tangentSignWS);
    surface.geometricNormal = geometricNormalWS;
    surface.uv              = uv;
    surface.instanceID      = hit.instanceID;
    surface.primitiveID     = hit.primitiveID;
    surface.ddxUV           = dUV;
    surface.ddyUV           = duv;
    return surface;
}

// Tangent frame at the hit point
BxDF::Frame MakeSurfaceFrame(SurfaceData hp)
{
    BxDF::Frame frame;
    frame.N = hp.normal;

    float3 T = hp.tangent.xyz - frame.N * dot(hp.tangent.xyz, frame.N);
    float tangentLen2 = dot(T, T);
    if (tangentLen2 <= EPSILON_MIN)
    {
        BuildSurfaceONB(frame.N, frame.T, frame.B);
    }
    else
    {
        frame.T = T * rsqrt(tangentLen2);
        frame.B = cross(frame.N, frame.T);
    }

    return frame;
}

float GetClearcoatAlpha(SurfaceMaterial material)
{
    return max(ResolveMicrofacetAlpha(material.clearcoatRoughness), 1.0e-3);
}

// Resolve model-specific alpha roughness along the anisotropy tangent/bitangent axes.
float2 GetAlpha2(SurfaceMaterial material)
{
    return ResolveMicrofacetAlpha2(
        material.roughness,
        material.anisotropy,
        material.isPrincipled);
}

float GetAnisotropyRotation(SurfaceMaterial material)
{
    // Material/texture rotation is authored in the signed tangent frame. 
    // BxDF evaluation uses the canonical right-handed frame, so parity is applied here.
    return material.tangentFrameSign * material.anisotropyRotation;
}

#endif // _HLSL_PATHSURFACE_HEADER
