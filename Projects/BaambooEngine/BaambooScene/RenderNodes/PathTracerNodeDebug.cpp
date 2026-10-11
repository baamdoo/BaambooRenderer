#include "BaambooPch.h"
#include "PathTracerNode.h"

#include "RenderCommon/CommandContext.h"
#include "RenderCommon/RenderDevice.h"

#include <imgui.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>

namespace baamboo
{
namespace
{
#if PT_VALIDATION
// One row per validation AOV: GPU debug name / PathTracerLIB.hlsl binding /
// EXR filename under engine_aov/. Creation, barriers, descriptor staging,
// resize and dumping all iterate this table — add a row, get all five.
struct ValidationAOVDesc
{
    const char* textureName;
    const char* shaderName;
    const char* fileName;
};

constexpr ValidationAOVDesc VALIDATION_AOVS[] =
{
    { "PathTracer::Albedo",                "g_Albedo",                "albedo.exr" },
    { "PathTracer::Normal",                "g_Normal",                "normal.exr" },
    { "PathTracer::Depth",                 "g_Depth",                 "depth.exr" },
    { "PathTracer::GeometricNormal",       "g_GeometricNormal",       "geometric_normal.exr" },
    { "PathTracer::MaterialParams",        "g_MaterialParams",        "material_params.exr" },
    { "PathTracer::MaterialExtra",         "g_MaterialExtra",         "material_extra.exr" },
    { "PathTracer::MaterialSpecularColor", "g_MaterialSpecularColor", "material_specular_color.exr" },
    { "PathTracer::Emission",              "g_Emission",              "emission.exr" },
    { "PathTracer::DiffuseRadiance",       "g_DiffuseRadiance",       "diffuse_radiance.exr" },
    { "PathTracer::SpecularRadiance",      "g_SpecularRadiance",      "specular_radiance.exr" },
    { "PathTracer::TransmissionRadiance",  "g_TransmissionRadiance",  "transmission_radiance.exr" },
    { "PathTracer::SurfaceLobeMask",       "g_SurfaceLobeMask",       "surface_lobe_mask.exr" },
    { "PathTracer::SurfaceLobeWeight",     "g_SurfaceLobeWeight",     "surface_lobe_weight.exr" },
    { "PathTracer::SampledLobeFrequency",  "g_SampledLobeFrequency",  "sampled_lobe_frequency.exr" },
    { "PathTracer::PrimaryId",             "g_PrimaryId",             "primary_id.exr" },
};

static_assert(sizeof(VALIDATION_AOVS) / sizeof(VALIDATION_AOVS[0]) == PathTracerNode::VALIDATION_AOV_COUNT,
              "VALIDATION_AOVS table must match PathTracerNode::VALIDATION_AOV_COUNT");

constexpr const char* PATH_VALIDATION_STAT_NAMES[] =
{
    "continuation_total", "continuation_zero", "continuation_near_zero",
    "finite_nee_total", "finite_nee_zero", "finite_nee_near_zero",
    "environment_nee_total", "environment_nee_zero", "environment_nee_near_zero",
    "no_continuous_proposal", "single_layer_zero", "nlayer_zero_candidate",
    "nlayer_retry_any_positive", "nlayer_retry_all_zero", "nonfinite", "nlayer_positive",
    "sample_history_0_3", "sample_history_4_7", "sample_history_8_15",
    "sample_history_16_31", "sample_history_32_63", "sample_history_64_127", "sample_history_128_plus",
    "evaluate_history_0_3", "evaluate_history_4_7", "evaluate_history_8_15",
    "evaluate_history_16_31", "evaluate_history_32_63", "evaluate_history_64_127", "evaluate_history_128_plus",
    "marginal_pdf_history_0_3", "marginal_pdf_history_4_7", "marginal_pdf_history_8_15",
    "marginal_pdf_history_16_31", "marginal_pdf_history_32_63",
    "marginal_pdf_history_64_127", "marginal_pdf_history_128_plus",
    "sample_forward_sum", "evaluate_forward_sum", "evaluate_reverse_continuous_sum",
    "evaluate_reverse_delta_sum", "pdf_support_probe_sum", "pdf_forward_sum",
    "pdf_reverse_continuous_sum", "pdf_reverse_delta_sum",
    "sample_max_total", "evaluate_max_total", "pdf_max_total", "counter_overflow",
};

static_assert(sizeof(PATH_VALIDATION_STAT_NAMES) / sizeof(PATH_VALIDATION_STAT_NAMES[0]) == PathTracerNode::VALIDATION_STAT_COUNT,
              "PATH_VALIDATION_STAT_NAMES must match the shader validation-stat contract");
#endif // PT_VALIDATION
} // namespace

#if PT_VALIDATION
void PathTracerNode::InitializeValidationResources()
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

    for (u32 i = 0; i < VALIDATION_AOV_COUNT; ++i)
        m_ValidationAOVs[i] = makeAOV(VALIDATION_AOVS[i].textureName);

    m_pPathValidationStats = Buffer::Create(
        m_RenderDevice,
        "PathTracer::PathValidationStats",
        {
            .count              = VALIDATION_STAT_COUNT,
            .elementSizeInBytes = sizeof(u32),
            .bufferUsage        = eBufferUsage_Storage | eBufferUsage_TransferSource | eBufferUsage_TransferDest,
        });
    m_pPathValidationStatsReadback = Buffer::Create(
        m_RenderDevice,
        "PathTracer::PathValidationStatsReadback",
        {
            .count              = VALIDATION_READBACK_SLOT_COUNT * VALIDATION_STAT_COUNT,
            .elementSizeInBytes = sizeof(u32),
            .mapDirection       = 2,
            .bufferUsage        = eBufferUsage_TransferDest,
        });
    m_pPrimaryRayMediumSeedReadback = Buffer::Create(
        m_RenderDevice,
        "PathTracer::PrimaryRayMediumSeedReadback",
        {
            .count              = VALIDATION_READBACK_SLOT_COUNT,
            .elementSizeInBytes = sizeof(PrimaryRayMediumStackSeedData),
            .mapDirection       = 2,
            .bufferUsage        = eBufferUsage_TransferDest,
        });
}

void PathTracerNode::BindValidationResources(render::CommandContext& context)
{
    using namespace render;
    context.ClearBuffer(m_pPathValidationStats, 0u);
    context.TransitionBufferToWrite(m_pPathValidationStats, ePipelineStage::RayTracingShader);
    for (const auto& pAOV : m_ValidationAOVs)
        context.TransitionTextureToWrite(pAOV, ePipelineStage::RayTracingShader);

    for (u32 i = 0; i < VALIDATION_AOV_COUNT; ++i)
        context.StageDescriptor(VALIDATION_AOVS[i].shaderName, m_ValidationAOVs[i]);
    context.StageDescriptor("g_PathValidationStats", m_pPathValidationStats);
}

void PathTracerNode::ResizeValidationResources(u32 width, u32 height)
{
    for (auto& pAOV : m_ValidationAOVs)
        if (pAOV) pAOV->Resize(width, height, 1);
}
#endif // PT_VALIDATION

void PathTracerNode::RequestAOVDump()
{
    if (m_bAutomationBatch && (IsAOVDumpComplete() || HasAutomationFailed()))
        return;
    m_bDumpCompleted.store(false, std::memory_order_release);
    m_bDumpRequested.store(true, std::memory_order_release);
}

bool PathTracerNode::IsAOVDumpComplete() const
{
    return m_bDumpCompleted.load(std::memory_order_acquire);
}

bool PathTracerNode::HasAutomationFailed() const
{
    return m_bAutomationFailed.load(std::memory_order_acquire);
}

void PathTracerNode::ConfigureAutomationBatch(const std::vector<u32>& seeds, const std::vector<u32>& sampleLevels)
{
    m_bAutomationBatch = true;
    m_BatchSeeds = seeds.empty() ? std::vector<u32>{m_SobolSeed} : seeds;
    m_BatchSampleLevels = sampleLevels.empty() ? std::vector<u32>{m_DumpTargetSamples} : sampleLevels;
    if (m_ReferenceOutputDirOverride.empty())
    {
        FailAutomationBatch("Batch output directory is required");
        return;
    }
    for (size_t i = 0; i < m_BatchSeeds.size(); ++i)
    {
        if (std::find(m_BatchSeeds.begin(), m_BatchSeeds.begin() + i, m_BatchSeeds[i]) != m_BatchSeeds.begin() + i)
        {
            FailAutomationBatch("Batch seeds must be unique");
            return;
        }
    }
    for (size_t i = 0; i < m_BatchSampleLevels.size(); ++i)
    {
        if (m_BatchSampleLevels[i] == 0 || (i > 0 && m_BatchSampleLevels[i] <= m_BatchSampleLevels[i - 1]))
        {
            FailAutomationBatch("Batch sample levels must be positive and strictly increasing");
            return;
        }
    }
    m_BatchSeedIndex = 0;
    m_BatchSampleLevelIndex = 0;
    m_CompletedBatchCheckpoints = 0;
    m_SobolSeed = m_BatchSeeds.front();
    m_DumpTargetSamples = m_BatchSampleLevels.front();
    m_bOverrideTargetSamples = true;
}

void PathTracerNode::FailAutomationBatch(const char* reason)
{
    fprintf(stderr, "[PathTracer] Batch failed at seed %u, target %u: %s\n", m_SobolSeed, m_DumpTargetSamples, reason);
    fflush(stderr);
    m_bAutomationFailed.store(true, std::memory_order_release);
}

bool PathTracerNode::CheckAutomationSceneReset()
{
    if (m_bAutomationBatch && m_CompletedBatchCheckpoints != 0 && !IsAOVDumpComplete())
    {
        FailAutomationBatch("Camera, scene, light distribution, or viewport changed after a completed checkpoint");
        return false;
    }
    return true;
}

bool PathTracerNode::AdvanceAutomationBatch()
{
    if (m_BatchSampleLevelIndex + 1 < m_BatchSampleLevels.size())
    {
        ++m_BatchSampleLevelIndex;
        m_DumpTargetSamples = m_BatchSampleLevels[m_BatchSampleLevelIndex];
        return true;
    }
    if (m_BatchSeedIndex + 1 >= m_BatchSeeds.size())
        return false;

    ++m_BatchSeedIndex;
    m_BatchSampleLevelIndex = 0;
    m_SobolSeed = m_BatchSeeds[m_BatchSeedIndex];
    m_DumpTargetSamples = m_BatchSampleLevels.front();
    m_AccumulatedSampleCount = 0;
    m_LastResetDirtyMask = 0;
    m_bHasRendered = false;
    ResetRenderTiming();
#if PT_VALIDATION
    ResetValidationReadbackAggregation();
#endif
    return true;
}

void PathTracerNode::FinishAOVDump(bool bSucceeded)
{
    if (!bSucceeded)
    {
        if (m_bAutomationBatch)
            FailAutomationBatch("Checkpoint radiance, metadata, or AOV could not be saved");
        return;
    }
    if (m_bAutomationBatch)
    {
        ++m_CompletedBatchCheckpoints;
        if (AdvanceAutomationBatch())
            return;
        if (HasAutomationFailed())
            return;
    }
    m_bDumpRequested.store(false, std::memory_order_release);
    m_bDumpCompleted.store(true, std::memory_order_release);
}

std::filesystem::path PathTracerNode::ReferenceOutputDir() const
{
    if (m_bAutomationBatch)
        return m_ReferenceOutputDirOverride / ("seed_" + std::to_string(m_SobolSeed)) / ("spp_" + std::to_string(m_DumpTargetSamples));
    if (!m_ReferenceOutputDirOverride.empty())
        return m_ReferenceOutputDirOverride;
    return std::filesystem::path("Output") / "References" / "Generated" / m_ReferenceSceneName / "engine_aov";
}

void PathTracerNode::ResetRenderTiming()
{
    m_RenderStartTime = {};
    m_bHasRenderStartTime = false;
}

void PathTracerNode::BeginRenderTiming()
{
    m_RenderStartTime = std::chrono::steady_clock::now();
    m_bHasRenderStartTime = true;
}

bool PathTracerNode::DumpRenderMetadata(f32 wallSeconds, f32 renderLoopWallSeconds) const
{
    std::ofstream out(ReferenceOutputDir() / "render_metadata.json", std::ios::trunc);
    if (!out)
        return false;

    out << std::setprecision(std::numeric_limits<f32>::max_digits10);
    out << "{\n"
        << "  \"version\": 1,\n"
        << "  \"scene\": " << std::quoted(m_ReferenceSceneName) << ",\n"
        << "  \"seed\": " << m_SobolSeed << ",\n"
        << "  \"samples_per_pixel\": " << m_AccumulatedSampleCount << ",\n"
        << "  \"target_samples\": " << m_DumpTargetSamples << ",\n"
        << "  \"samples_per_frame\": " << m_SamplesPerFrame << ",\n"
        << "  \"width\": " << m_pRadiance->Width() << ",\n"
        << "  \"height\": " << m_pRadiance->Height() << ",\n"
        << "  \"max_depth\": " << m_MaxDepth << ",\n"
        << "  \"pt_validation\": " << (PT_VALIDATION ? "true" : "false") << ",\n"
        << "  \"wall_seconds\": " << wallSeconds << ",\n"
        << "  \"render_loop_wall_seconds\": "
        << std::setprecision(std::numeric_limits<f32>::max_digits10) << renderLoopWallSeconds << ",\n"
        << std::setprecision(std::numeric_limits<f32>::max_digits10)
        << "  \"render_loop_samples\": " << m_AccumulatedSampleCount << ",\n"
        << "  \"render_loop_has_prior_checkpoint_io\": "
        << ((m_bAutomationBatch && m_BatchSampleLevelIndex != 0) ? "true" : "false") << ",\n"
        << "  \"render_loop_scope\": \"steady_clock wall from first accumulation dispatch recording to completion of previously submitted GPU work via RenderDevice::Flush, before scene-debug/readback/EXR output; includes frame scheduling, CPU work and all-queue fence wait; excludes scene loading and shutdown; multi-SPP batches include prior checkpoint I/O; not GPU-only\",\n";
    if (m_bAutomationBatch)
    {
        out << "  \"batch_checkpoint_index\": " << m_CompletedBatchCheckpoints << ",\n"
            << "  \"batch_checkpoint_count\": " << m_BatchSeeds.size() * m_BatchSampleLevels.size() << ",\n"
            << "  \"batch_seed_index\": " << m_BatchSeedIndex << ",\n"
            << "  \"batch_spp_index\": " << m_BatchSampleLevelIndex << ",\n"
            << "  \"timing_scope\": \"cumulative steady_clock wall time within the current seed, from first accumulation dispatch through current radiance EXR readback and write; includes earlier checkpoint readback, metadata and AOV writes, frame scheduling, validation drain, and CPU work; excludes scene loading and current subsequent AOV writes; indices are zero-based; not GPU-only time and not a one-shot performance measurement\"\n";
    }
    else
        out << "  \"timing_scope\": \"steady_clock wall time from first accumulation dispatch through synchronous radiance EXR readback and write; excludes scene loading and subsequent AOV writes; includes frame scheduling, validation drain, and CPU work; not GPU-only time\"\n";
    out << "}\n";
    out.flush();
    return out.good();
}

#if PT_VALIDATION
void PathTracerNode::PublishValidationReadbackSlot()
{
    const u32 slot = m_ValidationReadbackIndex;
    if (!m_ValidationReadbackPending[slot])
        return;

    const u64 statsBytes  = VALIDATION_STAT_COUNT * sizeof(u32);
    const u64 statsOffset = slot * statsBytes;
    m_pPathValidationStatsReadback->InvalidateMappedRange(statsOffset, statsBytes);

    const u64 seedBytes  = sizeof(PrimaryRayMediumStackSeedData);
    const u64 seedOffset = slot * seedBytes;
    m_pPrimaryRayMediumSeedReadback->InvalidateMappedRange(seedOffset, seedBytes);

    if (m_ValidationReadbackGenerations[slot] == m_ValidationReadbackGeneration)
    {
        if (const auto* stats = static_cast< const u32* >(m_pPathValidationStatsReadback->MappedMemory()))
        {
            const u32 base = slot * VALIDATION_STAT_COUNT;
            constexpr u32 maxStatBegin = VALIDATION_STAT_COUNT - 4u;
            constexpr u32 maxStatEnd   = VALIDATION_STAT_COUNT - 1u;
            for (u32 i = 0u; i < VALIDATION_STAT_COUNT; ++i)
            {
                const u64 frameValue = stats[base + i];
                if (i >= maxStatBegin && i < maxStatEnd)
                    m_PathValidationStatTotals[i] = std::max(m_PathValidationStatTotals[i], frameValue);
                else
                    m_PathValidationStatTotals[i] += frameValue;
            }
        }

        if (const auto* seeds = static_cast< const PrimaryRayMediumStackSeedData* >(m_pPrimaryRayMediumSeedReadback->MappedMemory()))
        {
            m_PrimaryRayMediumSeedDebug = seeds[slot];
            m_PrimaryRayMediumSeedDebugGeneration = m_ValidationReadbackGeneration;
            m_bHasPrimaryRayMediumSeedDebug = true;
        }
        ++m_ValidationReadbackPublishedFrameCount;
    }

    m_ValidationReadbackPending[slot] = false;
}

void PathTracerNode::SubmitValidationReadback(render::CommandContext& context)
{
    const u32 slot = m_ValidationReadbackIndex;
    const u64 statsBytes  = VALIDATION_STAT_COUNT * sizeof(u32);
    const u64 statsOffset = slot * statsBytes;
    context.TransitionBufferToRead(m_pPathValidationStats, render::ePipelineStage::Copy);
    context.CopyBufferRegion(m_pPathValidationStatsReadback, m_pPathValidationStats, statsBytes, statsOffset, 0u);

    const u64 seedBytes  = sizeof(PrimaryRayMediumStackSeedData);
    const u64 seedOffset = slot * seedBytes;
    context.TransitionBufferToRead(m_pPrimaryRayMediumSeed, render::ePipelineStage::Copy);
    context.CopyBufferRegion(m_pPrimaryRayMediumSeedReadback, m_pPrimaryRayMediumSeed, seedBytes, seedOffset, 0u);
    context.TransitionBufferToRead(m_pPrimaryRayMediumSeed, render::ePipelineStage::RayTracingShader);

    m_ValidationReadbackGenerations[slot] = m_ValidationReadbackGeneration;
    m_ValidationReadbackPending[slot] = true;
    ++m_ValidationReadbackSubmittedFrameCount;
}

void PathTracerNode::AdvanceValidationReadbackSlot()
{
    m_ValidationReadbackIndex = (m_ValidationReadbackIndex + 1u) % VALIDATION_READBACK_SLOT_COUNT;
}

void PathTracerNode::ResetValidationReadbackAggregation()
{
    ++m_ValidationReadbackGeneration;
    m_PathValidationStatTotals.fill(0u);
    m_PrimaryRayMediumSeedDebug = {};
    m_PrimaryRayMediumSeedDebugGeneration = 0u;
    m_ValidationReadbackSubmittedFrameCount = 0u;
    m_ValidationReadbackPublishedFrameCount = 0u;
    m_bHasPrimaryRayMediumSeedDebug = false;
}

bool PathTracerNode::HasPendingValidationReadback() const
{
    for (u32 slot = 0u; slot < VALIDATION_READBACK_SLOT_COUNT; ++slot)
    {
        if (m_ValidationReadbackPending[slot] &&
            m_ValidationReadbackGenerations[slot] == m_ValidationReadbackGeneration)
        {
            return true;
        }
    }
    return false;
}

bool PathTracerNode::DumpLayeredValidationStats() const
{
    namespace fs = std::filesystem;

    std::error_code ec;
    const fs::path dir = ReferenceOutputDir();
    fs::create_directories(dir, ec);

    std::ofstream out(dir / "layered_stats.txt", std::ios::trunc);
    if (!out)
        return false;

    out << "version=2\n";
    out << "samplesPerPixel=" << m_AccumulatedSampleCount << '\n';
    out << "samplesPerFrame=" << m_SamplesPerFrame << '\n';
    out << "submittedFrameCount=" << m_ValidationReadbackSubmittedFrameCount << '\n';
    out << "publishedFrameCount=" << m_ValidationReadbackPublishedFrameCount << '\n';
    out << "width=" << m_pRadiance->Width() << '\n';
    out << "height=" << m_pRadiance->Height() << '\n';
    for (u32 i = 0u; i < VALIDATION_STAT_COUNT; ++i)
        out << "stat." << PATH_VALIDATION_STAT_NAMES[i] << '=' << m_PathValidationStatTotals[i] << '\n';
    return out.good();
}
#endif // PT_VALIDATION

bool PathTracerNode::DumpAOVs(f32 renderLoopWallSeconds)
{
    namespace fs = std::filesystem;

    std::error_code ec;
    const fs::path dir = ReferenceOutputDir();
    fs::create_directories(dir, ec);

    bool bOk = m_RenderDevice.SaveTextureToEXR(m_pRadiance, (dir / "radiance.exr").string().c_str());
    const auto renderEndTime = std::chrono::steady_clock::now();
    if (m_bHasRenderStartTime)
    {
        const f32 wallSeconds = std::chrono::duration<f32>(renderEndTime - m_RenderStartTime).count();
        bOk = DumpRenderMetadata(wallSeconds, renderLoopWallSeconds) && bOk;
    }
    else
        bOk = false;
#if PT_VALIDATION
    // Save every AOV even if an earlier one failed — partial dumps are easier
    // to diagnose than an early-out that hides which files were written.
    for (u32 i = 0; i < VALIDATION_AOV_COUNT; ++i)
        bOk = m_RenderDevice.SaveTextureToEXR(m_ValidationAOVs[i], (dir / VALIDATION_AOVS[i].fileName).string().c_str()) && bOk;
#endif // PT_VALIDATION

    std::error_code removeEc;
    fs::remove(dir / "material_specular.exr", removeEc);

    printf("[PathTracer] AOV dump %s: %s\n", bOk ? "complete" : "failed", fs::absolute(dir, ec).string().c_str());
    return bOk;
}

void PathTracerNode::DumpRenderViewDebug(const SceneRenderView& renderView, u64 sceneDirtyMask) const
{
    namespace fs = std::filesystem;

    std::error_code ec;
    const fs::path dir = ReferenceOutputDir();
    fs::create_directories(dir, ec);

    std::ofstream out(dir / "scene_debug.txt", std::ios::trunc);
    if (!out)
        return;

    out << "producerSequence " << renderView.producerSequence << '\n';
    out << "scene " << m_ReferenceSceneName << '\n';
    out << "mediumIdentityPolicy instance_slot_v1" << '\n';
    out << "viewport " << renderView.viewport.x << ' ' << renderView.viewport.y << '\n';
    out << "accumulatedSamples " << m_AccumulatedSampleCount << '\n';
    out << "sobolSeed " << m_SobolSeed << '\n';
    out << "lastResetDirtyMask " << m_LastResetDirtyMask << '\n';
    out << "pathTracerSceneDirtyMask " << sceneDirtyMask << '\n';
    out << "samplesPerFrame " << m_SamplesPerFrame << '\n';
    out << "dumpTargetSamples " << m_DumpTargetSamples << '\n';
    out << "maxDepth " << m_MaxDepth << '\n';
    out << "environmentRadiance "
        << m_EnvironmentRadiance.x << ' '
        << m_EnvironmentRadiance.y << ' '
        << m_EnvironmentRadiance.z << '\n';
    out << "environmentMap " << (m_bUseEnvironmentMap ? m_EnvironmentMapPath : std::string()) << '\n';
    out << "environmentSampling " << (m_bUseEnvironmentSampling ? 1 : 0) << '\n';
    out << "environmentDistribution " << m_EnvironmentDistributionWidth << ' ' << m_EnvironmentDistributionHeight << '\n';
#if PT_VALIDATION
    const bool bSeedAvailable = m_bHasPrimaryRayMediumSeedDebug &&
        m_PrimaryRayMediumSeedDebugGeneration == m_ValidationReadbackGeneration;
    out << "primaryMediumSeedAvailable " << (bSeedAvailable ? 1 : 0) << '\n';
    out << "primaryMediumSeedStatus " << m_PrimaryRayMediumSeedDebug.status << '\n';
    out << "primaryMediumSeedCount " << m_PrimaryRayMediumSeedDebug.count << '\n';
    out << "primaryMediumSeedAirCount " << m_PrimaryRayMediumSeedDebug.airCount << '\n';
    out << std::setprecision(std::numeric_limits<f32>::max_digits10);
    for (u32 i = 0u; i < kPrimaryRayMediumStackCapacity; ++i)
    {
        const auto& entry = m_PrimaryRayMediumSeedDebug.entries[i];
        out << "primaryMediumSeedEntry[" << i << "]"
            << " boundaryInstanceID=" << entry.boundaryInstanceID
            << " mediumID=" << entry.mediumID
            << " ior=" << entry.ior
            << '\n';
        const auto& airEntry = m_PrimaryRayMediumSeedDebug.airEntries[i];
        out << "primaryMediumSeedAirEntry[" << i << "]"
            << " boundaryInstanceID=" << airEntry.boundaryInstanceID
            << " mediumID=" << airEntry.mediumID
            << " ior=" << airEntry.ior
            << '\n';
    }
    out << std::setprecision(6);
#endif
    out << "meshes " << renderView.meshes.size() << '\n';
    out << "materials " << renderView.materials.size() << '\n';
    out << "draws " << renderView.draws.size() << '\n';
    out << "directionalLights " << renderView.light.numDirectionals << '\n';
    out << "spotLights " << renderView.light.numSpots << '\n';
    out << "materialSlabs " << renderView.materialSlabs.size() << '\n';
    out << "areaLights " << renderView.light.numAreas << '\n';
    out << "diskLights " << renderView.light.numDisks << '\n';
    out << "tubeLights " << renderView.light.numTubes << '\n';
	out << "sphereLights " << renderView.light.numSpheres << '\n';
    out << '\n';

    u32 materialIndex = 0;
    for (const auto& material : renderView.materials)
    {
        out << "material[" << materialIndex++ << "]"
            << " id=" << material.id
            << " tint=" << material.tint.x << ',' << material.tint.y << ',' << material.tint.z
            << " opacity=" << material.opacity
            << " metallic=" << material.metallic
            << " roughness=" << material.roughness
            << " alphaCutoff=" << material.alphaCutoff
            << " ior=" << material.ior
            << " transmission=" << material.transmission
            << " materialType=" << material.materialType
            << " materialFlags=" << material.materialFlags
            << " clearcoat=" << material.clearcoat
            << " clearcoatRoughness=" << material.clearcoatRoughness
            << " anisotropy=" << material.anisotropy
            << " anisotropyRotation=" << material.anisotropyRotation
            << " sheenColor=" << material.sheenColor.x << ',' << material.sheenColor.y << ',' << material.sheenColor.z
            << " sheenRoughness=" << material.sheenRoughness
            << " specularColor=" << material.specularColor.x << ',' << material.specularColor.y << ',' << material.specularColor.z
            << " specularStrength=" << material.specularStrength
            << " emission=" << material.emissionColor.x << ',' << material.emissionColor.y << ',' << material.emissionColor.z
            << " emissivePower=" << material.emissivePower
            << " layerOffset=" << material.layerOffset
            << " layerCount=" << material.layerCount
            << '\n';
    }

    out << '\n';
    for (u32 i = 0; i < renderView.materialSlabs.size(); ++i)
    {
        const auto& slab = renderView.materialSlabs[i];
        out << "materialSlab[" << i << "]"
            << " materialID=" << slab.materialID
            << " sigmaA=" << slab.sigmaA.x << ',' << slab.sigmaA.y << ',' << slab.sigmaA.z
            << " sigmaS=" << slab.sigmaS.x << ',' << slab.sigmaS.y << ',' << slab.sigmaS.z
            << " thickness=" << slab.thickness
            << " phaseG=" << slab.phaseG
            << '\n';
    }

    out << '\n';
    for (u32 i = 0; i < renderView.light.numDirectionals; ++i)
    {
        const auto& light = renderView.light.directionals[i];
        out << "directionalLight[" << i << "]"
            << " direction=" << light.direction.x << ',' << light.direction.y << ',' << light.direction.z
            << " color=" << light.color.x << ',' << light.color.y << ',' << light.color.z
            << " illuminance=" << light.illuminanceLux
            << " angularRadius=" << light.angularRadiusRad
            << '\n';
    }

    for (u32 i = 0; i < renderView.light.numSpots; ++i)
    {
        const auto& light = renderView.light.spots[i];
        out << "spotLight[" << i << "]"
            << " pos=" << light.position.x << ',' << light.position.y << ',' << light.position.z
            << " direction=" << light.direction.x << ',' << light.direction.y << ',' << light.direction.z
            << " color=" << light.color.x << ',' << light.color.y << ',' << light.color.z
            << " flux=" << light.luminousFluxLm
            << " radius=" << light.radiusM
            << " inner=" << light.innerConeAngleRad
            << " outer=" << light.outerConeAngleRad
            << '\n';
    }


    for (u32 i = 0; i < renderView.light.numAreas; ++i)
    {
        const auto& light = renderView.light.areas[i];
        out << "areaLight[" << i << "]"
            << " pos=" << light.position.x << ',' << light.position.y << ',' << light.position.z
            << " half=" << light.halfWidth << ',' << light.halfHeight
            << " normal=" << light.normal.x << ',' << light.normal.y << ',' << light.normal.z
            << " tangent=" << light.tangent.x << ',' << light.tangent.y << ',' << light.tangent.z
            << " color=" << light.color.x << ',' << light.color.y << ',' << light.color.z
            << " flux=" << light.luminousFluxLm
            << '\n';
    }

	for (u32 i = 0; i < renderView.light.numDisks; ++i)
	{
		const auto& light = renderView.light.disks[i];
		out << "diskLight[" << i << "]"
			<< " pos=" << light.position.x << ',' << light.position.y << ',' << light.position.z
			<< " radius=" << light.radius
			<< " normal=" << light.normal.x << ',' << light.normal.y << ',' << light.normal.z
			<< " tangent=" << light.tangent.x << ',' << light.tangent.y << ',' << light.tangent.z
			<< " color=" << light.color.x << ',' << light.color.y << ',' << light.color.z
			<< " flux=" << light.luminousFluxLm
			<< '\n';
	}

    for (u32 i = 0; i < renderView.light.numTubes; ++i)
    {
        const auto& light = renderView.light.tubes[i];
        out << "tubeLight[" << i << "]"
            << " a=" << light.positionA.x << ',' << light.positionA.y << ',' << light.positionA.z
            << " b=" << light.positionB.x << ',' << light.positionB.y << ',' << light.positionB.z
            << " radius=" << light.radius
            << " color=" << light.color.x << ',' << light.color.y << ',' << light.color.z
            << " flux=" << light.luminousFluxLm
            << '\n';
    }


	for (u32 i = 0; i < renderView.light.numSpheres; ++i)
	{
		const auto& light = renderView.light.spheres[i];
		out << "sphereLight[" << i << "]"
			<< " pos=" << light.position.x << ',' << light.position.y << ',' << light.position.z
			<< " radius=" << light.radius
			<< " color=" << light.color.x << ',' << light.color.y << ',' << light.color.z
			<< " flux=" << light.luminousFluxLm
			<< '\n';
	}


    out << '\n';
    u32 instanceIndex = renderView.voxelTerrain.bValid ? kMaxVoxelChunkSlots : 0u;
    for (const auto& [entityId, draw] : renderView.draws)
    {
        if (draw.mesh != kInvalidIndex)
        {
            u32 terminalMaterialID = draw.material;
            if (draw.material < renderView.materials.size())
            {
                const auto& material = renderView.materials[draw.material];
                if (material.layerCount > 1u &&
                    material.layerOffset < renderView.materialSlabs.size() &&
                    material.layerCount <= renderView.materialSlabs.size() - material.layerOffset)
                {
                    terminalMaterialID = renderView.materialSlabs[
                        material.layerOffset + material.layerCount - 1u].materialID;
                }
            }
            out << "mediumBinding[" << instanceIndex << "]"
                << " mediumID=" << (draw.material != kInvalidIndex ? instanceIndex : kInvalidIndex)
                << " terminalMaterialID=" << terminalMaterialID << '\n';
            ++instanceIndex;
        }
        out << "draw[" << entityId << "]"
            << " mesh=" << draw.mesh
            << " transform=" << draw.transform
            << " material=" << draw.material
            << '\n';
    }
}

void PathTracerNode::DrawUI()
{
    if (ImGui::Begin("Path Tracer (AOV)"))
    {
        ImGui::TextUnformatted("PathTracer validation");
        ImGui::Text("Reference scene: %s", m_ReferenceSceneName.c_str());
        ImGui::Text("Accumulated samples: %u", m_AccumulatedSampleCount);
        ImGui::Text("Samples/frame: %u", m_SamplesPerFrame);
        ImGui::Text("Dump target samples: %u", m_DumpTargetSamples);
        ImGui::Text("Max depth: %u", m_MaxDepth);
        ImGui::Text("Environment radiance: %.3f %.3f %.3f", m_EnvironmentRadiance.x, m_EnvironmentRadiance.y, m_EnvironmentRadiance.z);
        ImGui::Text("Environment map: %s", m_bUseEnvironmentMap ? m_EnvironmentMapPath.c_str() : "<none>");
        ImGui::Text("Last reset dirty mask: 0x%llx", static_cast<unsigned long long>(m_LastResetDirtyMask));
        ImGui::Separator();

        if (ImGui::Button("Dump AOV EXRs"))
            RequestAOVDump();

#if PT_VALIDATION
        if (m_bHasPrimaryRayMediumSeedDebug && m_PrimaryRayMediumSeedDebugGeneration == m_ValidationReadbackGeneration)
            ImGui::Text("Camera medium seed: status %u, host %u, air %u",
                m_PrimaryRayMediumSeedDebug.status, m_PrimaryRayMediumSeedDebug.count, m_PrimaryRayMediumSeedDebug.airCount);
        ImGui::TextDisabled("radiance / geometry / material / contribution / lobe AOVs ->");
#else
        ImGui::TextDisabled("radiance only (PT_VALIDATION=0) ->");
#endif
        const std::string outputDir = ReferenceOutputDir().generic_string();
        ImGui::TextDisabled("%s", outputDir.c_str());
        if (m_bDumpRequested.load(std::memory_order_acquire))
            ImGui::TextDisabled("Dump requested; waiting for %u accumulated samples.", m_DumpTargetSamples);
        else if (m_bDumpCompleted.load(std::memory_order_acquire))
            ImGui::TextDisabled("Last dump completed.");
    }
    ImGui::End();
}

} // namespace baamboo
