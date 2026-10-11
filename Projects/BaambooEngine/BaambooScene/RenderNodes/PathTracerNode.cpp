#include "BaambooPch.h"
#include "PathTracerNode.h"

#include "RenderCommon/CommandContext.h"
#include "RenderCommon/RenderDevice.h"

#include "BaambooScene/Scene.h"

#include "Utils/Math.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <cstring>
#include <cmath>
#include <limits>
#include <vector>

namespace baamboo
{

namespace
{

constexpr u32 PATH_TRACER_DEFAULT_SAMPLES_PER_FRAME = 1;
constexpr u32 PATH_TRACER_DEFAULT_DUMP_TARGET_SAMPLES = 128;
constexpr u64 PATH_TRACER_SCENE_DIRTY_MASK =
    (1ULL << eComponentType::CTransform)  |
    (1ULL << eComponentType::CStaticMesh) |
    (1ULL << eComponentType::CDynamicMesh) |
    (1ULL << eComponentType::CMaterial)   |
    (1ULL << eComponentType::CMedium)     |
    (1ULL << eComponentType::CSkyLight)   |
    (1ULL << eComponentType::CAtmosphere) |
    (1ULL << eComponentType::CCloud)      |
    (1ULL << eComponentType::CLocalLight);

constexpr u64 PATH_TRACER_PRIMARY_MEDIUM_DIRTY_MASK =
    (1ULL << eComponentType::CTransform)   |
    (1ULL << eComponentType::CStaticMesh)  |
    (1ULL << eComponentType::CDynamicMesh) |
    (1ULL << eComponentType::CMaterial) |
    (1ULL << eComponentType::CMedium);

constexpr u32 PRIMARY_MEDIUM_QUERY_MAX_TRACE_HITS = 64u;
constexpr f32 PRIMARY_MEDIUM_QUERY_MIN_SCENE_RADIUS = 1.0f;

constexpr u32 PATH_TRACER_ENV_CDF_MAGIC = 0x46444345; // 'ECDF' little-endian
constexpr u32 PATH_TRACER_ENV_CDF_VERSION = 1;

struct EnvironmentDistributionFileHeader
{
    u32 magic = 0;
    u32 version = 0;
    u32 width = 0;
    u32 height = 0;
    u32 count = 0;
};

template< typename T >
bool BytesEqual(const T& lhs, const T& rhs)
{
    return std::memcmp(&lhs, &rhs, sizeof(T)) == 0;
}

bool IsFiniteFloat3(const float3& value)
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

bool TryBuildPrimaryMediumQueryParams(const SceneRenderView& renderView, PrimaryMediumQueryParams& params)
{
    params = {};
    params.sceneBoundsCenter     = renderView.camera.pos;
    params.sceneBoundsRadius     = 0.0f;
    params.worldExteriorMediumID = kInvalidIndex;
    params.worldExteriorIOR      = 1.0f;
    params.maxTraceHits          = PRIMARY_MEDIUM_QUERY_MAX_TRACE_HITS;

    if (!IsFiniteFloat3(renderView.camera.pos))
        return false;

    float3 boundsMin(std::numeric_limits<f32>::max());
    float3 boundsMax(std::numeric_limits<f32>::lowest());
    bool bHasBounds = false;

    for (const auto& [entityID, draw] : renderView.draws)
    {
        UNUSED(entityID);
        if (!IsValidIndex(draw.mesh))
            continue;
        if (draw.mesh >= renderView.meshes.size() ||
            !IsValidIndex(draw.transform) || draw.transform >= renderView.transforms.size())
        {
            return false;
        }

        const BoundingBox& localBounds = renderView.meshes[draw.mesh].aabb;
        const mat4& mWorld = renderView.transforms[draw.transform].mWorld;
        const float3 localMin = localBounds.Min();
        const float3 localMax = localBounds.Max();

        for (u32 cornerIndex = 0u; cornerIndex < 8u; ++cornerIndex)
        {
            const float3 corner(
                (cornerIndex & 1u) != 0u ? localMax.x : localMin.x,
                (cornerIndex & 2u) != 0u ? localMax.y : localMin.y,
                (cornerIndex & 4u) != 0u ? localMax.z : localMin.z);
            const float3 worldCorner = float3(mWorld * float4(corner, 1.0f));
            if (!IsFiniteFloat3(worldCorner))
                return false;

            boundsMin = glm::min(boundsMin, worldCorner);
            boundsMax = glm::max(boundsMax, worldCorner);
            bHasBounds = true;
        }
    }

    if (!bHasBounds)
    {
        const f32 visibleDistance = renderView.camera.maxVisibleDistance;
        params.sceneBoundsRadius = std::isfinite(visibleDistance)
            ? std::max(std::abs(visibleDistance), PRIMARY_MEDIUM_QUERY_MIN_SCENE_RADIUS)
            : PRIMARY_MEDIUM_QUERY_MIN_SCENE_RADIUS;
        return true;
    }

    params.sceneBoundsCenter = (boundsMin + boundsMax) * 0.5f;
    params.sceneBoundsRadius = glm::length(boundsMax - boundsMin) * 0.5f;
    if (!IsFiniteFloat3(params.sceneBoundsCenter) || !std::isfinite(params.sceneBoundsRadius))
        return false;

    params.sceneBoundsRadius = std::max(params.sceneBoundsRadius, PRIMARY_MEDIUM_QUERY_MIN_SCENE_RADIUS);
    return true;
}

u64 GatherPathTracerDirtyMask(const SceneRenderView& renderView, const std::array< u64, NumComponents >& lastComponentRevisions)
{
    u64 dirtyMask = 0;
    for (u32 component = 0; component < NumComponents; ++component)
    {
        const u64 componentMask = 1ULL << component;
        if ((PATH_TRACER_SCENE_DIRTY_MASK & componentMask) != 0 &&
            renderView.componentRevisions[component] != lastComponentRevisions[component])
        {
            dirtyMask |= componentMask;
        }
    }
    return dirtyMask;
}

void ComputeLightPowerWeights(const LightRenderView& light, float sceneBoundsRadius, std::vector< float >& outWeights)
{
    u32 numLights = light.numDirectionals + light.numSpots + light.numAreas + light.numDisks + light.numSpheres + light.numTubes;

    outWeights.clear();
    outWeights.reserve(numLights);
    for (u32 i = 0u; i < numLights; ++i)
    {
        if (i < light.numDirectionals)
        {
            u32 index = i;

            float flux = light.directionals[index].illuminanceLux * PI * sceneBoundsRadius * sceneBoundsRadius;
            outWeights.push_back(flux * math::Luminance(light.directionals[index].color));
        }
        else if (i < light.numDirectionals + light.numSpots)
        {
            u32 index = i - light.numDirectionals;

            float flux = light.spots[index].luminousFluxLm;
            outWeights.push_back(flux * math::Luminance(light.spots[index].color));
        }
        else if (i < light.numDirectionals + light.numSpots + light.numAreas)
        {
            u32 index = i - light.numDirectionals - light.numSpots;

            float flux = light.areas[index].luminousFluxLm;
            outWeights.push_back(flux * math::Luminance(light.areas[index].color));
        }
        else if (i < light.numDirectionals + light.numSpots + light.numAreas + light.numDisks)
        {
            u32 index = i - light.numDirectionals - light.numSpots - light.numAreas;

            float flux = light.disks[index].luminousFluxLm;
            outWeights.push_back(flux * math::Luminance(light.disks[index].color));
        }
        else if (i < light.numDirectionals + light.numSpots + light.numAreas + light.numDisks + light.numSpheres)
        {
            u32 index = i - light.numDirectionals - light.numSpots - light.numAreas - light.numDisks;

            float flux = light.spheres[index].luminousFluxLm;
            outWeights.push_back(flux * math::Luminance(light.spheres[index].color));
        }
        else
        {
            u32 index = i - light.numDirectionals - light.numSpots - light.numAreas - light.numDisks - light.numSpheres;

            float flux = light.tubes[index].luminousFluxLm;
            outWeights.push_back(flux * math::Luminance(light.tubes[index].color));
        }
    }
}

} // namespace

PathTracerNode::PathTracerNode(render::RenderDevice& rd)
    : Super(rd, "PathTracer")
{
    using namespace render;

    const auto makeAOV = [&](const char* name)
    {
        return Texture::Create(
            m_RenderDevice,
            name,
            {
                .resolution = { m_RenderDevice.WindowWidth(), m_RenderDevice.WindowHeight(), 1 },
                .format     = eFormat::RGBA32_FLOAT,
                .imageUsage = eTextureUsage_Sample | eTextureUsage_Storage | eTextureUsage_TransferSource
            });
    };

    m_pAccumulation = makeAOV("PathTracer::Accumulation");
    m_pRadiance     = makeAOV("PathTracer::Radiance");
#if PT_VALIDATION
    InitializeValidationResources();
#endif // PT_VALIDATION

    m_pEnvironmentDistribution = Buffer::Create(
        m_RenderDevice,
        "PathTracer::EnvironmentDistributionFallback",
        {
            .count              = 1,
            .elementSizeInBytes = sizeof(f32),
            .mapDirection       = 1,
            .bufferUsage        = eBufferUsage_Storage,
        });
    ResetEnvironmentDistribution();

    m_pLightSelectionCDF = Buffer::Create(
        m_RenderDevice,
        "PathTracer::LightSelectionCDFFallback",
        {
            .count              = 1,
            .elementSizeInBytes = sizeof(f32),
            .mapDirection       = 1,
            .bufferUsage        = eBufferUsage_Storage,
        });
    if (m_pLightSelectionCDF && m_pLightSelectionCDF->MappedMemory())
    {
        auto* p = static_cast< f32* >(m_pLightSelectionCDF->MappedMemory());
        p[0] = 1.0f;
        m_pLightSelectionCDF->FlushMappedRange(0, sizeof(f32));
    }
    RebuildMaterialSlabBuffer({});

    // The camera containment query writes the initial medium stack for every primary path.
    m_pPrimaryRayMediumSeed = Buffer::Create(
        m_RenderDevice,
        "PathTracer::PrimaryRayMediumSeed",
        {
            .count              = 1,
            .elementSizeInBytes = sizeof(PrimaryRayMediumStackSeedData),
            .bufferUsage        = eBufferUsage_Storage | eBufferUsage_TransferSource,
        });

    m_pPrimaryMediumQueryPSO = ComputePipeline::Create(m_RenderDevice, "PrimaryMediumQueryPSO");
    m_pPrimaryMediumQueryPSO->SetComputeShader(
        Shader::Create(m_RenderDevice, "PrimaryMediumQueryCS",
            {
                .stage    = eShaderStage::Compute,
                .filename = "PrimaryMediumQueryCS"
            })).Build();

    m_pPSO = RaytracingPipeline::Create(m_RenderDevice, "PathTracerPSO");
    m_pPSO->SetShaderLibrary(
            Shader::Create(m_RenderDevice, "PathTracerLIB",
                {
                    .stage    = eShaderStage::RayGeneration,
                    .filename = "PathTracerLIB"
                }))
          .SetRayGenerationShader("RayGen")
          .AddMissShader("PrimaryMiss")
          .AddMissShader("ShadowMiss")
          .AddHitGroup(
              {
                  .hitGroupName           = "PrimaryHitGroup",
                  .closestHitShaderExport = "ClosestHit_Primary",
                  .anyHitShaderExport     = "AnyHit_PrimaryAlpha",
              })
          .AddHitGroup(
              {
                  .hitGroupName       = "ShadowHitGroup",
                  .anyHitShaderExport = "AnyHit_ShadowAlpha",
              })
          .SetMaxPayloadSize(sizeof(float) * 6) // SurfacePayload: 24 bytes; ShadowPayload is smaller.
          .SetMaxAttributeSize(sizeof(float) * 2)
          .SetMaxRecursionDepth(1)
          .Build();

    m_pSBT = ShaderBindingTable::Create(m_RenderDevice, "PathTracerSBT");
    m_pSBT->SetRayGenerationRecord(m_pPSO->GetShaderIdentifier("RayGen"), nullptr, 0)
        .AddMissRecord("PrimaryMiss", m_pPSO->GetShaderIdentifier("PrimaryMiss"))
        .AddMissRecord("ShadowMiss", m_pPSO->GetShaderIdentifier("ShadowMiss"))
        .AddHitGroupRecord("PrimaryHitGroup", m_pPSO->GetShaderIdentifier("PrimaryHitGroup"))
        .AddHitGroupRecord("ShadowHitGroup", m_pPSO->GetShaderIdentifier("ShadowHitGroup"))
        .Build();
}

void PathTracerNode::Apply(render::CommandContext& context, const SceneRenderView& renderView)
{
    using namespace render;

    auto& rm = m_RenderDevice.GetResourceManager();

    auto pTLAS = rm.GetSceneResource().GetTLAS();
    if (!pTLAS || !pTLAS->IsBuilt())
    {
        g_FrameData.pColor = rm.GetFlatBlackTexture();
        return;
    }

    if (m_bAutomationBatch && (HasAutomationFailed() || IsAOVDumpComplete()))
    {
        g_FrameData.pColor = m_pRadiance;
        return;
    }

#if PT_VALIDATION
    PublishValidationReadbackSlot();
#endif

    const bool bCameraChanged =
        !m_bHasCameraState ||
        !BytesEqual(m_LastView, renderView.camera.mView) ||
        !BytesEqual(m_LastProj, renderView.camera.mProj) ||
        !BytesEqual(m_LastViewport, renderView.viewport);
    const u64 sceneDirtyMask = GatherPathTracerDirtyMask(renderView, m_LastComponentRevisions);
    const bool bSceneChanged = sceneDirtyMask != 0;
    const u64 materialSlabRevision = renderView.componentRevisions[eComponentType::CMaterial];
    bool bMaterialSlabsRebuilt = false;
    if (materialSlabRevision != m_MaterialSlabRevision)
    {
        bMaterialSlabsRebuilt = RebuildMaterialSlabBuffer(renderView.materialSlabs);
        if (bMaterialSlabsRebuilt)
            m_MaterialSlabRevision = materialSlabRevision;
    }

    // A changed selection CDF changes the estimator - accumulation must restart.
    const bool bLightSelectionChanged = UpdateLightSelectionDistribution(renderView);

    const bool bPrimaryMediumQueryDirty =
        !m_bHasPrimaryMediumQueryState ||
        !BytesEqual(m_LastPrimaryMediumQueryPosition, renderView.camera.pos) ||
        (sceneDirtyMask & PATH_TRACER_PRIMARY_MEDIUM_DIRTY_MASK) != 0 ||
        bMaterialSlabsRebuilt;

    if (bCameraChanged || bSceneChanged || bLightSelectionChanged)
    {
        if (!CheckAutomationSceneReset())
        {
            g_FrameData.pColor = m_pRadiance;
            return;
        }
        m_LastView        = renderView.camera.mView;
        m_LastProj        = renderView.camera.mProj;
        m_LastViewport    = renderView.viewport;
        m_bHasCameraState = true;

        m_AccumulatedSampleCount = 0;
        ResetRenderTiming();
        m_LastResetDirtyMask      = sceneDirtyMask;
        m_bHasRendered           = false;
        m_bDumpCompleted.store(false, std::memory_order_release);
#if PT_VALIDATION
        ResetValidationReadbackAggregation();
#endif

        for (u32 component = 0; component < NumComponents; ++component)
        {
            if ((sceneDirtyMask & (1ULL << component)) != 0)
                m_LastComponentRevisions[component] = renderView.componentRevisions[component];
        }
    }

    // Once the target is reached, freeze the accumulated image. Validation builds
    // keep ticking only long enough for the frame-latent readback ring to drain.
    if (m_bDumpRequested.load(std::memory_order_acquire) &&
        m_bHasRendered &&
        m_AccumulatedSampleCount >= m_DumpTargetSamples)
    {
#if PT_VALIDATION
        if (HasPendingValidationReadback())
        {
            AdvanceValidationReadbackSlot();
            g_FrameData.pColor = m_pRadiance;
            return;
        }
#endif
        // The last accumulation dispatch was submitted by the previous frame.
        // Wait for its completion before stopping the render-loop wall clock;
        // scene debug, readback and EXR output are outside this new interval.
        m_RenderDevice.Flush();
        const f32 renderLoopWallSeconds = std::chrono::duration<f32>(
            std::chrono::steady_clock::now() - m_RenderStartTime).count();
        DumpRenderViewDebug(renderView, PATH_TRACER_SCENE_DIRTY_MASK);
        bool bDumpOk = true;
#if PT_VALIDATION
        bDumpOk = DumpLayeredValidationStats();
#endif
        bDumpOk = DumpAOVs(renderLoopWallSeconds) && bDumpOk;
        FinishAOVDump(bDumpOk);

        g_FrameData.pColor = m_pRadiance;
        return;
    }

    if (bPrimaryMediumQueryDirty)
    {
        PrimaryMediumQueryParams queryParams;
        if (!TryBuildPrimaryMediumQueryParams(renderView, queryParams))
            queryParams.sceneBoundsRadius = 0.0f;
        m_SceneBounds = float4(queryParams.sceneBoundsCenter, queryParams.sceneBoundsRadius);

        context.SetRenderPipeline(m_pPrimaryMediumQueryPSO.get());
        context.TransitionBufferToWrite(m_pPrimaryRayMediumSeed, ePipelineStage::ComputeShader);
        context.SetComputeDynamicUniformBuffer("g_PrimaryMediumQueryParams", queryParams);
        context.SetAccelerationStructure("g_Scene", *pTLAS);
        context.StageDescriptor("g_MaterialSlabs", m_pMaterialSlabs);
        context.SetComputeShaderResource("g_PrimaryRayMediumSeedOut", m_pPrimaryRayMediumSeed);
        context.Dispatch(1, 1, 1);
        context.TransitionBufferToRead(m_pPrimaryRayMediumSeed, ePipelineStage::RayTracingShader);

        m_LastPrimaryMediumQueryPosition = renderView.camera.pos;
        m_bHasPrimaryMediumQueryState = true;
    }

    context.SetRenderPipeline(m_pPSO.get());
    context.SetComputeDynamicUniformBuffer("PathSceneBounds", m_SceneBounds);

    context.TransitionTextureToWrite(m_pAccumulation, ePipelineStage::RayTracingShader);
    context.TransitionTextureToWrite(m_pRadiance, ePipelineStage::RayTracingShader);

    u32 samplesThisFrame = m_SamplesPerFrame;
    if (m_bOverrideTargetSamples && m_bDumpRequested.load(std::memory_order_acquire))
        samplesThisFrame = std::min(samplesThisFrame, m_DumpTargetSamples - m_AccumulatedSampleCount);

    struct PathTracerPushConstants
    {
        u32 accumulatedSampleCount;
        u32 samplesPerFrame;
        u32 bResetAccumulation;
        u32 maxDepth;
        float3 environmentRadiance;
        u32    bUseEnvironmentMap;
        u32    bUseEnvironmentSampling;
        u32    environmentDistributionWidth;
        u32    environmentDistributionHeight;
        u32    sobolSeed;
    } constants =
    {
        m_AccumulatedSampleCount,
        samplesThisFrame,
        m_AccumulatedSampleCount == 0 ? 1u : 0u,
        m_MaxDepth,
        m_EnvironmentRadiance,
        m_bUseEnvironmentMap && m_pEnvironmentMap ? 1u : 0u,
        m_bUseEnvironmentSampling && m_pEnvironmentDistribution ? 1u : 0u,
        m_EnvironmentDistributionWidth,
        m_EnvironmentDistributionHeight,
        m_SobolSeed
    };
    static_assert(sizeof(PathTracerPushConstants) == 48);

    context.SetComputeConstants(sizeof(constants), &constants);
    context.SetAccelerationStructure("g_Scene", *pTLAS);
    context.SetComputeShaderResource("g_PrimaryRayMediumSeed", m_pPrimaryRayMediumSeed);
    context.StageDescriptor("g_Accumulation", m_pAccumulation);
    context.StageDescriptor("g_Radiance", m_pRadiance);
#if PT_VALIDATION
    BindValidationResources(context);
#endif // PT_VALIDATION
    context.StageDescriptor("g_EnvironmentMap", m_pEnvironmentMap ? m_pEnvironmentMap : rm.GetFlatBlackTexture(), g_FrameData.pLinearClamp);
    context.StageDescriptor("g_EnvironmentDistribution", m_pEnvironmentDistribution);
    context.StageDescriptor("g_LightSelectionCDF", m_pLightSelectionCDF);

    context.StageDescriptor("g_MaterialSlabs", m_pMaterialSlabs);
    if (m_AccumulatedSampleCount == 0)
        BeginRenderTiming();
    context.DispatchRays(*m_pSBT, m_pRadiance->Width(), m_pRadiance->Height());

#if PT_VALIDATION
    SubmitValidationReadback(context);
    AdvanceValidationReadbackSlot();
#endif

    m_AccumulatedSampleCount += samplesThisFrame;
    m_bHasRendered            = true;
    g_FrameData.pColor        = m_pRadiance;
}

void PathTracerNode::ConfigureReferenceScene(const std::string& sceneName, const float3& environmentRadiance, u32 samplesPerFrame, u32 dumpTargetSamples, u32 maxDepth, const std::string& environmentMapPath)
{
    m_ReferenceSceneName      = sceneName.empty() ? "cornell_box" : sceneName;
    m_EnvironmentRadiance     = environmentRadiance;
    m_EnvironmentMapPath      = environmentMapPath;
    m_pEnvironmentMap.reset();
    ResetEnvironmentDistribution();
    m_bUseEnvironmentMap      = !m_EnvironmentMapPath.empty();
    if (m_bUseEnvironmentMap)
    {
        auto& rm = m_RenderDevice.GetResourceManager();
        m_pEnvironmentMap = rm.LoadTexture(m_EnvironmentMapPath, false, render::eTextureColorSpace::Linear);
        m_bUseEnvironmentMap = m_pEnvironmentMap != nullptr;
        if (m_bUseEnvironmentMap)
            LoadEnvironmentDistribution(m_EnvironmentMapPath);
    }
    m_SamplesPerFrame         = std::max(samplesPerFrame, PATH_TRACER_DEFAULT_SAMPLES_PER_FRAME);
#if PT_VALIDATION
    // Validation gathers aggregate state/estimator statistics rather than a final
    // radiance oracle. Keep its runtime bounded; production retains the authored SPP.
    m_DumpTargetSamples       = PATH_TRACER_DEFAULT_DUMP_TARGET_SAMPLES;
#else
    m_DumpTargetSamples       = std::max(dumpTargetSamples, PATH_TRACER_DEFAULT_DUMP_TARGET_SAMPLES);
#endif
    m_MaxDepth                = std::max(maxDepth, 1u);
    m_AccumulatedSampleCount  = 0;
    ResetRenderTiming();
    m_LastResetDirtyMask      = 0;
    m_bHasRendered            = false;
    m_bHasPrimaryMediumQueryState = false;
#if PT_VALIDATION
    ResetValidationReadbackAggregation();
#endif
    m_bDumpCompleted.store(false, std::memory_order_release);
}

void PathTracerNode::ConfigureAutomation(u32 seed, u32 targetSamples, u32 samplesPerFrame, u32 maxDepth, const std::filesystem::path& outputDir)
{
    m_SobolSeed = seed;
    m_bOverrideTargetSamples = targetSamples != 0;
    if (m_bOverrideTargetSamples)
        m_DumpTargetSamples = targetSamples;
    if (samplesPerFrame != 0)
        m_SamplesPerFrame = samplesPerFrame;
    if (maxDepth != 0)
        m_MaxDepth = maxDepth;
    m_ReferenceOutputDirOverride = outputDir;
}

bool PathTracerNode::RebuildMaterialSlabBuffer(const std::vector< MaterialSlabData >& slabs)
{
    using namespace render;

    const u32 slabCount = std::max(static_cast<u32>(slabs.size()), 1u);
    auto pSlabs = Buffer::Create(
        m_RenderDevice,
        slabs.empty() ? "PathTracer::MaterialSlabsFallback" : "PathTracer::MaterialSlabs",
        {
            .count              = slabCount,
            .elementSizeInBytes = sizeof(MaterialSlabData),
            .mapDirection       = 1,
            .bufferUsage        = eBufferUsage_Storage,
        });
    if (!pSlabs || !pSlabs->MappedMemory())
        return false;

    const size_t dataSize = static_cast<size_t>(slabCount) * sizeof(MaterialSlabData);
    if (slabs.empty())
    {
        MaterialSlabData fallback = {};
        fallback.materialID = kInvalidIndex;
        std::memcpy(pSlabs->MappedMemory(), &fallback, sizeof(fallback));
    }
    else
    {
        std::memcpy(pSlabs->MappedMemory(), slabs.data(), dataSize);
    }

    pSlabs->FlushMappedRange(0, dataSize);
    m_pMaterialSlabs = pSlabs;
    return true;
}

void PathTracerNode::ResetEnvironmentDistribution()
{
    m_bUseEnvironmentSampling = false;
    m_EnvironmentDistributionWidth = 0;
    m_EnvironmentDistributionHeight = 0;
    if (m_pEnvironmentDistribution && m_pEnvironmentDistribution->MappedMemory())
    {
        auto* p = static_cast< f32* >(m_pEnvironmentDistribution->MappedMemory());
        p[0] = 0.0f;
        m_pEnvironmentDistribution->FlushMappedRange(0, sizeof(f32));
    }
}

bool PathTracerNode::LoadEnvironmentDistribution(const std::filesystem::path& environmentMapPath)
{
    namespace fs = std::filesystem;

    fs::path cdfPath = environmentMapPath;
    cdfPath.replace_extension(".envcdf.bin");
    if (!fs::exists(cdfPath))
    {
        printf("[PathTracer] environment distribution not found: %s\n", cdfPath.string().c_str());
        return false;
    }

    std::ifstream in(cdfPath, std::ios::binary);
    if (!in)
        return false;

    EnvironmentDistributionFileHeader header;
    in.read(reinterpret_cast< char* >(&header), sizeof(header));
    if (!in || header.magic != PATH_TRACER_ENV_CDF_MAGIC || header.version != PATH_TRACER_ENV_CDF_VERSION ||
        header.width == 0u || header.height == 0u || header.count != header.height + header.width * header.height)
    {
        printf("[PathTracer] invalid environment distribution: %s\n", cdfPath.string().c_str());
        return false;
    }

    std::vector< f32 > values(header.count);
    in.read(reinterpret_cast< char* >(values.data()), values.size() * sizeof(f32));
    if (!in)
    {
        printf("[PathTracer] truncated environment distribution: %s\n", cdfPath.string().c_str());
        return false;
    }

    auto pDistribution = render::Buffer::Create(
        m_RenderDevice,
        "PathTracer::EnvironmentDistribution",
        {
            .count              = header.count,
            .elementSizeInBytes = sizeof(f32),
            .mapDirection       = 1,
            .bufferUsage        = render::eBufferUsage_Storage,
        });
    if (!pDistribution || !pDistribution->MappedMemory())
        return false;

    std::memcpy(pDistribution->MappedMemory(), values.data(), values.size() * sizeof(f32));
    pDistribution->FlushMappedRange(0, values.size() * sizeof(f32));
    m_pEnvironmentDistribution = pDistribution;
    m_EnvironmentDistributionWidth = header.width;
    m_EnvironmentDistributionHeight = header.height;
    m_bUseEnvironmentSampling = true;
    printf("[PathTracer] loaded environment distribution: %s (%ux%u)\n", cdfPath.string().c_str(), header.width, header.height);
    return true;
}

bool PathTracerNode::UpdateLightSelectionDistribution(const SceneRenderView& renderView)
{
    const LightRenderView& light = renderView.light;
    const u32 lightCount = light.numDirectionals + light.numSpots + light.numAreas +
                           light.numDisks + light.numSpheres + light.numTubes;
    if (lightCount == 0)
    {
        const bool bChanged = !m_LightSelectionCDFData.empty();
        m_LightSelectionCDFData.clear();
        return bChanged;
    }

    PrimaryMediumQueryParams boundsParams;
    if (!TryBuildPrimaryMediumQueryParams(renderView, boundsParams))
        boundsParams.sceneBoundsRadius = PRIMARY_MEDIUM_QUERY_MIN_SCENE_RADIUS;

    std::vector< f32 > weights;
    ComputeLightPowerWeights(light, boundsParams.sceneBoundsRadius, weights);

    float total         = 0.0f;
    bool  bValidWeights = true;
    for (float w : weights)
    {
        if (!std::isfinite(w) || w < 0.0f)
        {
            bValidWeights = false;
            break;
        }
        total += w;
    }
    if (!bValidWeights || !(total > 0.0f))
    {
        weights.assign(lightCount, 1.0f);
        total = (f32)lightCount;
    }

    std::vector< f32 > cdf(lightCount);
    f32 running = 0.0f;
    for (u32 i = 0; i < lightCount; ++i)
    {
        running += weights[i];
        cdf[i] = running / total;
    }
    cdf[lightCount - 1] = 1.0f;

    if (cdf == m_LightSelectionCDFData)
        return false;

    if (!m_pLightSelectionCDF || m_LightSelectionBufferCount < lightCount)
    {
        m_pLightSelectionCDF = render::Buffer::Create(
            m_RenderDevice,
            "PathTracer::LightSelectionCDF",
            {
                .count              = lightCount,
                .elementSizeInBytes = sizeof(f32),
                .mapDirection       = 1,
                .bufferUsage        = render::eBufferUsage_Storage,
            });
        m_LightSelectionBufferCount = lightCount;
    }
    if (!m_pLightSelectionCDF || !m_pLightSelectionCDF->MappedMemory())
        return false;

    std::memcpy(m_pLightSelectionCDF->MappedMemory(), cdf.data(), cdf.size() * sizeof(f32));
    m_pLightSelectionCDF->FlushMappedRange(0, cdf.size() * sizeof(f32));
    m_LightSelectionCDFData = std::move(cdf);
    return true;
}

void PathTracerNode::Resize(u32 width, u32 height, u32 depth)
{
    UNUSED(depth);

    CheckAutomationSceneReset();

    if (m_pAccumulation) m_pAccumulation->Resize(width, height, 1);
    if (m_pRadiance)     m_pRadiance->Resize(width, height, 1);
#if PT_VALIDATION
    ResizeValidationResources(width, height);
#endif // PT_VALIDATION

    m_AccumulatedSampleCount = 0;
    ResetRenderTiming();
    m_LastResetDirtyMask      = 0;
    m_bHasCameraState        = false;
    m_bHasRendered           = false;
#if PT_VALIDATION
    ResetValidationReadbackAggregation();
#endif
    m_bDumpCompleted.store(false, std::memory_order_release);
}

} // namespace baamboo


