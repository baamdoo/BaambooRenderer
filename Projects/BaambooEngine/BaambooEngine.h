#pragma once
#include "BaambooScene/Scene.h"
#include "BaambooScene/Camera.h"

#include "Timer.h"
#include "ThreadQueue.hpp"
#include "RenderCommon/RendererAPI.h"

#include <deque>

namespace baamboo { class Engine; }
namespace ImGui
{
	void DrawUI(baamboo::Engine& engine);
}

namespace baamboo
{

class Engine
{
public:
	Engine();
	virtual ~Engine();

	virtual void Initialize(eRendererAPI api);
	virtual int  Run();

	[[nodiscard]]
	class Scene* GetScene() const { return m_pScene; }
	[[nodiscard]]
	render::Renderer* GetRenderer() const { return m_pRendererBackend; }
	[[nodiscard]]
	class Window* GetWindow() const { return m_pWindow; }

protected:
	virtual void Release();

	virtual void Update(float dt);
	virtual void GameLoop(float dt);
	virtual void RenderLoop();

	virtual bool InitWindow() { return false; }
	virtual bool LoadScene() { return false; }

	virtual void DrawUI();
	virtual void DrawEntityNode(Entity entity);

	virtual void ApplyScriptBehaviors(float dt);
	virtual void ProcessInput();

protected:
	class Window* m_pWindow = nullptr;
	class Scene*  m_pScene  = nullptr;
	EditorCamera* m_pCamera = nullptr;

	render::Renderer* m_pRendererBackend = nullptr;

	int  m_ResizeWidth    = -1;
	int  m_ResizeHeight   = -1;
	bool m_bWindowResized = false;

	render::DeviceSettings m_DeviceSettings;

private:
	struct PendingResizeRequest
	{
		u32 width = 0;
		u32 height = 0;
		u64 firstProducerSequence = 0;
		bool bPending = false;
	};

	u64    m_ProducerSequence = 0;
	double m_RunningTime = 0.0;
	PendingResizeRequest m_PendingResize;
	bool                 m_bRenderSuspended = false;

	std::thread                    m_RenderThread;
	ThreadQueue< SceneRenderView > m_RenderViewQueue;
	std::atomic_bool               m_bRunning;

	Timer m_GameTimer = {};
	std::atomic< double > m_GameElapsedTime{ 0.0 };

	double m_LastFrameGpuTimeElapsed = 0.0;

	// --- GPU/CPU profile ---
	// Snapshots of the previous frame's profiles (one each for CPU / GPU), updated
	// at the start of each render-thread iteration. Read/written only on the render thread, so no synchronization is needed.
	struct ProfileSnapshotEntry
	{
		std::string name;

		u32    depth          = 0;
		double currentMs      = 0.0;
		double emaMs          = 0.0;   // 0.95/0.05 exponential moving average
		double minMs          = 0.0;   // since last reset
		double maxMs          = 0.0;   // since last reset
		double percentOfFrame = 0.0;   // 0..100 (of top-level "Frame" scope)

		bool bHasStats        = false;
		bool bHasMeshCounters = false; // task/mesh invocations are real
		u64  clippingInvs     = 0;     // triangles submitted to clipping (pre-clip; proxy for meshlet/SW cull effectiveness)
		u64  clippingPrims    = 0;     // triangles that survived clipping (post-clip; rasterized)
		u64  fsInvocations    = 0;     // fragment shader invocations
		u64  meshInvocations  = 0;     // mesh shader invocations (mesh pipeline)
		u64  taskInvocations  = 0;     // task shader invocations (mesh pipeline)

		// Smoothed counter values (EMA 0.95/0.05).
		double clippingInvsEma  = 0.0;
		double clippingPrimsEma = 0.0;
		double fsInvocationsEma = 0.0;
	};
	struct ProfileStats
	{
		double ema   = 0.0;
		double minMs = 0.0; // 0 means "unset" (reset state); first sample seeds
		double maxMs = 0.0;
		bool   bSeeded = false;

		// Counter EMAs (separate from the timing ema above).
		double clipInsEma   = 0.0;
		double clipPrimsEma = 0.0;
		double fsInvocsEma  = 0.0;
	};
	std::vector< ProfileSnapshotEntry >           m_GpuProfileSnapshot;
	std::vector< ProfileSnapshotEntry >           m_CpuProfileSnapshot;
	std::unordered_map< std::string, ProfileStats > m_GpuProfileStatsByName;
	std::unordered_map< std::string, ProfileStats > m_CpuProfileStatsByName;

	// Frame-time history ring buffer for ImGui::PlotLines.
	static constexpr u32 kFrameHistorySize = 200;
	float m_FrameTimeHistory[kFrameHistorySize] = {};
	u32   m_FrameTimeHistoryIdx = 0;

	// --- Frame anomaly capture ---
	// When a frame's GPU time deviates from the smoothed EMA baseline by more than
	// m_AnomalyThresholdPct (%), snapshot the full profile + counters for post-hoc
	// inspection. Ring-buffered to bound memory. Cooldown avoids flooding on drift.
	struct FrameAnomaly
	{
		u64                               frameNum       = 0;
		double                            currentMs      = 0.0;
		double                            baselineMs     = 0.0;  // EMA of Frame scope at capture time
		double                            deltaPct       = 0.0;  // (current - baseline)/baseline * 100
		bool                              bSpike         = true; // true=slower, false=faster
		u32                               cullFlags      = 0;
		u32                               totalInstances = 0;
		u32                               phase1Drawn    = 0;
		u32                               phase2Drawn    = 0;
		std::vector< ProfileSnapshotEntry > gpuProfile;
		std::vector< ProfileSnapshotEntry > cpuProfile;
	};
	static constexpr u32 kMaxAnomalyCaptures    = 16;
	static constexpr u32 kAnomalyWarmupFrames   = 60;  // skip detection until EMA stabilizes
	static constexpr u32 kAnomalyCooldownFrames = 10;  // min gap between consecutive captures

	std::deque< FrameAnomaly > m_AnomalyLog;
	u64                        m_FrameCounter        = 0;
	u64                        m_LastAnomalyFrame    = 0;
	float                      m_AnomalyThresholdPct = 50.0f;
	bool                       m_bAnomalyCapture     = true;

	// mutex for sync between writing entity-components data in render-thread and reading(view-each) in game-thread
	std::mutex m_ImGuiMutex;
	fs::path   m_CurrentDirectory;

	std::vector<fs::directory_entry> m_CachedDirectoryEntries;
	fs::path                         m_CachedBrowserPath;
	friend void ImGui::DrawUI(baamboo::Engine& engine);
};

} // namespace baamboo