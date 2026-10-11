#pragma once
#include "BaambooEngine.h"

#include <atomic>
#include <string>
#include <string_view>
#include <vector>

namespace baamboo
{
class PathTracerNode;
}

class RayTracingApp : public baamboo::Engine
{
using Super = baamboo::Engine;
public:
	inline static constexpr std::string_view s_DefaultPathTracerScene = "usd_shaderball_n2";

	virtual void Initialize(eRendererAPI api) override;
	virtual void Update(float dt) override;
	virtual void Release() override;

	void ConfigurePathTracerAutomation(bool bDumpAOV, bool bExitAfterDump, std::string_view referenceSceneName = s_DefaultPathTracerScene,
		u32 seed = 0, u32 targetSamples = 0, u32 samplesPerFrame = 0, u32 maxDepth = 0, const std::string& outputDir = std::string());
	void ConfigurePathTracerBatch(const std::vector<u32>& seeds, const std::vector<u32>& sampleLevels);
	bool HasPathTracerAutomationFailed() const;

private:
	virtual bool InitWindow() override;
	virtual bool LoadScene() override;
	virtual void DrawUI() override;

	void ConfigureRenderGraph();
	void ConfigureSceneObjects();
	void ConfigureCamera();

	baamboo::CameraController_FirstPerson m_CameraController;
	Weak< baamboo::PathTracerNode > m_pPathTracerNode;

	std::string m_PathTracerReferenceScene = std::string(s_DefaultPathTracerScene);
	u32 m_PathTracerSeed = 0;
	u32 m_PathTracerTargetSamples = 0;
	u32 m_PathTracerSamplesPerFrame = 0;
	u32 m_PathTracerMaxDepth = 0;
	std::string m_PathTracerOutputDir;
	std::vector<u32> m_PathTracerSeeds;
	std::vector<u32> m_PathTracerSampleLevels;
	std::atomic< i32 > m_RequestedPathTracerPreset{ -1 };
	i32 m_PathTracerPresetToApply = -1;
	bool m_bAutoDumpAOV      = false;
	bool m_bExitAfterAOVDump = false;
};
