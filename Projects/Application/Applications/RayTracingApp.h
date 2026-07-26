#pragma once
#include "BaambooEngine.h"

#include <atomic>
#include <string>
#include <string_view>

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

	void ConfigurePathTracerAutomation(bool bDumpAOV, bool bExitAfterDump, std::string_view referenceSceneName = s_DefaultPathTracerScene);

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
	std::atomic< i32 > m_RequestedPathTracerPreset{ -1 };
	i32 m_PathTracerPresetToApply = -1;
	bool m_bAutoDumpAOV      = false;
	bool m_bExitAfterAOVDump = false;
};
