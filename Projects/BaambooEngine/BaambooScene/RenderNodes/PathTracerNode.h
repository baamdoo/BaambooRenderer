#pragma once
#include "Defines.h"
#include "RenderCommon/RenderNode.h"
#include "SceneRenderView.h"

#include <array>
#include <atomic>
#include <filesystem>
#include <string>

namespace baamboo
{

class PathTracerNode : public render::RenderNode
{
	using Super = render::RenderNode;
public:
	PathTracerNode(render::RenderDevice& rd);
	virtual ~PathTracerNode() = default;

	virtual void Apply(render::CommandContext& context, const SceneRenderView& renderView) override;
	virtual void DrawUI() override;
	virtual void Resize(u32 width, u32 height, u32 depth = 1) override;

	void RequestAOVDump();
	bool IsAOVDumpComplete() const;

#if PT_VALIDATION
	static constexpr u32 VALIDATION_AOV_COUNT = 15;
	static constexpr u32 VALIDATION_STAT_COUNT = 49;
	static constexpr u32 VALIDATION_READBACK_SLOT_COUNT = kMaxFramesInFlight;
#endif
	void ConfigureReferenceScene(const std::string& sceneName, const float3& environmentRadiance, u32 samplesPerFrame, u32 dumpTargetSamples, u32 maxDepth = 12u, const std::string& environmentMapPath = std::string());

private:
	std::filesystem::path ReferenceOutputDir() const;
	bool DumpAOVs();
	void DumpRenderViewDebug(const SceneRenderView& renderView) const;
#if PT_VALIDATION
	bool DumpLayeredValidationStats() const;
	void PublishValidationReadbackSlot();
	void SubmitValidationReadback(render::CommandContext& context);
	void AdvanceValidationReadbackSlot();
	void ResetValidationReadbackAggregation();
	bool HasPendingValidationReadback() const;
#endif
	void ResetEnvironmentDistribution();
	bool LoadEnvironmentDistribution(const std::filesystem::path& environmentMapPath);
	bool RebuildMaterialSlabBuffer(const std::vector< MaterialSlabData >& slabs);

private:
	Arc< render::Texture > m_pAccumulation;
	Arc< render::Texture > m_pRadiance;
#if PT_VALIDATION
	// Validation AOVs, table-driven: VALIDATION_AOVS
	Arc< render::Texture > m_ValidationAOVs[VALIDATION_AOV_COUNT];
	Arc< render::Buffer >  m_pPathValidationStats;
	Arc< render::Buffer >  m_pPathValidationStatsReadback;
	Arc< render::Buffer >  m_pPrimaryRayMediumSeedReadback;
#endif // PT_VALIDATION
	Arc< render::Texture > m_pEnvironmentMap;
	Arc< render::Buffer >  m_pEnvironmentDistribution;
	Arc< render::Buffer >  m_pMaterialSlabs;
	Arc< render::Buffer >  m_pPrimaryRayMediumSeed;

	Arc< render::ShaderBindingTable > m_pSBT;
	Box< render::ComputePipeline >     m_pPrimaryMediumQueryPSO;
	Box< render::RaytracingPipeline > m_pPSO;

	std::atomic_bool m_bDumpRequested = false;
	std::atomic_bool m_bDumpCompleted = false;
	bool             m_bHasRendered   = false;

#if PT_VALIDATION
	std::array< u64, VALIDATION_STAT_COUNT > m_PathValidationStatTotals = {};
	PrimaryRayMediumStackSeedData m_PrimaryRayMediumSeedDebug = {};
	std::array< u64, VALIDATION_READBACK_SLOT_COUNT > m_ValidationReadbackGenerations = {};
	std::array< bool, VALIDATION_READBACK_SLOT_COUNT > m_ValidationReadbackPending = {};
	u64  m_ValidationReadbackGeneration = 1;
	u64  m_PrimaryRayMediumSeedDebugGeneration = 0;
	u64  m_ValidationReadbackSubmittedFrameCount = 0;
	u64  m_ValidationReadbackPublishedFrameCount = 0;
	u32  m_ValidationReadbackIndex = 0;
	bool m_bHasPrimaryRayMediumSeedDebug = false;
#endif

	u32    m_AccumulatedSampleCount = 0;
	u64    m_LastResetDirtyMask      = 0;
	u32    m_SamplesPerFrame         = 1;
	u32    m_DumpTargetSamples       = 128;
	u32    m_MaxDepth                = 12;
	float3 m_EnvironmentRadiance     = float3(0.0f);
	bool   m_bUseEnvironmentMap     = false;
	bool   m_bUseEnvironmentSampling = false;
	u32    m_EnvironmentDistributionWidth  = 0;
	u32    m_EnvironmentDistributionHeight = 0;
	std::string m_EnvironmentMapPath;
	std::string m_ReferenceSceneName = "cornell_box";
	u64 m_MaterialSlabRevision = ~u64(0);
	bool   m_bHasPrimaryMediumQueryState = false;
	float3 m_LastPrimaryMediumQueryPosition = float3(0.0f);
	bool   m_bHasCameraState        = false;
	std::array< u64, NumComponents > m_LastComponentRevisions = {};
	mat4   m_LastView               = mat4(1.0f);
	mat4   m_LastProj               = mat4(1.0f);
	float2 m_LastViewport           = float2(0.0f);
};

} // namespace baamboo



