#ifndef _HLSL_MEDIUM_HEADER
#define _HLSL_MEDIUM_HEADER
#include "Sampling.hlsli"

struct Medium
{
    uint mediumID;
    float ior;
};

struct BoundaryMediumPair
{
    Medium mediumI;
    Medium mediumT;

    uint isEntering;
};

struct MediumEntry
{
    uint boundaryInstanceID;

    Medium medium;
};

static const uint MAX_MEDIUM_STACK_CAPACITY = PRIMARY_RAY_MEDIUM_STACK_CAPACITY;
struct MediumStack
{
    uint count;
    MediumEntry entries[MAX_MEDIUM_STACK_CAPACITY];

    // Empty state for unordered air membership
    void Initialize()
    {
        count = 0u;
    }

    // Host state keeps its exterior base and uses TryPush/TryPop for nested boundaries.
    void Initialize(Medium sceneExteriorMedium)
    {
        count = 1u;
        entries[0].boundaryInstanceID = INVALID_INDEX;
        entries[0].medium = sceneExteriorMedium;
    }

    MediumEntry Current()
    {
        return entries[count - 1u];
    }

    bool TryPush(MediumEntry entry)
    {
        if (count < 1u ||
            count >= MAX_MEDIUM_STACK_CAPACITY ||
            entry.boundaryInstanceID == INVALID_INDEX ||
            !IsPathFinite(entry.medium.ior) ||
            entry.medium.ior <= 0.0)
        {
            return false;
        }

        entries[count] = entry;
        count++;
        return true;
    }

    bool TryPop(uint parityID)
    {
        if (count <= 1u)
            return false;

        const uint topIndex = count - 1u;
        if (entries[topIndex].boundaryInstanceID != parityID)
            return false;

        count = topIndex;
        return true;
    }

    // Unordered air membership uses boundary identity
    bool TryAdd(MediumEntry entry)
    {
        if (count >= MAX_MEDIUM_STACK_CAPACITY ||
            entry.boundaryInstanceID == INVALID_INDEX || entry.medium.mediumID == INVALID_INDEX)
            return false;

        for (uint index = 0u; index < count; ++index)
        {
            if (entries[index].boundaryInstanceID == entry.boundaryInstanceID)
                return false;
        }

        entries[count] = entry;
        count++;
        return true;
    }

    bool TryRemove(uint boundaryInstanceID)
    {
        if (count == 0u || count > MAX_MEDIUM_STACK_CAPACITY || boundaryInstanceID == INVALID_INDEX)
            return false;

        uint removeIndex = INVALID_INDEX;
        for (uint index = 0u; index < count; ++index)
        {
            if (entries[index].boundaryInstanceID != boundaryInstanceID)
                continue;

            removeIndex = index;
            count--;
            break;
        }
        if (removeIndex == INVALID_INDEX)
            return false;

        for (uint i = removeIndex; i < count; ++i)
        {
            entries[i] = entries[i + 1];
        }

        return true;
    }

    bool TryResolveBoundaryMediums(uint parityID, Medium toMedium, float NoW, out BoundaryMediumPair pair)
    {
        pair = (BoundaryMediumPair)0;
        if (count < 1 || parityID == INVALID_INDEX)
            return false;

        pair.mediumI = Current().medium;
        if (NoW > EPSILON_MIN)
        {
            pair.isEntering = 1;

            pair.mediumT = toMedium;
        }
        else if (NoW < -EPSILON_MIN)
        {
            pair.isEntering = 0;
            if (count < 2 || Current().boundaryInstanceID != parityID)
                return false;

            pair.mediumT = entries[count - 2].medium;
        }
        else
            return false;

        return true;
    }
};

#define PNANOVDB_HLSL
#define PNANOVDB_ADDRESS_32
#include "../NanoVDB/PNanoVDB.h"

float SampleMediumDensity(pnanovdb_buf_t gridBuffer, pnanovdb_address_t gridAddress, pnanovdb_vec3_t q)
{
    pnanovdb_grid_handle_t grid;
    grid.address = gridAddress;
    
    pnanovdb_tree_handle_t tree = pnanovdb_grid_get_tree(gridBuffer, grid); 
    pnanovdb_root_handle_t root = pnanovdb_tree_get_root(gridBuffer, tree);
    
    pnanovdb_coord_t lower = pnanovdb_root_get_bbox_min(gridBuffer, root);
    pnanovdb_coord_t upper = pnanovdb_root_get_bbox_max(gridBuffer, root);
    if (q.x < float(lower.x) - 1.0f || q.y < float(lower.y) - 1.0f || q.z < float(lower.z) - 1.0f ||
        q.x > float(upper.x) + 1.0f || q.y > float(upper.y) + 1.0f || q.z > float(upper.z) + 1.0f)
        return 0.0f;
    
    pnanovdb_coord_t base;
    base.x = int(floor(q.x)); base.y = int(floor(q.y)); base.z = int(floor(q.z));
    
    float fx = q.x - float(base.x), fy = q.y - float(base.y), fz = q.z - float(base.z);
    
    pnanovdb_readaccessor_t accessor;
    pnanovdb_readaccessor_init(PNANOVDB_REF(accessor), root);
    
    float density = 0.0f;
    float maxDensity = 0.0f;
    for (int z = 0; z < 2; ++z)
    {
        for (int y = 0; y < 2; ++y)
        {
            for (int x = 0; x < 2; ++x)
            {
                pnanovdb_coord_t ijk;
                ijk.x = base.x + x; ijk.y = base.y + y; ijk.z = base.z + z;
                
                pnanovdb_address_t address = pnanovdb_readaccessor_get_value_address(
                    PNANOVDB_GRID_TYPE_FLOAT, gridBuffer, PNANOVDB_REF(accessor), PNANOVDB_REF(ijk));
                
                float weight = (x == 0 ? 1.0f - fx : fx) * (y == 0 ? 1.0f - fy : fy) * (z == 0 ? 1.0f - fz : fz);
                float value = pnanovdb_read_float(gridBuffer, address);
                maxDensity = max(maxDensity, value);
                density += weight * value;
            }
        }
    }
    
    return min(density, maxDensity);
}

struct MediumProperties
{
    float3 sigmaA;
    float3 sigmaS;
    float phaseG;
    float relativeDensity;
};

bool TryLoadMedium(uint mediumID, out MediumData data)
{
    StructuredBuffer< MediumData > media = GetResource(g_MediumResources.media);
    
    uint count, stride;
    media.GetDimensions(count, stride);
    
    data = (MediumData)0;
    if (mediumID >= min(count, g_MeshStreams.mediumCount)) 
        return false;
    
    data = media[mediumID];
    return true;
}

bool EvaluateMedium(uint mediumID, float3 positionWS, out MediumProperties properties)
{
    properties = (MediumProperties)0;
    if (mediumID == INVALID_INDEX) 
        return true; // Current exterior is vacuum
    
    MediumData data;
    if (!TryLoadMedium(mediumID, data) || !all(isfinite(positionWS))) 
        return false;
    
    float density = data.densityScale;
    if (data.densityByteOffset != INVALID_INDEX)
    {
        float3 q = mul(data.mWorldToDensity, float4(positionWS, 1.0)).xyz;
        if (!all(isfinite(q)) || any(abs(q) > 2147483000.0)) 
            return false;
        
        StructuredBuffer<uint> gridBuffer = GetResource(g_MediumResources.density);
        pnanovdb_address_t address = pnanovdb_address_offset(pnanovdb_address_null(), data.densityByteOffset);
        density *= SampleMediumDensity(gridBuffer, address, q);
    }
    
    // Match the separately rounded products used to construct majorant
    precise float3 sigmaA = data.sigmaA * density;
    precise float3 sigmaS = data.sigmaS * density;
    properties.sigmaA          = sigmaA;
    properties.sigmaS          = sigmaS;
    properties.phaseG          = data.phaseG;
    properties.relativeDensity = density;
    
    return isfinite(density) && density >= 0.0 && all(isfinite(properties.sigmaA)) && all(isfinite(properties.sigmaS));
}

// Reference: https://pbr-book.org/4ed/Shapes/Basic_Shape_Interface#RayndashBoundsIntersections
bool GetMediumTrackingInterval(MediumData media, float3 originWS, float3 directionWS, float segmentLength, out float2 trackingInterval)
{
    trackingInterval = 0.0;
    if (!isfinite(segmentLength) || segmentLength < 0.0 ||
        !all(isfinite(originWS)) || !all(isfinite(directionWS)) ||
        abs(dot(directionWS, directionWS) - 1.0) > 1e-4)
        return false;
    if (segmentLength == 0.0)
        return true;

    trackingInterval = float2(0.0, segmentLength);
    if (media.densityByteOffset == INVALID_INDEX)
        return true; // A homogeneous medium has no density-grid bound.

    StructuredBuffer<uint> gridBuffer = GetResource(g_MediumResources.density);
    pnanovdb_grid_handle_t grid;
    grid.address = pnanovdb_address_offset(pnanovdb_address_null(), media.densityByteOffset);
    pnanovdb_tree_handle_t tree = pnanovdb_grid_get_tree(gridBuffer, grid);
    pnanovdb_root_handle_t root = pnanovdb_tree_get_root(gridBuffer, tree);
    pnanovdb_coord_t lower = pnanovdb_root_get_bbox_min(gridBuffer, root);
    pnanovdb_coord_t upper = pnanovdb_root_get_bbox_max(gridBuffer, root);
    if (lower.x > upper.x || lower.y > upper.y || lower.z > upper.z)
    {
        trackingInterval = 0.0;
        return true;
    }

    // Match SampleMediumDensity, including the trilinear interpolation shoulders.
    float3 boundsMin = float3(lower.x, lower.y, lower.z) - 1.0;
    float3 boundsMax = float3(upper.x, upper.y, upper.z) + 1.0;
    
    float3 originIndex    = mul(media.mWorldToDensity, float4(originWS, 1.0)).xyz;
    float3 directionIndex = mul(media.mWorldToDensity, float4(directionWS, 0.0)).xyz;
    // Do not normalize directionIndex: q(s) = originIndex + s * directionIndex.
    if (!all(isfinite(originIndex)) || !all(isfinite(directionIndex)) || all(directionIndex == 0.0))
        return false;

    for (uint axis = 0u; axis < 3u; ++axis)
    {
        float origin    = originIndex[axis];
        float direction = directionIndex[axis];
        if (direction == 0.0)
        {
            if (origin < boundsMin[axis] || origin > boundsMax[axis])
            {
                trackingInterval = 0.0;
                return true;
            }
            continue;
        }

        float s0 = (boundsMin[axis] - origin) / direction;
        float s1 = (boundsMax[axis] - origin) / direction;
        trackingInterval.x = max(trackingInterval.x, min(s0, s1));
        trackingInterval.y = min(trackingInterval.y, max(s0, s1));
        if (trackingInterval.y <= trackingInterval.x)
        {
            trackingInterval = 0.0;
            return true;
        }
    }

    return true;
}

bool EvaluateMedium(MediumStack media, float3 positionWS, out float3 sigmaA, out float3 sigmaS)
{
    sigmaA = sigmaS = 0.0;
    if (media.count > MAX_MEDIUM_STACK_CAPACITY || !all(isfinite(positionWS)))
        return false;

    precise float3 totalA = 0.0;
    precise float3 totalS = 0.0;
    for (uint index = 0u; index < media.count; ++index)
    {
        MediumEntry entry = media.entries[index];

        MediumProperties mp;
        if (entry.medium.mediumID == INVALID_INDEX ||
            !EvaluateMedium(entry.medium.mediumID, positionWS, mp) ||
            any(mp.sigmaA < 0.0) || any(mp.sigmaS < 0.0))
            return false;

        totalA += mp.sigmaA;
        totalS += mp.sigmaS;
    }
    if (!all(isfinite(totalA)) || !all(isfinite(totalS)))
        return false;

    sigmaA = totalA;
    sigmaS = totalS;
    return true;
}

bool TryRoundUpMediumMajorant(float value, out float roundedBound)
{
    roundedBound = 0.0;
    uint bits = asuint(value);
    uint magnitude = bits & 0x7fffffffu;
    if (magnitude >= 0x7f800000u || ((bits & 0x80000000u) != 0u && magnitude != 0u))
        return false;
    if (magnitude == 0u)
        return true;

    uint roundedBits = max(magnitude + 1u, 0x00800000u);
    if (roundedBits >= 0x7f800000u)
        return false;
    roundedBound = asfloat(roundedBits);
    return true;
}

bool GetMediumTrackingInterval(MediumStack media, float3 originWS, float3 directionWS, float segmentLength, out float2 trackingInterval, out float majorant)
{
    trackingInterval = 0.0;
    majorant = 0.0;
    if (media.count > MAX_MEDIUM_STACK_CAPACITY || !isfinite(segmentLength) || segmentLength < 0.0 ||
        !all(isfinite(originWS)) || !all(isfinite(directionWS)) || abs(dot(directionWS, directionWS) - 1.0) > 1e-4)
        return false;
    if (segmentLength == 0.0)
        return true;

    float2 interval = float2(segmentLength, 0.0);
    float totalMajorant = 0.0;
    uint nonzeroCount = 0u;
    for (uint index = 0u; index < media.count; ++index)
    {
        MediumEntry entry = media.entries[index];
        MediumData data;
        if (!TryLoadMedium(entry.medium.mediumID, data))
            return false;

        uint bits = asuint(data.majorant);
        uint magnitude = bits & 0x7fffffffu;
        if (magnitude >= 0x7f800000u || ((bits & 0x80000000u) != 0u && magnitude != 0u))
            return false;
        if (magnitude == 0u)
            continue;

        float fieldMajorant = asfloat(max(magnitude, 0x00800000u));
        if (nonzeroCount == 0u)
            totalMajorant = fieldMajorant;
        else
        {
            precise float sum = totalMajorant + fieldMajorant;
            if (!TryRoundUpMediumMajorant(sum, totalMajorant))
                return false;
        }
        ++nonzeroCount;

        float2 fieldInterval;
        if (!GetMediumTrackingInterval(data, originWS, directionWS, segmentLength, fieldInterval))
            return false;
        if (fieldInterval.y > fieldInterval.x)
        {
            interval.x = min(interval.x, fieldInterval.x);
            interval.y = max(interval.y, fieldInterval.y);
        }
    }
    if (nonzeroCount > 1u)
    {
        // n <= 8: 1+n*2^-23 covers rounding of separate precise A/S sums and their final addition.
        float roundingFactor = asfloat(0x3f800000u + nonzeroCount);
        precise float paddedMajorant = totalMajorant * roundingFactor;
        if (!TryRoundUpMediumMajorant(paddedMajorant, totalMajorant))
            return false;
    }

    if (interval.y > interval.x)
        trackingInterval = interval;
    majorant = totalMajorant;
    return true;
}

// media contains the caller-selected optical fields, not every entry of a nested host stack.
bool EstimateMediumTransmittance(MediumStack media, float3 originWS, float3 directionWS, float segmentLength, inout RngState rng, out float3 estimatedT)
{
    estimatedT = 1.0;
    if (media.count > MAX_MEDIUM_STACK_CAPACITY || !isfinite(segmentLength) || segmentLength < 0.0)
        return false;
    if (segmentLength == 0.0 || media.count == 0u)
        return true;

    float  majorant;
    float2 trackingInterval;
    if (!GetMediumTrackingInterval(media, originWS, directionWS, segmentLength, trackingInterval, majorant))
        return false;
    if (majorant == 0.0 || trackingInterval.y <= trackingInterval.x)
        return true;
    
    // Distances remain relative to the original world-space origin.
    float traversedLength = trackingInterval.x;
    for (;;)
    {
        float u = NextFloat(rng);
        float deltaS = -log(1.0 - u) / majorant;

        traversedLength += deltaS;
        if (traversedLength >= trackingInterval.y)
            break;

        float3 samplePosWS = originWS + traversedLength * directionWS;

        float3 sigmaA, sigmaS;
        if (!EvaluateMedium(media, samplePosWS, sigmaA, sigmaS))
            return false;

        // Equivalent to 1 - sigmaT/M. Subtract first and preserve the ordering:
        // reciprocal/FMA contraction can otherwise make sigmaT == M slightly negative.
        precise float3 sigmaT      = sigmaA + sigmaS;
        precise float3 ratioWeight = (majorant - sigmaT) / majorant;
        estimatedT *= ratioWeight;
    }

    return true;
}

bool EstimateMediumTransmittance(uint mediumID, float3 originWS, float3 directionWS, float segmentLength, inout RngState rng, out float3 estimatedT)
{
    Medium singleMedium;
    singleMedium.mediumID = mediumID;
    singleMedium.ior      = 1.0; // Optical tracking does not use IOR.
    
    MediumStack media;
    media.Initialize(singleMedium);
    return EstimateMediumTransmittance(media, originWS, directionWS, segmentLength, rng, estimatedT);
}

static const uint MEDIUM_EVENT_INVALID = 0u;
static const uint MEDIUM_EVENT_ESCAPE  = 1u;
static const uint MEDIUM_EVENT_ABSORB  = 2u;
static const uint MEDIUM_EVENT_SCATTER = 3u;

struct MediumInteractionSample
{
    float3 weight;
    float  distance;
    float  phaseG; // Only the uint-mediumID adapter supplies a scalar HG parameter.
    
    uint eventType;
};

bool SampleMediumInteraction(MediumStack media, float3 originWS, float3 directionWS, float segmentLength, inout RngState rng, out MediumInteractionSample result)
{
    result = (MediumInteractionSample)0;
    if (media.count > MAX_MEDIUM_STACK_CAPACITY || !isfinite(segmentLength) || segmentLength < 0.0)
        return false;
    if (segmentLength == 0.0 || media.count == 0u)
    {
        result.eventType = MEDIUM_EVENT_ESCAPE;
        result.distance = segmentLength;
        result.weight = 1.0;
        return true;
    }

    float  majorant;
    float2 trackingInterval;
    if (!GetMediumTrackingInterval(media, originWS, directionWS, segmentLength, trackingInterval, majorant))
        return false;
    
    if (majorant == 0.0 || trackingInterval.y <= trackingInterval.x)
    {
        result.eventType = MEDIUM_EVENT_ESCAPE;
        result.distance  = segmentLength;
        result.weight    = 1.0;
        return true;
    }
    
    float  traversedLength = trackingInterval.x;
    float3 segmentWeight   = 1.0;
    for (;;)
    {
        float deltaS = -log(1.0 - NextFloat(rng)) / majorant;
        traversedLength += deltaS;
        
        if (traversedLength >= trackingInterval.y)
        {
            // Leaving density support does not replace the physical segment endpoint.
            result.eventType = MEDIUM_EVENT_ESCAPE;
            result.distance  = segmentLength;
            result.weight    = segmentWeight;
            return true;
        }

        float3 sigmaA, sigmaS;
        if (!EvaluateMedium(media, originWS + traversedLength * directionWS, sigmaA, sigmaS))
            return false;
        
        precise float3 sigmaT = sigmaA + sigmaS;
        if (any(sigmaT > majorant))
            return false;
        precise float3 sigmaN = majorant - sigmaT; // coef for null event
        
        float3 ratioA = sigmaA / majorant;
        float3 ratioS = sigmaS / majorant;
        float3 ratioN = sigmaN / majorant;
        
        float qA = mean3(ratioA);
        float qS = mean3(ratioS);
        float qN = mean3(ratioN);
        float qSum = qA + qS + qN;
        if (qSum <= 0.0)
            return false;
        
        qA /= qSum;
        qS /= qSum;
        qN /= qSum;
        
        float u = NextFloat(rng);
        if (qA > 0.0 && u < qA)
        {
            result.eventType = MEDIUM_EVENT_ABSORB;
            result.distance  = traversedLength;
            result.weight    = 0.0;
            return true;
        }
        else if (qS > 0.0 && u < qA + qS)
        {
            result.eventType = MEDIUM_EVENT_SCATTER;
            result.distance  = traversedLength;
            result.weight    = segmentWeight * ratioS / qS;
            return true;
        }
        else if (qN > 0.0)
        {
            segmentWeight *= ratioN / qN;
        }
        else
        {
            return false;
        }
    }
    
    return false;
}

// Only this singleton adapter carries phaseG; aggregate callers evaluate phase at the scatter point.
bool SampleMediumInteraction(uint mediumID, float3 originWS, float3 directionWS, float segmentLength, inout RngState rng, out MediumInteractionSample result)
{
    Medium singleMedium;
    singleMedium.mediumID = mediumID;
    singleMedium.ior = 1.0;
    MediumStack media;
    media.Initialize(singleMedium);
    if (!SampleMediumInteraction(media, originWS, directionWS, segmentLength, rng, result))
        return false;
    if (result.eventType == MEDIUM_EVENT_SCATTER)
    {
        MediumData data;
        if (!TryLoadMedium(mediumID, data))
            return false;
        result.phaseG = data.phaseG;
    }
    return true;
}

// Reference: https://pbr-book.org/4ed/Volume_Scattering/Phase_Functions
float EvaluateMediumPhase(float cosTheta, float phaseG)
{
    float mu   = clamp(cosTheta, -1.0, 1.0);
    float absG = abs(phaseG);
    
    float peakCosine = phaseG >= 0.0 ? mu : -mu;
    float oneMinusG  = 1.0 - absG;
    
    float denom = oneMinusG * oneMinusG + 2.0 * absG * (1.0 - peakCosine);
    return oneMinusG * (1.0 + absG) / (4.0 * PI * denom * sqrt(denom));
}

struct MediumPhaseSample
{
    float3 directionWS;
    float3 value; // RGB angular response
    float  pdf;
};

bool SampleMediumPhase(float3 directionWS, float phaseG, float2 u, out MediumPhaseSample result)
{
    result = (MediumPhaseSample)0;
    if (!all(isfinite(directionWS)) || abs(dot(directionWS, directionWS) - 1.0) > 1e-4 ||
        !isfinite(phaseG) || abs(phaseG) >= 1.0 ||
        !all(isfinite(u)) || any(u < 0.0) || any(u >= 1.0))
        return false;
    
    float phi = 2.0 * PI * u.y;
    
    float mu;
    if (abs(phaseG) < 1e-3)
    {
        // Exact algebraic rearrangement of the HG inverse CDF around g = 0.
        // Avoiding a subtraction of nearly equal values followed by division by tiny g.
        float r = 2.0 * u.x - 1.0;
        float r2 = r * r;
        float denom = 1.0 + phaseG * r;
        float numerator = 2.0 * r + phaseG * (3.0 + r2 + phaseG * (2.0 * r + phaseG * (r2 - 1.0)));
        mu = clamp(numerator / (2.0 * denom * denom), -1.0, 1.0);
    }
    else
    {
        float g2    = phaseG * phaseG;
        float ratio = (1.0 - g2) / (1.0 - phaseG + 2.0 * phaseG * u.x);
        mu = (1.0 + g2 - ratio * ratio) / (2.0 * phaseG);
    }
    
    float cosTheta = mu;
    float sinTheta = safeSqrt(1.0 - cosTheta * cosTheta);
    float3 sampleDir = SphericalToCartesian(cosTheta, sinTheta, phi);

    float3 tangentWS, bitangentWS;
    BuildONB(directionWS, tangentWS, bitangentWS);
    
    result.pdf = EvaluateMediumPhase(cosTheta, phaseG);
    result.value = result.pdf;
    result.directionWS = sampleDir.x * tangentWS + sampleDir.y * bitangentWS + sampleDir.z * directionWS;
    return true;
}

// Physical RGB phase and the scalar PDF of the constituent-mixture sampler.
bool EvaluateMediumPhase(MediumStack media, float3 positionWS, float cosTheta, out float3 value, out float pdf)
{
    pdf   = 0.0;
    value = 0.0;
    if (media.count > MAX_MEDIUM_STACK_CAPACITY || !isfinite(cosTheta))
        return false;

    MediumProperties fields[MAX_MEDIUM_STACK_CAPACITY];
    precise float3 totalSigmaS = 0.0;
    for (uint index = 0u; index < media.count; ++index)
    {
        MediumEntry entry = media.entries[index];

        MediumProperties field;
        if (entry.medium.mediumID == INVALID_INDEX ||
            !EvaluateMedium(entry.medium.mediumID, positionWS, field) ||
            any(field.sigmaA < 0.0) || any(field.sigmaS < 0.0) ||
            (any(field.sigmaS > 0.0) && (!isfinite(field.phaseG) || abs(field.phaseG) >= 1.0)))
            return false;

        fields[index] = field;
        totalSigmaS += field.sigmaS;
    }

    float scale = max(totalSigmaS.x, max(totalSigmaS.y, totalSigmaS.z));
    if (!all(isfinite(totalSigmaS)) || scale <= 0.0)
        return false;

    float  weightSum   = 0.0;
    float  weightedPDF = 0.0;
    float3 phaseValue  = 0.0;
    for (uint index = 0u; index < media.count; ++index)
    {
        MediumProperties field = fields[index];
        if (all(field.sigmaS == 0.0))
            continue;

        float weight     = mean3(field.sigmaS / scale);
        float fieldPhase = EvaluateMediumPhase(cosTheta, field.phaseG);

        weightSum   += weight;
        weightedPDF += weight * fieldPhase;
        for (uint channel = 0u; channel < 3u; ++channel)
            if (totalSigmaS[channel] > 0.0)
                phaseValue[channel] += (field.sigmaS[channel] / totalSigmaS[channel]) * fieldPhase;
    }

    float directionPDF = weightedPDF / weightSum;
    if (!all(isfinite(phaseValue)) || any(phaseValue < 0.0) || !isfinite(directionPDF) || directionPDF <= 0.0)
        return false;

    pdf   = directionPDF;
    value = phaseValue;
    return true;
}

bool SampleMediumPhase(MediumStack media, float3 positionWS, float3 directionWS, float3 u, out MediumPhaseSample result)
{
    result = (MediumPhaseSample)0;
    if (media.count > MAX_MEDIUM_STACK_CAPACITY || !isfinite(u.x) || u.x < 0.0 || u.x >= 1.0)
        return false;

    MediumProperties fields[MAX_MEDIUM_STACK_CAPACITY];
    precise float3 totalSigmaS = 0.0;
    for (uint index = 0u; index < media.count; ++index)
    {
        MediumEntry entry = media.entries[index];

        MediumProperties field;
        if (entry.medium.mediumID == INVALID_INDEX ||
            !EvaluateMedium(entry.medium.mediumID, positionWS, field) ||
            any(field.sigmaA < 0.0) || any(field.sigmaS < 0.0) ||
            (any(field.sigmaS > 0.0) && (!isfinite(field.phaseG) || abs(field.phaseG) >= 1.0)))
            return false;

        fields[index] = field;
        totalSigmaS += field.sigmaS;
    }

    float scale = max(totalSigmaS.x, max(totalSigmaS.y, totalSigmaS.z));
    if (!all(isfinite(totalSigmaS)) || scale <= 0.0)
        return false;

    float weightSum = 0.0;
    for (uint index = 0u; index < media.count; ++index)
        weightSum += mean3(fields[index].sigmaS / scale);

    uint selectedIndex     = INVALID_INDEX;
    uint lastPositiveIndex = INVALID_INDEX;

    float cumulative = 0.0;
    for (uint index = 0u; index < media.count; ++index)
    {
        float probability = mean3(fields[index].sigmaS / scale) / weightSum;
        if (probability <= 0.0)
            continue;

        lastPositiveIndex = index;
        cumulative += probability;
        if (u.x < cumulative)
        {
            selectedIndex = index;
            break;
        }
    }
    if (selectedIndex == INVALID_INDEX)
        selectedIndex = lastPositiveIndex;

    MediumPhaseSample phaseSample;
    if (!SampleMediumPhase(directionWS, fields[selectedIndex].phaseG, u.yz, phaseSample))
        return false;
    if (!EvaluateMediumPhase(media, positionWS, dot(directionWS, phaseSample.directionWS), phaseSample.value, phaseSample.pdf))
        return false;

    result = phaseSample;
    return true;
}

#endif // _HLSL_MEDIUM_HEADER
