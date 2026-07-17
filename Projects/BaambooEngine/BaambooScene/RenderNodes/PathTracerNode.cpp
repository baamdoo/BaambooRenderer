#include "BaambooPch.h"
#include "PathTracerNode.h"

#include "RenderCommon/CommandContext.h"
#include "RenderCommon/RenderDevice.h"

#include "BaambooScene/Scene.h"

#include <imgui.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <cstring>
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
    (1ULL << eComponentType::CSkyLight)   |
    (1ULL << eComponentType::CAtmosphere) |
    (1ULL << eComponentType::CCloud)      |
    (1ULL << eComponentType::CLocalLight);

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
#endif // PT_VALIDATION

u64 GatherPathTracerDirtyMask(
    const SceneRenderView& renderView,
    const std::array< u64, NumComponents >& lastComponentRevisions)
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
    for (u32 i = 0; i < VALIDATION_AOV_COUNT; ++i)
        m_ValidationAOVs[i] = makeAOV(VALIDATION_AOVS[i].textureName);
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
          .SetMaxPayloadSize(sizeof(float) * 40) // SurfacePayload: 160 bytes; ShadowPayload is smaller.
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

    const bool bCameraChanged =
        !m_bHasCameraState ||
        !BytesEqual(m_LastView, renderView.camera.mView) ||
        !BytesEqual(m_LastProj, renderView.camera.mProj) ||
        !BytesEqual(m_LastViewport, renderView.viewport);
    const u64 sceneDirtyMask = GatherPathTracerDirtyMask(renderView, m_LastComponentRevisions);
    const bool bSceneChanged = sceneDirtyMask != 0;

    if (bCameraChanged || bSceneChanged)
    {
        m_LastView        = renderView.camera.mView;
        m_LastProj        = renderView.camera.mProj;
        m_LastViewport    = renderView.viewport;
        m_bHasCameraState = true;

        m_AccumulatedSampleCount = 0;
        m_LastResetDirtyMask      = sceneDirtyMask;
        m_bHasRendered           = false;
        m_bDumpCompleted.store(false, std::memory_order_release);

        for (u32 component = 0; component < NumComponents; ++component)
        {
            if ((sceneDirtyMask & (1ULL << component)) != 0)
                m_LastComponentRevisions[component] = renderView.componentRevisions[component];
        }
    }

    // Deferred AOV dump: the AOV textures hold the previous completed frame. For
    // progressive rendering, wait until the reference sample target is reached.
    if (m_bDumpRequested.load(std::memory_order_acquire) &&
        m_bHasRendered &&
        m_AccumulatedSampleCount >= m_DumpTargetSamples)
    {
        DumpRenderViewDebug(renderView);
        if (DumpAOVs())
        {
            m_bDumpRequested.store(false, std::memory_order_release);
            m_bDumpCompleted.store(true, std::memory_order_release);
        }
    }

    context.SetRenderPipeline(m_pPSO.get());

    context.TransitionBarrier(m_pAccumulation, eTextureLayout::General);
    context.TransitionBarrier(m_pRadiance, eTextureLayout::General);
#if PT_VALIDATION
    for (const auto& pAOV : m_ValidationAOVs)
        context.TransitionBarrier(pAOV, eTextureLayout::General);
#endif // PT_VALIDATION

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
        u32    padding0;
    } constants =
    {
        m_AccumulatedSampleCount,
        m_SamplesPerFrame,
        m_AccumulatedSampleCount == 0 ? 1u : 0u,
        m_MaxDepth,
        m_EnvironmentRadiance,
        m_bUseEnvironmentMap && m_pEnvironmentMap ? 1u : 0u,
        m_bUseEnvironmentSampling && m_pEnvironmentDistribution ? 1u : 0u,
        m_EnvironmentDistributionWidth,
        m_EnvironmentDistributionHeight,
        0u
    };

    context.SetComputeConstants(sizeof(constants), &constants);
    context.SetAccelerationStructure("g_Scene", *pTLAS);
    context.StageDescriptor("g_Accumulation", m_pAccumulation);
    context.StageDescriptor("g_Radiance", m_pRadiance);
#if PT_VALIDATION
    for (u32 i = 0; i < VALIDATION_AOV_COUNT; ++i)
        context.StageDescriptor(VALIDATION_AOVS[i].shaderName, m_ValidationAOVs[i]);
#endif // PT_VALIDATION
    context.StageDescriptor("g_EnvironmentMap", m_pEnvironmentMap ? m_pEnvironmentMap : rm.GetFlatBlackTexture(), g_FrameData.pLinearClamp);
    context.StageDescriptor("g_EnvironmentDistribution", m_pEnvironmentDistribution);

    context.DispatchRays(*m_pSBT, m_pRadiance->Width(), m_pRadiance->Height());

    m_AccumulatedSampleCount += m_SamplesPerFrame;
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
    m_DumpTargetSamples       = std::max(dumpTargetSamples, PATH_TRACER_DEFAULT_DUMP_TARGET_SAMPLES);
    m_MaxDepth                = std::max(maxDepth, 1u);
    m_AccumulatedSampleCount  = 0;
    m_LastResetDirtyMask      = 0;
    m_bHasRendered            = false;
    m_bDumpCompleted.store(false, std::memory_order_release);
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
    m_pEnvironmentDistribution = pDistribution;
    m_EnvironmentDistributionWidth = header.width;
    m_EnvironmentDistributionHeight = header.height;
    m_bUseEnvironmentSampling = true;
    printf("[PathTracer] loaded environment distribution: %s (%ux%u)\n", cdfPath.string().c_str(), header.width, header.height);
    return true;
}

void PathTracerNode::RequestAOVDump()
{
    m_bDumpCompleted.store(false, std::memory_order_release);
    m_bDumpRequested.store(true, std::memory_order_release);
}

bool PathTracerNode::IsAOVDumpComplete() const
{
    return m_bDumpCompleted.load(std::memory_order_acquire);
}

std::filesystem::path PathTracerNode::ReferenceOutputDir() const
{
    return std::filesystem::path("Output") / "References" / "Generated" / m_ReferenceSceneName / "engine_aov";
}

bool PathTracerNode::DumpAOVs()
{
    namespace fs = std::filesystem;

    std::error_code ec;
    const fs::path dir = ReferenceOutputDir();
    fs::create_directories(dir, ec);

    bool bOk = m_RenderDevice.SaveTextureToEXR(m_pRadiance, (dir / "radiance.exr").string().c_str());
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

void PathTracerNode::DumpRenderViewDebug(const SceneRenderView& renderView) const
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
    out << "viewport " << renderView.viewport.x << ' ' << renderView.viewport.y << '\n';
    out << "accumulatedSamples " << m_AccumulatedSampleCount << '\n';
    out << "lastResetDirtyMask " << m_LastResetDirtyMask << '\n';
    out << "pathTracerSceneDirtyMask " << PATH_TRACER_SCENE_DIRTY_MASK << '\n';
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
    out << "meshes " << renderView.meshes.size() << '\n';
    out << "materials " << renderView.materials.size() << '\n';
    out << "draws " << renderView.draws.size() << '\n';
    out << "directionalLights " << renderView.light.numDirectionals << '\n';
    out << "spotLights " << renderView.light.numSpots << '\n';
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
            << " metallic=" << material.metallic
            << " roughness=" << material.roughness
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
    for (const auto& [entityId, draw] : renderView.draws)
    {
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

void PathTracerNode::Resize(u32 width, u32 height, u32 depth)
{
    UNUSED(depth);

    if (m_pAccumulation) m_pAccumulation->Resize(width, height, 1);
    if (m_pRadiance)     m_pRadiance->Resize(width, height, 1);
#if PT_VALIDATION
    for (auto& pAOV : m_ValidationAOVs)
        if (pAOV) pAOV->Resize(width, height, 1);
#endif // PT_VALIDATION

    m_AccumulatedSampleCount = 0;
    m_LastResetDirtyMask      = 0;
    m_bHasCameraState        = false;
    m_bHasRendered           = false;
    m_bDumpCompleted.store(false, std::memory_order_release);
}

} // namespace baamboo


