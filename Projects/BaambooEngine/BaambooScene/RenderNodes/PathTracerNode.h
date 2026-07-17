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
#endif
	void ConfigureReferenceScene(const std::string& sceneName, const float3& environmentRadiance, u32 samplesPerFrame, u32 dumpTargetSamples, u32 maxDepth = 12u, const std::string& environmentMapPath = std::string());

private:
	std::filesystem::path ReferenceOutputDir() const;
	bool DumpAOVs();
	void DumpRenderViewDebug(const SceneRenderView& renderView) const;
	void ResetEnvironmentDistribution();
	bool LoadEnvironmentDistribution(const std::filesystem::path& environmentMapPath);

private:
	Arc< render::Texture > m_pAccumulation;
	Arc< render::Texture > m_pRadiance;
#if PT_VALIDATION
	// Validation AOVs, table-driven: VALIDATION_AOVS
	Arc< render::Texture > m_ValidationAOVs[VALIDATION_AOV_COUNT];
#endif // PT_VALIDATION
	Arc< render::Texture > m_pEnvironmentMap;
	Arc< render::Buffer >  m_pEnvironmentDistribution;

	Arc< render::ShaderBindingTable > m_pSBT;
	Box< render::RaytracingPipeline > m_pPSO;

	std::atomic_bool m_bDumpRequested = false;
	std::atomic_bool m_bDumpCompleted = false;
	bool             m_bHasRendered   = false;

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
	bool   m_bHasCameraState        = false;
	std::array< u64, NumComponents > m_LastComponentRevisions = {};
	mat4   m_LastView               = mat4(1.0f);
	mat4   m_LastProj               = mat4(1.0f);
	float2 m_LastViewport           = float2(0.0f);
};

} // namespace baamboo



