#include "BaambooPch.h"
#include "BaambooEngine.h"
#include "BaambooCore/Window.h"
#include "BaambooCore/EngineCore.h"
#include "BaambooCore/Input.hpp"
#include "BaambooScene/Entity.h"
#include "RenderCommon/RenderDevice.h"
#include "RenderCommon/CommandContext.h"
#include "RenderCommon/CpuProfiler.h"
#include "ThreadQueue.hpp"
#include "Utils/Math.hpp"

#include <filesystem>
#include <imgui/imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>
#include <glm/gtc/type_ptr.hpp>
#include <magic_enum/magic_enum.hpp>

namespace ImGui
{

baamboo::Entity SelectedEntity;
baamboo::Entity EntityToCopy;
u32 ContentBrowserSetup = 0;

baamboo::ThreadQueue< baamboo::Entity > EntityDeletionQueue;

ImGuiContext* InitUI()
{
	IMGUI_CHECKVERSION();
	auto pContext = ImGui::CreateContext();
	ImGui::StyleColorsDark();

	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

	ImGuiStyle& style = ImGui::GetStyle();
	style.Colors[ImGuiCol_TitleBg] = ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
	style.Colors[ImGuiCol_TitleBgActive] = ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
	style.Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(1.0f, 0.0f, 0.0f, 0.1f);
	style.Colors[ImGuiCol_MenuBarBg] = ImVec4(1.0f, 0.0f, 0.0f, 0.4f);
	style.Colors[ImGuiCol_Header] = ImVec4(0.8f, 0.0f, 0.0f, 0.4f);
	style.Colors[ImGuiCol_HeaderActive] = ImVec4(1.0f, 0.0f, 0.0f, 0.4f);
	style.Colors[ImGuiCol_HeaderHovered] = ImVec4(1.0f, 0.0f, 0.0f, 0.4f);
	style.Colors[ImGuiCol_FrameBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.8f);
	style.Colors[ImGuiCol_CheckMark] = ImVec4(1.0f, 0.0f, 0.0f, 0.8f);
	style.Colors[ImGuiCol_SliderGrab] = ImVec4(1.0f, 0.0f, 0.0f, 0.4f);
	style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(1.0f, 0.0f, 0.0f, 0.8f);
	style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(1.0f, 1.0f, 1.0f, 0.1f);
	style.Colors[ImGuiCol_FrameBgActive] = ImVec4(1.0f, 1.0f, 1.0f, 0.2f);
	style.Colors[ImGuiCol_Button] = ImVec4(1.0f, 0.0f, 0.0f, 0.4f);
	style.Colors[ImGuiCol_ButtonHovered] = ImVec4(1.0f, 0.0f, 0.0f, 0.6f);
	style.Colors[ImGuiCol_ButtonActive] = ImVec4(1.0f, 0.0f, 0.0f, 0.8f);

	// subsequent initialization will be executed by window and renderer-backend

	return pContext;
}

void DrawUI(baamboo::Engine& engine)
{
	if (!engine.GetRenderer() || !engine.GetWindow())
		return;

	engine.GetRenderer()->NewFrame();
	engine.GetWindow()->NewFrame();
	ImGui::NewFrame();

	engine.DrawUI();
}

void Destroy()
{
	EntityToCopy.Reset();
	SelectedEntity.Reset();
	ContentBrowserSetup = 0;

	if (ImGui::GetCurrentContext())
		ImGui::DestroyContext();
}

} // namespace ImGui

namespace baamboo
{

enum
{
	eContentButton_None,
	eContentButton_Mesh,
	eContentButton_Albedo,
	eContentButton_Normal,
	eContentButton_Ao,
	eContentButton_Metallic,
	eContentButton_Roughness,
	eContentButton_Emission,
	eContentButton_Clearcoat,
	eContentButton_Anisotropy,
	eContentButton_Sheen,

	eContentButton_Skybox,
};

constexpr u32 NUM_TOLERANCE_ASYNC_FRAME_GAME_TO_RENDER = 3;

Engine::Engine()
	: m_CurrentDirectory(ASSET_PATH)
	, m_bRunning(false)
{
}

Engine::~Engine()
{
	Release();
}

void Engine::Initialize(eRendererAPI eApi)
{
	s_RendererAPI = eApi;

	auto pImGuiContext = ImGui::InitUI();
	if (!InitWindow())
		throw std::runtime_error("Failed to initialize window!");
	if (!LoadRenderer(s_RendererAPI, m_pWindow, m_DeviceSettings, pImGuiContext, &m_pRendererBackend))
		throw std::runtime_error("Failed to load backend!");
	if (!LoadScene())
		throw std::runtime_error("Failed to create scene!");

	auto pDevice = m_pRendererBackend->GetDevice();
	assert(pDevice);

	g_FrameData.componentMarker = 0;

	g_FrameData.pPointClamp    = render::Sampler::CreatePointClamp(*pDevice);
	g_FrameData.pPointWrap     = render::Sampler::CreatePointRepeat(*pDevice);
	g_FrameData.pLinearClamp   = render::Sampler::CreateLinearClamp(*pDevice);
	g_FrameData.pLinearWrap    = render::Sampler::CreateLinearRepeat(*pDevice);
	g_FrameData.pPointClampMin  = render::Sampler::CreatePointClampMin(*pDevice);
	g_FrameData.pLinearClampMin = render::Sampler::CreateLinearClampMin(*pDevice);
}

i32 Engine::Run()
{
	m_bRunning = true;
	m_RunningTime = 0.0;

	m_RenderThread = std::thread(&Engine::RenderLoop, this);
	while (m_pWindow->PollEvent())
	{
		m_GameTimer.Tick();

		auto dt        = m_GameTimer.GetDeltaSeconds();
		m_RunningTime += dt;

		if (m_GameTimer.GetTotalSeconds() > 1.0)
		{
			m_GameElapsedTime.store(m_GameTimer.GetDeltaMilliseconds(), std::memory_order_relaxed);

			m_GameTimer.Reset();
		}

		Update(static_cast<float>(dt));

		Input::Inst()->EndFrame();
	}

	m_bRunning = false;
	return 0;
}

void Engine::Update(float dt)
{
	if (m_bWindowResized && m_ResizeWidth >= 0 && m_ResizeHeight >= 0)
	{
		if (m_ResizeWidth == 0 || m_ResizeHeight == 0)
			return;

		m_pWindow->OnWindowResized(m_ResizeWidth, m_ResizeHeight);
		m_pScene->OnWindowResized(m_ResizeWidth, m_ResizeHeight);
		m_pRendererBackend->Resize(m_ResizeWidth, m_ResizeHeight);

		m_bWindowResized = false;
		m_ResizeWidth    = m_ResizeHeight = -1;

		m_Frame = 0;
	}

	GameLoop(dt);
}

void Engine::Release()
{
	Input::Inst()->Reset();

	m_RenderViewQueue.clear();
	if (m_RenderThread.joinable())
	{
		m_RenderThread.join();
	}
	m_pRendererBackend->WaitIdle();

	RELEASE(m_pCamera);
	RELEASE(m_pScene);
	RELEASE(m_pWindow);

	g_FrameData.pLinearClampMin.reset();
	g_FrameData.pPointClampMin.reset();
	g_FrameData.pLinearWrap.reset();
	g_FrameData.pLinearClamp.reset();
	g_FrameData.pPointClamp.reset();
	g_FrameData.pPointWrap.reset();
	RELEASE(m_pRendererBackend);

	ImGui::Destroy();
}

void Engine::ApplyScriptBehaviors(float dt)
{
	if (m_pScene == nullptr)
		return;

	m_pScene->Registry().view< TransformComponent, ScriptComponent >().each([this, dt](auto id, auto& transformComponent, auto& scriptComponent)
		{
			if (scriptComponent.bMove)
			{
				transformComponent.transform.position += scriptComponent.moveVelocity * dt;
				m_pScene->Registry().patch< TransformComponent >(id, [](auto&) {});
			}

			if (scriptComponent.bRotate)
			{
				transformComponent.transform.Rotate(scriptComponent.rotationVelocity.y * dt, scriptComponent.rotationVelocity.x * dt, scriptComponent.rotationVelocity.z * dt);
				m_pScene->Registry().patch< TransformComponent >(id, [](auto&) {});
			}
		});
}

void Engine::GameLoop(float dt)
{
	ProcessInput();

	if (m_pScene == nullptr)
		return;

	std::lock_guard< std::mutex > lock(m_ImGuiMutex);

	ApplyScriptBehaviors(dt);
	m_pScene->Update(dt, *m_pCamera);

	auto renderView = m_pScene->RenderView(*m_pCamera, float2(m_pWindow->Width(), m_pWindow->Height()), m_Frame, m_pRendererBackend->GetDevice()->GetDeviceSettings());
	if (m_RenderViewQueue.size() >= NUM_TOLERANCE_ASYNC_FRAME_GAME_TO_RENDER)
		m_RenderViewQueue.replace(std::move(renderView));
	else
		m_RenderViewQueue.push(std::move(renderView));
}

void Engine::RenderLoop()
{
	while (m_bRunning)
	{
		if (!ImGui::EntityDeletionQueue.empty())
		{
			std::lock_guard< std::mutex > lock(m_ImGuiMutex);

			auto entity = ImGui::EntityDeletionQueue.try_pop();
			m_pScene->RemoveEntity(entity.value());

			m_RenderViewQueue.clear();
		}

		auto renderViewOptional = m_RenderViewQueue.try_pop();
		if (!renderViewOptional.has_value())
			continue;

		m_RenderTimer.Tick();

		// Render
		const auto& renderView = renderViewOptional.value();
		{
			auto& rm = m_pRendererBackend->GetDevice()->GetResourceManager();

			render::CpuProfiler::Thread().BeginFrame(); // Must match the GPU "Frame" scope.

			auto pContext = m_pRendererBackend->BeginFrame();
			if (pContext)
			{
				rm.GetSceneResource().UpdateSceneResources(renderView, *pContext);
				rm.GetSceneResource().BindSceneResources(*pContext);

				m_LastFrameGpuTimeElapsed = pContext->GetLastFrameElapsedTime();

				// --- Update GPU/CPU profile snapshots + stats ---
				auto updateSnapshot = [](
					const auto&                                     entries,     // vector of Gpu/CpuProfileEntry
					std::vector< ProfileSnapshotEntry >&            snapshot,
					std::unordered_map< std::string, ProfileStats >& statsByName)
				{
					snapshot.clear();
					snapshot.reserve(entries.size());

					// Frame total = top-level scope (index 0 by construction). Used for % share.
					const double frameTotalMs = entries.empty() ? 0.0 : entries[0].elapsedMs;

					for (const auto& e : entries)
					{
						const char*   rawName = e.name ? e.name : "(null)";
						ProfileStats& s       = statsByName[rawName];

						// EMA: ~20-frame moving average.
						s.ema = 0.95 * s.ema + 0.05 * e.elapsedMs;

						// Min/Max since last reset. First sample seeds both.
						if (!s.bSeeded)
						{
							s.minMs   = e.elapsedMs;
							s.maxMs   = e.elapsedMs;
							s.bSeeded = true;
						}
						else
						{
							if (e.elapsedMs < s.minMs) s.minMs = e.elapsedMs;
							if (e.elapsedMs > s.maxMs) s.maxMs = e.elapsedMs;
						}

						const double pct = (frameTotalMs > 1e-9)
							? (e.elapsedMs / frameTotalMs * 100.0)
							: 0.0;

						ProfileSnapshotEntry snap = {
							.name           = rawName,
							.depth          = e.depth,
							.currentMs      = e.elapsedMs,
							.emaMs          = s.ema,
							.minMs          = s.minMs,
							.maxMs          = s.maxMs,
							.percentOfFrame = pct,
						};
						if constexpr (requires { e.bHasStats; e.stats; })
						{
							if (e.bHasStats)
							{
								snap.bHasStats        = true;
								snap.bHasMeshCounters = e.stats.bHasMeshCounters;
								snap.clippingInvs     = e.stats.clippingInvocations;
								snap.clippingPrims    = e.stats.clippingPrimitives;
								snap.fsInvocations    = e.stats.fsInvocations;
								snap.meshInvocations  = e.stats.meshInvocations;
								snap.taskInvocations  = e.stats.taskInvocations;

								s.clipInsEma   = 0.95 * s.clipInsEma   + 0.05 * double(e.stats.clippingInvocations);
								s.clipPrimsEma = 0.95 * s.clipPrimsEma + 0.05 * double(e.stats.clippingPrimitives);
								s.fsInvocsEma  = 0.95 * s.fsInvocsEma  + 0.05 * double(e.stats.fsInvocations);
								snap.clippingInvsEma  = s.clipInsEma;
								snap.clippingPrimsEma = s.clipPrimsEma;
								snap.fsInvocationsEma = s.fsInvocsEma;
							}
						}
						snapshot.push_back(std::move(snap));
					}
				};

				updateSnapshot(pContext->GetLastFrameProfile(),
				               m_GpuProfileSnapshot,
				               m_GpuProfileStatsByName);

				updateSnapshot(render::CpuProfiler::Thread().GetLastFrameProfile(),
				               m_CpuProfileSnapshot,
				               m_CpuProfileStatsByName);

				// Push this frame's GPU total (implicit "Frame" scope) to the ring-buffer.
				const float frameTotalMs = m_GpuProfileSnapshot.empty()
					? 0.0f
					: float(m_GpuProfileSnapshot[0].currentMs);
				m_FrameTimeHistory[m_FrameTimeHistoryIdx] = frameTotalMs;
				m_FrameTimeHistoryIdx = (m_FrameTimeHistoryIdx + 1) % FRAME_HISTORY_SIZE;

				// --- Frame anomaly detection ---
				// Compare current Frame time against its EMA baseline; on large deviation,
				// snapshot the full profile for post-hoc inspection.
				++m_FrameCounter;
				if (m_bAnomalyCapture
				    && !m_GpuProfileSnapshot.empty()
				    && m_FrameCounter > ANOMALY_WARMUP_FRAMES
				    && m_FrameCounter - m_LastAnomalyFrame > ANOMALY_COOLDOWN_FRAMES)
				{
					const auto& frameEntry  = m_GpuProfileSnapshot[0];
					const double baselineMs = frameEntry.emaMs;
					const double currentMs  = frameEntry.currentMs;
					if (baselineMs > 0.1) // avoid div-near-zero
					{
						const double deltaPct = (currentMs - baselineMs) / baselineMs * 100.0;
						if (std::abs(deltaPct) >= double(m_AnomalyThresholdPct))
						{
							FrameAnomaly a;
							a.frameNum       = m_FrameCounter;
							a.currentMs      = currentMs;
							a.baselineMs     = baselineMs;
							a.deltaPct       = deltaPct;
							a.bSpike         = deltaPct > 0.0;
							a.cullFlags      = g_FrameData.cullFlags;
							a.totalInstances = g_FrameData.totalInstances;
							a.phase1Drawn    = g_FrameData.phase1InstanceDrawCount;
							a.phase2Drawn    = g_FrameData.phase2InstanceDrawCount;
							a.gpuProfile     = m_GpuProfileSnapshot;  // deep copy
							a.cpuProfile     = m_CpuProfileSnapshot;
							m_AnomalyLog.push_back(std::move(a));
							if (m_AnomalyLog.size() > MAX_ANOMALY_CAPTURES)
								m_AnomalyLog.pop_front();
							m_LastAnomalyFrame = m_FrameCounter;
						}
					}
				}

				if (renderView.pEntityDirtyMarks)
				{
					assert(renderView.pSceneMutex);
					std::lock_guard< std::mutex > lock(*renderView.pSceneMutex);

					g_FrameData.componentMarker |= (*renderView.pEntityDirtyMarks)[renderView.cloud.id] & (1 << eComponentType::CCloud);
					g_FrameData.componentMarker |= (*renderView.pEntityDirtyMarks)[renderView.atmosphere.id] & (1 << eComponentType::CAtmosphere);
					// .. process other markers if needed
				}

				if (m_pRendererBackend->GetDevice()->GetDeviceSettings().bDrawUI)
				{
					assert(renderView.pSceneMutex);
					std::lock_guard< std::mutex > lock(*renderView.pSceneMutex);

					ImGui::DrawUI(*this);
					for (auto pNode : m_pScene->GetRenderNodes())
						if (pNode && pNode->IsEnabled())
							pNode->DrawUI();

					ImGui::Render();
				}

				for (auto pNode : m_pScene->GetRenderNodes())
				{
					if (pNode)
					{
						BAAMBOO_PROFILE_SCOPE(*pContext, pNode->GetName().c_str());
						pNode->Apply(*pContext, renderView);
					}
				}

				assert(g_FrameData.pColor);

				m_pRendererBackend->EndFrame(std::move(pContext), g_FrameData.pColor.lock(), m_pRendererBackend->GetDevice()->GetDeviceSettings().bDrawUI);

				m_Frame++;
				g_FrameData.componentMarker = 0;
				if (renderView.pEntityDirtyMarks)
				{
					std::lock_guard< std::mutex > lock(*renderView.pSceneMutex);
					for (auto& mark : (*renderView.pEntityDirtyMarks)) 
						mark.second = 0;
				}
			}

			// Close the CPU-side "Frame" scope.
			render::CpuProfiler::Thread().EndFrame();

			/*if (!m_GpuProfileSnapshot.empty() && m_pRendererBackend)
				m_pRendererBackend->RecordFrameTime(m_GpuProfileSnapshot[0].currentMs);*/
		}
	}
}

void Engine::DrawUI()
{
	// **
	// engine stats
	// **
	ImGui::Begin("Engine Stats");
		static double gameElapsedAcc_ms = 0.0;
		gameElapsedAcc_ms += m_GameElapsedTime.load(std::memory_order_relaxed);
		static double gameElapsed_ms = m_GameElapsedTime.load(std::memory_order_relaxed);
		if (gameElapsedAcc_ms > 1000.0)
		{
			gameElapsed_ms = m_GameElapsedTime.load(std::memory_order_relaxed);
		}
		ImGui::Text("GameLoop   %.3f ms(frame: %.1f FPS)", gameElapsed_ms, 1000.0f / gameElapsed_ms);

		static double renderElapsedCpu_ms = m_RenderTimer.GetDeltaMilliseconds();
		static double renderElapsedGpu_ms = m_LastFrameGpuTimeElapsed * 1e-6;
		if (m_RenderTimer.GetTotalSeconds() > 1.0f)
		{
			renderElapsedCpu_ms = m_RenderTimer.GetDeltaMilliseconds();
			renderElapsedGpu_ms = m_LastFrameGpuTimeElapsed * 1e-6;

			m_RenderTimer.Reset();
		}
		ImGui::Text("RenderLoop(CPU) %.3f ms(frame: %.1f FPS)", renderElapsedCpu_ms, 1000.0f / renderElapsedCpu_ms);
		ImGui::Text("RenderLoop(GPU) %.3f ms(frame: %.1f FPS)", renderElapsedGpu_ms, 1000.0f / renderElapsedGpu_ms);

		// --- GPU Frame Time History Plot ---
		ImGui::Separator();
		{
			float histMax = 0.0f;
			for (u32 i = 0; i < FRAME_HISTORY_SIZE; ++i)
				histMax = std::max(histMax, m_FrameTimeHistory[i]);
			histMax = std::max(histMax * 1.15f, 1.0f); // 15% headroom, min 1ms

			char overlay[32];
			snprintf(overlay, sizeof(overlay), "max %.2f ms", histMax);
			ImGui::PlotLines(
				"##gpu_frame_time",
				m_FrameTimeHistory,
				int(FRAME_HISTORY_SIZE),
				int(m_FrameTimeHistoryIdx),
				overlay,
				0.0f, histMax,
				ImVec2(0.0f, 60.0f));
		}

		if (m_GpuProfileSnapshot.size() > 1)
		{
			ImGui::Separator();
			ImGui::TextDisabled("Frame breakdown (top-level passes)");

			const double frameMs = m_GpuProfileSnapshot[0].currentMs;
			struct Slice { const char* name; double ms; u32 color; };
			std::vector< Slice > slices;
			slices.reserve(m_GpuProfileSnapshot.size());

			double accounted = 0.0;
			for (const auto& e : m_GpuProfileSnapshot)
			{
				if (e.depth != 1) continue; // top-level only
				slices.push_back({ e.name.c_str(), e.currentMs, render::GetGpuMarkerColor(e.name.c_str()) });
				accounted += e.currentMs;
			}
			const double residualMs = std::max(0.0, frameMs - accounted);

			// Chart area + legend: pie on the left, colored labels on the right.
			const float pieRadius = 45.0f;
			const ImVec2 cursor   = ImGui::GetCursorScreenPos();
			const ImVec2 center   = { cursor.x + pieRadius + 4.0f, cursor.y + pieRadius + 4.0f };

			ImDrawList* dl = ImGui::GetWindowDrawList();
			if (frameMs > 1e-6)
			{
				constexpr float TAU = 6.2831853f;
				float a0 = -TAU * 0.25f; // start at 12 o'clock
				auto addWedge = [&](double frac, u32 color)
				{
					if (frac <= 0.0) return;
					const float a1 = a0 + float(frac) * TAU;
					const int   seg = std::max(4, int(frac * 48.0));
					dl->PathLineTo(center);
					dl->PathArcTo(center, pieRadius, a0, a1, seg);
					dl->PathFillConvex(color);
					a0 = a1;
				};
				for (const auto& s : slices) addWedge(s.ms / frameMs, s.color);
				addWedge(residualMs / frameMs, 0xFF555555u); // "other" in dark gray
				dl->AddCircle(center, pieRadius + 0.5f, 0xFF000000u, 48, 1.0f);
			}

			// Reserve vertical space for the pie so ImGui advances cursor.
			ImGui::Dummy(ImVec2(pieRadius * 2.0f + 8.0f, pieRadius * 2.0f + 8.0f));

			// Legend
			ImGui::SameLine();
			ImGui::BeginGroup();
			auto legendRow = [&](const char* label, double ms, u32 color)
			{
				const ImVec2 p = ImGui::GetCursorScreenPos();
				const float  sz = ImGui::GetTextLineHeight();
				dl->AddRectFilled(p, { p.x + sz, p.y + sz }, color);
				dl->AddRect(p, { p.x + sz, p.y + sz }, 0xFF000000u);
				ImGui::Dummy(ImVec2(sz + 4.0f, sz));
				ImGui::SameLine();
				const double frac = frameMs > 1e-6 ? ms / frameMs * 100.0 : 0.0;
				ImGui::Text("%s  %.2f ms (%.1f%%)", label, ms, frac);
			};
			for (const auto& s : slices) legendRow(s.name, s.ms, s.color);
			if (residualMs > 0.001) legendRow("other", residualMs, 0xFF555555u);
			ImGui::EndGroup();
		}

		// --- GPU / CPU Profile Trees ---
		auto drawProfileTable = [](
			const char*                                       tableId,
			const std::vector< ProfileSnapshotEntry >&         snapshot,
			std::unordered_map< std::string, ProfileStats >&  stats)
		{
			// Reset button clears min/max (ema survives so the moving avg doesn't jump).
			if (ImGui::SmallButton((std::string("Reset Min/Max##minmax") + tableId).c_str()))
			{
				for (auto& kv : stats)
				{
					kv.second.minMs   = 0.0;
					kv.second.maxMs   = 0.0;
					kv.second.bSeeded = false;
				}
			}
			ImGui::SameLine();
			if (ImGui::SmallButton((std::string("Reset EMA##ema") + tableId).c_str()))
			{
				for (auto& kv : stats)
				{
					kv.second.ema          = 0.0;
					kv.second.clipInsEma   = 0.0;
					kv.second.clipPrimsEma = 0.0;
					kv.second.fsInvocsEma  = 0.0;
				}
			}

			if (snapshot.empty())
			{
				ImGui::TextDisabled("(no data yet)");
				return;
			}

			const int numCols = 6;
			if (ImGui::BeginTable(tableId, numCols,
				ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollX | ImGuiTableFlags_Resizable))
			{
				ImGui::TableSetupColumn("Pass", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoClip, 200.0f);
				ImGui::TableSetupColumn("ms", ImGuiTableColumnFlags_WidthFixed, 65.0f);
				ImGui::TableSetupColumn("ema", ImGuiTableColumnFlags_WidthFixed, 65.0f);
				ImGui::TableSetupColumn("%", ImGuiTableColumnFlags_WidthFixed, 50.0f);
				ImGui::TableSetupColumn("min", ImGuiTableColumnFlags_WidthFixed, 65.0f);
				ImGui::TableSetupColumn("max", ImGuiTableColumnFlags_WidthFixed, 65.0f);
				ImGui::TableHeadersRow();

				auto formatCount = [](u64 v, char* buf, size_t n)
				{
					if (v >= 1'000'000ULL) snprintf(buf, n, "%.2fM", double(v) * 1e-6);
					else if (v >= 1'000ULL) snprintf(buf, n, "%.1fK", double(v) * 1e-3);
					else snprintf(buf, n, "%llu", (unsigned long long)v);
				};

				for (const auto& e : snapshot)
				{
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::Text("%*s%s", int(e.depth) * 2, "", e.name.c_str());
					ImGui::TableSetColumnIndex(1);
					ImGui::Text("%6.3f", e.currentMs);
					ImGui::TableSetColumnIndex(2);
					ImGui::Text("%6.3f", e.emaMs);
					ImGui::TableSetColumnIndex(3);
					ImGui::Text("%5.1f", e.percentOfFrame);
					ImGui::TableSetColumnIndex(4);
					ImGui::Text("%6.3f", e.minMs);
					ImGui::TableSetColumnIndex(5);
					ImGui::Text("%6.3f", e.maxMs);
				}
				ImGui::EndTable();
			}
		};

		ImGui::Separator();
		if (ImGui::CollapsingHeader("GPU Profile", ImGuiTreeNodeFlags_DefaultOpen))
		{
			drawProfileTable("##gpu_profile", m_GpuProfileSnapshot, m_GpuProfileStatsByName);
		}

		/*if (ImGui::CollapsingHeader("CPU Profile"))
		{
			drawProfileTable("##cpu_profile", m_CpuProfileSnapshot, m_CpuProfileStatsByName);
		}*/

		// --- Culling Controls [TEMP] ---
		ImGui::Separator();
		if (ImGui::CollapsingHeader("Culling Controls", ImGuiTreeNodeFlags_DefaultOpen))
		{
			auto toggleBit = [](const char* label, u32 bit)
			{
				bool on = (g_FrameData.cullFlags & bit) != 0;
				if (ImGui::Checkbox(label, &on))
				{
					if (on) g_FrameData.cullFlags |=  bit;
					else    g_FrameData.cullFlags &= ~bit;
				}
			};
			ImGui::TextDisabled("Per-triangle (mesh shader)");
			toggleBit("Backface##cull0", 0x1);
			ImGui::SameLine();
			toggleBit("Subpixel##cull1", 0x2);

			ImGui::TextDisabled("Per-meshlet (task shader)");
			toggleBit("Frustum##cull2", 0x4);
			ImGui::SameLine();
			toggleBit("Cone##cull3", 0x8);
			ImGui::SameLine();
			toggleBit("Occlusion##cull4", 0x10);

			ImGui::TextDisabled("LOD selection (SSE)");
			ImGui::SliderFloat("SSE Threshold (px)##lodSse", &g_FrameData.sseThresholdPx, 0.1f, 4.0f, "%.2f");
		}

		// --- Pipeline Summary ---
		ImGui::Separator();
		if (ImGui::CollapsingHeader("Pipeline Summary", ImGuiTreeNodeFlags_DefaultOpen))
		{
			// =====================================================================
			// Per-level cull pipeline. Each row is one stage of the GPU cull funnel:
			//
			//   Instance  — InstanceCullingCS: frustum + HZB cull on whole models.
			//               OUTPUT count = phase1+phase2 instance-cull survivors.
			//               When meshlet-occlusion is ON the same instance is
			//               re-emitted in BOTH phases, so OUTPUT can exceed INPUT
			//               by design — the "Cull %" cell hides the meaningless
			//               number in that mode.
			//   Meshlet   — task shader: frustum + cone + HZB + Phase 1 visibility
			//               persistence. INPUT/OUTPUT come from atomic counters
			//               written by the task shader.
			//   Triangle  — mesh-shader triangle cull (backface + subpixel).
			//               INPUT  = sum of meshlet.triangleCount for drawn meshlets
			//                        (= triangleCandidates atomic counter).
			//               OUTPUT = clippingInvocations from pipeline statistics
			//                        (= what the mesh shader actually emitted).
			//   Clip (HW) — hardware clipping/subpixel rejection.
			//               INPUT  = clippingInvocations (mesh shader output).
			//               OUTPUT = clippingPrimitives (rasterizer fed primitives).
			// =====================================================================
			const u32 totalInst  = g_FrameData.totalInstances;
			const u32 phase1Inst = g_FrameData.phase1InstanceDrawCount;
			const u32 phase2Inst = g_FrameData.phase2InstanceDrawCount;
			const u32 drawnInst  = phase1Inst + phase2Inst;

#if PROFILING_LEVEL >= 1
			const u32 phase1mTotal  = g_FrameData.phase1MeshletTotal;
			const u32 phase2mTotal  = g_FrameData.phase2MeshletTotal;
			const u32 phase1mDrawn  = g_FrameData.phase1MeshletDrawn;
			const u32 phase2mDrawn  = g_FrameData.phase2MeshletDrawn;
			const u32 totalMeshlets = phase1mTotal + phase2mTotal;
			const u32 drawnMeshlets = phase1mDrawn + phase2mDrawn;

			const u32 phase1TCand = g_FrameData.phase1TriangleCandidates;
			const u32 phase2TCand = g_FrameData.phase2TriangleCandidates;
			const u32 totalTCand  = phase1TCand + phase2TCand;
#endif // PROFILING_LEVEL >= 1

			// "Drawn instances exceeds total" can happen with meshlet-occlusion ON (2-phase draw of same instance).
			// Detect that here so the cull% cell can be marked N/A instead of going negative.
			const bool bMeshletOcclusionOn =
				(g_FrameData.cullFlags & 0x10u) != 0u;
			const bool bInstanceCullPctMeaningful = !bMeshletOcclusionOn;

			auto pct = [](u64 num, u64 den) { return den > 0 ? 100.0 * double(num) / double(den) : 0.0; };

			// --- Triangle counts (pipeline stats: clip stage in/out per phase) ---
			u64 phase1ClipIn  = 0, phase2ClipIn  = 0;
			u64 phase1ClipOut = 0, phase2ClipOut = 0;
			double phase1Ms   = 0.0, phase2Ms    = 0.0;
			double phase1ClipInEma  = 0.0, phase2ClipInEma  = 0.0;
			double phase1ClipOutEma = 0.0, phase2ClipOutEma = 0.0;
			double phase1FsEma      = 0.0, phase2FsEma      = 0.0;
			for (const auto& e : m_GpuProfileSnapshot)
			{
				if (!e.bHasStats) continue;
				if (e.name == "Phase1Draw")
				{
					phase1ClipIn     = e.clippingInvs;
					phase1ClipOut    = e.clippingPrims;
					phase1Ms         = e.currentMs;
					phase1ClipInEma  = e.clippingInvsEma;
					phase1ClipOutEma = e.clippingPrimsEma;
					phase1FsEma      = e.fsInvocationsEma;
				}
				else if (e.name == "Phase2Draw")
				{
					phase2ClipIn     = e.clippingInvs;
					phase2ClipOut    = e.clippingPrims;
					phase2Ms         = e.currentMs;
					phase2ClipInEma  = e.clippingInvsEma;
					phase2ClipOutEma = e.clippingPrimsEma;
					phase2FsEma      = e.fsInvocationsEma;
				}
			}
			const u64    clipInTotal     = phase1ClipIn  + phase2ClipIn;
			const u64    clipOutTotal    = phase1ClipOut + phase2ClipOut;
			const double clipInTotalEma  = phase1ClipInEma  + phase2ClipInEma;
			const double clipOutTotalEma = phase1ClipOutEma + phase2ClipOutEma;
			const double msTotal         = phase1Ms + phase2Ms;
			const double trisPerMs       = msTotal > 1e-6 ? double(clipOutTotal) / msTotal : 0.0;

			auto formatDouble = [](double v, char* buf, size_t n)
			{
				if (v >= 1e6)      snprintf(buf, n, "%.2fM", v * 1e-6);
				else if (v >= 1e3) snprintf(buf, n, "%.1fK", v * 1e-3);
				else               snprintf(buf, n, "%.0f",  v);
			};

			// =====================================================================
			// Cull pipeline (per-level In → Out → Cull%).
			// =====================================================================
			if (ImGui::BeginTable("##cull_levels", 4,
				ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH))
			{
				ImGui::TableSetupColumn("Level",  ImGuiTableColumnFlags_WidthFixed, 100.0f);
				ImGui::TableSetupColumn("In",     ImGuiTableColumnFlags_WidthFixed, 110.0f);
				ImGui::TableSetupColumn("Out",    ImGuiTableColumnFlags_WidthFixed, 110.0f);
				ImGui::TableSetupColumn("Cull %", ImGuiTableColumnFlags_WidthFixed, 90.0f);
				ImGui::TableHeadersRow();

				char buf[32];
				auto numCell = [&](double v)
				{
					if (v > 0.5) { formatDouble(v, buf, sizeof(buf)); ImGui::TextUnformatted(buf); }
					else ImGui::TextDisabled("-");
				};

				auto row = [&](const char* lvl, double inV, double outV, bool bShowCullPct, const char* note)
				{
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(lvl);
					ImGui::TableSetColumnIndex(1); numCell(inV);
					ImGui::TableSetColumnIndex(2); numCell(outV);
					ImGui::TableSetColumnIndex(3);
					if (!bShowCullPct)
						ImGui::TextDisabled("N/A");
					else if (inV > 0.5)
					{
						const double diff = (inV > outV) ? (inV - outV) : 0.0;
						ImGui::Text("%.1f%%", 100.0 * diff / inV);
					}
					else
						ImGui::TextDisabled("-");
					if (note && *note)
					{
						ImGui::SameLine();
						ImGui::TextDisabled("%s", note);
					}
				};

				row("Instance(CS)", double(totalInst), double(drawnInst), bInstanceCullPctMeaningful, "");
#if PROFILING_LEVEL >= 1
				row("Meshlet(TS)", double(totalMeshlets), double(drawnMeshlets), true, "");
				row("Triangle(MS)", double(totalTCand),  double(clipInTotalEma), true, "");
#endif
				row("Clip(HW)", double(clipInTotalEma), double(clipOutTotalEma), true, "");

				ImGui::EndTable();
			}
#if PROFILING_LEVEL == 0
			ImGui::TextDisabled("(Meshlet/Triangle rows hidden — define PROFILING_LEVEL>=1");
#endif

			// =====================================================================
			// Per-phase detail (Phase 1 emits prev-visible, Phase 2 emits the rest).
			// Meshlets / Tri-cand columns require PROFILING_LEVEL>=1; everything
			// else (instances, clipping primitives, FS invocations) is always on.
			// =====================================================================
			ImGui::Spacing();
			if (ImGui::TreeNode("Per-phase detail"))
			{
#if PROFILING_LEVEL >= 1
				constexpr int kPerPhaseCols = 6;
#else
				constexpr int kPerPhaseCols = 4;
#endif
				if (ImGui::BeginTable("##per_phase", kPerPhaseCols,
					ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH))
				{
					ImGui::TableSetupColumn("Phase",     ImGuiTableColumnFlags_WidthFixed, 70.0f);
					ImGui::TableSetupColumn("Instances", ImGuiTableColumnFlags_WidthFixed, 90.0f);
#if PROFILING_LEVEL >= 1
					ImGui::TableSetupColumn("Meshlets",  ImGuiTableColumnFlags_WidthFixed, 130.0f); // drawn / total
					ImGui::TableSetupColumn("Tri cand",  ImGuiTableColumnFlags_WidthFixed, 100.0f); // triangleCandidates (mesh shader input)
#endif
					ImGui::TableSetupColumn("Tri output",  ImGuiTableColumnFlags_WidthFixed, 100.0f); // clipping primitives (post-HW-clip EMA)
					ImGui::TableSetupColumn("FS invocations", ImGuiTableColumnFlags_WidthFixed, 110.0f);
					ImGui::TableHeadersRow();

					char buf[32];
					auto numCell = [&](double v)
					{
						if (v > 0.5) { formatDouble(v, buf, sizeof(buf)); ImGui::TextUnformatted(buf); }
						else ImGui::TextDisabled("-");
					};
#if PROFILING_LEVEL >= 1
					auto meshletCell = [&](u32 drawn, u32 total)
					{
						if (total == 0) { ImGui::TextDisabled("-"); return; }
						char b1[16]; char b2[16];
						formatDouble(double(drawn), b1, sizeof(b1));
						formatDouble(double(total), b2, sizeof(b2));
						ImGui::Text("%s / %s", b1, b2);
					};
#endif

#if PROFILING_LEVEL >= 1
					auto row = [&](const char* phase, u32 inst, u32 mDrawn, u32 mTot, double tCand, double clipOut, double fs)
					{
						ImGui::TableNextRow();
						ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(phase);
						ImGui::TableSetColumnIndex(1); ImGui::Text("%u", inst);
						ImGui::TableSetColumnIndex(2); meshletCell(mDrawn, mTot);
						ImGui::TableSetColumnIndex(3); numCell(tCand);
						ImGui::TableSetColumnIndex(4); numCell(clipOut);
						ImGui::TableSetColumnIndex(5); numCell(fs);
					};
					row("Phase 1", phase1Inst, phase1mDrawn, phase1mTotal, double(phase1TCand), phase1ClipOutEma, phase1FsEma);
					row("Phase 2", phase2Inst, phase2mDrawn, phase2mTotal, double(phase2TCand), phase2ClipOutEma, phase2FsEma);

					// Aggregate row
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Drawn");
					ImGui::TableSetColumnIndex(1); ImGui::Text("%u", drawnInst);
					ImGui::TableSetColumnIndex(2); meshletCell(drawnMeshlets, totalMeshlets);
					ImGui::TableSetColumnIndex(3); numCell(double(totalTCand));
					ImGui::TableSetColumnIndex(4); numCell(clipOutTotalEma);
					ImGui::TableSetColumnIndex(5); numCell(phase1FsEma + phase2FsEma);
#else // PROFILING_LEVEL >= 1
					auto row = [&](const char* phase, u32 inst, double clipOut, double fs)
					{
						ImGui::TableNextRow();
						ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(phase);
						ImGui::TableSetColumnIndex(1); ImGui::Text("%u", inst);
						ImGui::TableSetColumnIndex(2); numCell(clipOut);
						ImGui::TableSetColumnIndex(3); numCell(fs);
					};
					row("Phase 1", phase1Inst, phase1ClipOutEma, phase1FsEma);
					row("Phase 2", phase2Inst, phase2ClipOutEma, phase2FsEma);

					// Aggregate row
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Drawn");
					ImGui::TableSetColumnIndex(1); ImGui::Text("%u", drawnInst);
					ImGui::TableSetColumnIndex(2); numCell(clipOutTotalEma);
					ImGui::TableSetColumnIndex(3); numCell(phase1FsEma + phase2FsEma);
#endif // PROFILING_LEVEL >= 1

					ImGui::EndTable();
				}
				ImGui::TreePop();
			}

			// --- Throughput summary ---
			ImGui::Spacing();
			if (clipOutTotal > 0 && msTotal > 1e-6)
			{
				char tbuf[32];
				auto formatThroughput = [&](double v) {
					if (v >= 1e9) snprintf(tbuf, sizeof(tbuf), "%.2f Gtri/s", v * 1e-9);
					else if (v >= 1e6) snprintf(tbuf, sizeof(tbuf), "%.2f Mtri/s", v * 1e-6);
					else snprintf(tbuf, sizeof(tbuf), "%.1f Ktri/s", v * 1e-3);
				};
				formatThroughput(trisPerMs * 1000.0);
				ImGui::Text("Throughput : %s  (%.2f ms total draw)", tbuf, msTotal);
			}

			ImGui::Spacing();
			ImGui::TextDisabled("(GPU stats are %u frames stale)", u32(3));
		}

		// --- Frame Anomalies ---
		ImGui::Separator();
		{
			char header[64];
			snprintf(header, sizeof(header), "Frame Anomalies (%zu)###anomalies_header", m_AnomalyLog.size());
			const bool open = ImGui::CollapsingHeader(header, ImGuiTreeNodeFlags_DefaultOpen);
			if (open)
			{
				ImGui::Checkbox("Capture##anomaly_enable", &m_bAnomalyCapture);
				ImGui::SameLine();
				ImGui::SetNextItemWidth(120.0f);
				ImGui::DragFloat("±% threshold##anomaly_thr", &m_AnomalyThresholdPct, 1.0f, 10.0f, 500.0f, "%.0f%%");
				ImGui::SameLine();
				if (ImGui::SmallButton("Clear##anomaly_clear"))
					m_AnomalyLog.clear();
				ImGui::TextDisabled("Captured when |current - EMA|/EMA > threshold. Most recent first.");

				// List (newest first) with expandable detail per row.
				for (i64 i = i64(m_AnomalyLog.size()) - 1; i >= 0; --i)
				{
					const auto& a = m_AnomalyLog[size_t(i)];
					char label[128];
					snprintf(label, sizeof(label), "%s frame #%llu  %.2f ms  (baseline %.2f, %+.0f%%)###anomaly_%lld",
						a.bSpike ? "[SPIKE]" : "[DIP]  ",
						(unsigned long long)a.frameNum,
						a.currentMs, a.baselineMs, a.deltaPct,
						(long long)i);

					// Color-code based on severity
					const ImVec4 col = a.bSpike
						? ImVec4(1.00f, 0.55f, 0.30f, 1.0f)  // spike (orange)
						: ImVec4(0.50f, 0.90f, 1.00f, 1.0f); // dip (cyan)
					ImGui::PushStyleColor(ImGuiCol_Text, col);
					const bool rowOpen = ImGui::TreeNode(label);
					ImGui::PopStyleColor();

					if (rowOpen)
					{
						ImGui::Text("Instances : total %u | Phase1 %u | Phase2 %u (culled %u)",
							a.totalInstances, a.phase1Drawn, a.phase2Drawn,
							a.totalInstances > (a.phase1Drawn + a.phase2Drawn)
								? a.totalInstances - (a.phase1Drawn + a.phase2Drawn) : 0u);
						ImGui::Text("CullFlags : 0x%X  (bit0=backface, bit1=subpixel, bit2=meshletFrustum, bit3=meshletCone, bit4=meshletOcclusion)", a.cullFlags);

						// GPU profile table (compact, no stats columns)
						if (!a.gpuProfile.empty() &&
						    ImGui::BeginTable((std::string("##anomaly_gpu_") + std::to_string(i)).c_str(),
						                      4, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH))
						{
							ImGui::TableSetupColumn("Pass", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoClip, 180.0f);
							ImGui::TableSetupColumn("ms",   ImGuiTableColumnFlags_WidthFixed, 65.0f);
							ImGui::TableSetupColumn("ema",  ImGuiTableColumnFlags_WidthFixed, 65.0f);
							ImGui::TableSetupColumn("Δ %",  ImGuiTableColumnFlags_WidthFixed, 60.0f);
							ImGui::TableHeadersRow();
							for (const auto& e : a.gpuProfile)
							{
								ImGui::TableNextRow();
								ImGui::TableSetColumnIndex(0);
								ImGui::Text("%*s%s", int(e.depth) * 2, "", e.name.c_str());
								ImGui::TableSetColumnIndex(1); ImGui::Text("%6.3f", e.currentMs);
								ImGui::TableSetColumnIndex(2); ImGui::Text("%6.3f", e.emaMs);
								ImGui::TableSetColumnIndex(3);
								const double d = e.emaMs > 1e-6 ? (e.currentMs - e.emaMs) / e.emaMs * 100.0 : 0.0;
								if (std::abs(d) > 20.0)
								{
									ImGui::PushStyleColor(ImGuiCol_Text,
										d > 0.0 ? ImVec4(1,0.6f,0.3f,1) : ImVec4(0.5f,0.9f,1,1));
									ImGui::Text("%+.0f%%", d);
									ImGui::PopStyleColor();
								}
								else ImGui::Text("%+.0f%%", d);
							}
							ImGui::EndTable();
						}
						ImGui::TreePop();
					}
				}
			}
		}

	ImGui::End();

	// **
	// scene hierarchy panel
	// **
	if (!m_pScene)
		return;

	ImGui::Begin("Scene Hierarchy");
	{
		if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows))
		{
			if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_N))
			{
				ImGui::SelectedEntity = m_pScene->CreateEntity("Empty");
			}

			if (ImGui::Shortcut(ImGuiKey_Delete))
			{
				if (ImGui::SelectedEntity.IsValid())
				{
					// execute next frame to avoid data hazard
					ImGui::EntityDeletionQueue.push(std::move(ImGui::SelectedEntity));
				}
			}

			if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_C))
			{
				ImGui::EntityToCopy = ImGui::SelectedEntity;
			}

			if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_V))
			{
				if (ImGui::EntityToCopy.IsValid())
				{
					ImGui::SelectedEntity = ImGui::EntityToCopy.Clone();
				}
			}
		}

		auto rootView = m_pScene->Registry().view< TagComponent, TransformComponent, RootComponent >();
		std::vector< entt::entity > rootEntities;
		rootEntities.reserve(rootView.size_hint());
		rootView.each([&rootEntities](auto id, auto&, auto&) { rootEntities.push_back(id); });

		constexpr static u32 MAX_ENTITY_COUNT_ON_PANEL = 10;
		const int rootCount = static_cast< int >(rootEntities.size());
		if (rootCount > MAX_ENTITY_COUNT_ON_PANEL)
		{
			ImGuiListClipper clipper;
			clipper.Begin(rootCount);
			while (clipper.Step())
			{
				for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++)
				{
					auto id = rootEntities[i];
					auto entity = Entity(m_pScene, id);
					const auto& tag = m_pScene->Registry().get< TagComponent >(id).tag;

					ImGui::PushID((int)(u32)id);

					bool bSelected = (ImGui::SelectedEntity == entity);
					if (ImGui::Selectable(tag.c_str(), bSelected))
						ImGui::SelectedEntity = entity;

					if (ImGui::BeginDragDropSource())
					{
						ImGui::SetDragDropPayload("ENTITY_HIERARCHY", &id, sizeof(entt::entity));
						ImGui::Text("Moving Entity %d", (int)id);
						ImGui::EndDragDropSource();
					}

					if (ImGui::BeginDragDropTarget())
					{
						if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ENTITY_HIERARCHY"))
						{
							entt::entity droppedEntity = *(entt::entity*)payload->Data;
							entity.AttachChild(droppedEntity);
						}
						ImGui::EndDragDropTarget();
					}

					ImGui::PopID();
				}
			}
		}
		else
		{
			for (auto id : rootEntities)
			{
				DrawEntityNode(Entity(m_pScene, id));
			}
		}

		ImVec2 availRegion = ImGui::GetContentRegionAvail();
		ImGui::InvisibleButton("EmptyAreaDropTarget", ImVec2(availRegion.x, availRegion.y > 50 ? availRegion.y : 50));
		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ENTITY_HIERARCHY"))
			{
				entt::entity droppedEntity = *(entt::entity*)payload->Data;

				auto entity = Entity(m_pScene, droppedEntity);
				entity.DetachChild();
			}
			ImGui::EndDragDropTarget();
		}
	}
	ImGui::End();


	// **
	// component panel
	// **
	if (ImGui::SelectedEntity.IsValid())
	{
		ImGui::Begin("Components");
		{
			if (ImGui::CollapsingHeader("Tag"))
			{
				auto& tag = ImGui::SelectedEntity.GetComponent< TagComponent >().tag;
				ImGui::InputText("Name", &tag);
			}
			
			if (ImGui::SelectedEntity.HasAll< TransformComponent >())
			{
				bool bMark = false;

				if (ImGui::CollapsingHeader("Transform"))
				{
					auto& transformComponent = ImGui::SelectedEntity.GetComponent< TransformComponent >();

					ImGui::Text("Position");
					bMark |= ImGui::DragFloat3("##Position", glm::value_ptr(transformComponent.transform.position), 0.1f);

					ImGui::Text("Rotation");
					bMark |= ImGui::DragFloat3("##Rotation", glm::value_ptr(transformComponent.transform.rotation), 0.1f);

					ImGui::Text("Scale");
					bMark |= ImGui::DragFloat3("##Scale", glm::value_ptr(transformComponent.transform.scale), 0.1f);
				}

				if (bMark)
				{
					m_pScene->Registry().patch< TransformComponent >(ImGui::SelectedEntity.ID(), [](auto&) {});
				}
			}

			if (ImGui::SelectedEntity.HasAll< CameraComponent >())
			{
				bool bMark = false;
				if (ImGui::CollapsingHeader("Camera"))
				{
					auto& cameraComponent = ImGui::SelectedEntity.GetComponent< CameraComponent >();

					ImGui::Text("CameraType");
					if (ImGui::BeginCombo("##CameraType", GetCameraTypeString(cameraComponent.type).data()))
					{
						if (ImGui::Selectable(GetCameraTypeString(CameraComponent::eType::Perspective).data(), cameraComponent.type == CameraComponent::eType::Perspective))
						{
							bMark = true;
							cameraComponent.type = CameraComponent::eType::Perspective;
						}

						if (ImGui::Selectable(GetCameraTypeString(CameraComponent::eType::Orthographic).data(), cameraComponent.type == CameraComponent::eType::Orthographic))
						{
							bMark = true;
							cameraComponent.type = CameraComponent::eType::Orthographic;
						}

						ImGui::EndCombo();
					}

					float width = (ImGui::GetWindowWidth() - ImGui::GetStyle().ItemSpacing.x);

					ImGui::PushItemWidth(width * 0.3f);
					ImGui::Text("ClippingRange");
					bMark |= ImGui::InputFloat("##ClipNear", &cameraComponent.cNear, 0, 0, "%.2f");

					ImGui::PushItemWidth(width * 0.7f);
					ImGui::SameLine();
					bMark |= ImGui::InputFloat("##ClipFar", &cameraComponent.cFar, 0, 0, "%.2f");

					ImGui::Text("FoV");
					bMark |= ImGui::DragFloat("##FoV", &cameraComponent.fov, 0.1f, 1.0f, 90.0f, "%.1f");

					bool bMain = cameraComponent.bMain;
					ImGui::Checkbox("MainCam", &bMain);
				}

				if (bMark)
				{
					m_pScene->Registry().patch< CameraComponent >(ImGui::SelectedEntity.ID(), [](auto&) {});
				}
			}

			if (ImGui::SelectedEntity.HasAll< StaticMeshComponent >())
			{
				if (ImGui::CollapsingHeader("StaticMesh"))
				{
					auto& component = ImGui::SelectedEntity.GetComponent< StaticMeshComponent >();

					if (ImGui::Button("Mesh")) ImGui::ContentBrowserSetup = eContentButton_Mesh;
					ImGui::SameLine(); ImGui::Text(component.tag.c_str());
				}

				if (ImGui::SelectedEntity.HasAll< MaterialComponent >())
				{
					bool bMark = false;
					if (ImGui::CollapsingHeader("Material"))
					{
						auto& component = ImGui::SelectedEntity.GetComponent< MaterialComponent >();

						ImGui::Indent();
						{
							if (ImGui::CollapsingHeader("Basic"))
							{
								ImGui::Text("Tint");
								bMark |= ImGui::DragFloat3("##Tint", glm::value_ptr(component.tint), 0.01f, 0.0f, 1.0f, "%.2f");

								if (ImGui::Button("AlbedoTex")) ImGui::ContentBrowserSetup = eContentButton_Albedo;
								ImGui::SameLine(); ImGui::Text(component.albedoTex.c_str());
								if (ImGui::Button("NormalTex")) ImGui::ContentBrowserSetup = eContentButton_Normal;
								ImGui::SameLine(); ImGui::Text(component.normalTex.c_str());
							}

							if (ImGui::CollapsingHeader("ORM"))
							{
								ImGui::Text("Roughness");
								bMark |= ImGui::DragFloat("##Roughness", &component.roughness, 0.01f, 0.0f, 1.0f, "%.2f");
								ImGui::Text("Metallic");
								bMark |= ImGui::DragFloat("##Metallic", &component.metallic, 0.01f, 0.0f, 1.0f, "%.2f");

								if (ImGui::Button("AoTex")) ImGui::ContentBrowserSetup = eContentButton_Ao;
								ImGui::SameLine(); ImGui::Text(component.aoTex.c_str());
								if (ImGui::Button("RoughnessTex")) ImGui::ContentBrowserSetup = eContentButton_Roughness;
								ImGui::SameLine(); ImGui::Text(component.roughnessTex.c_str());
								if (ImGui::Button("MetallicTex")) ImGui::ContentBrowserSetup = eContentButton_Metallic;
								ImGui::SameLine(); ImGui::Text(component.metallicTex.c_str());
							}

							if (ImGui::CollapsingHeader("Emission"))
							{
								ImGui::Text("EmissivePower");
								bMark |= ImGui::DragFloat("##EmissivePower", &component.emissivePower, 0.01f, 0.0f, 100.0f, "%.2f");

								if (ImGui::Button("EmissionTex")) ImGui::ContentBrowserSetup = eContentButton_Emission;
								ImGui::SameLine(); ImGui::Text(component.emissionTex.c_str());
							}

							if (ImGui::CollapsingHeader("Clearcoat"))
							{
								bMark |= ImGui::DragFloat("Factor##CC", &component.clearcoat, 0.01f, 0.0f, 1.0f);
								bMark |= ImGui::DragFloat("Roughness##CC", &component.clearcoatRoughness, 0.01f, 0.0f, 1.0f);

								if (ImGui::Button("ClearcoatTex")) ImGui::ContentBrowserSetup = eContentButton_Clearcoat;
								ImGui::SameLine(); ImGui::Text(component.clearcoatTex.c_str());
							}

							if (ImGui::CollapsingHeader("Anisotropy"))
							{
								bMark |= ImGui::DragFloat("Strength##Aniso", &component.anisotropy, 0.01f, 0.0f, 1.0f);
								bMark |= ImGui::DragFloat("Rotation##Aniso", &component.anisotropyRotation, 0.01f, 0.0f, 6.283f);

								if (ImGui::Button("AnisotropyTex")) ImGui::ContentBrowserSetup = eContentButton_Anisotropy;
								ImGui::SameLine(); ImGui::Text(component.anisotropyTex.c_str());
							}

							if (ImGui::CollapsingHeader("Sheen"))
							{
								bMark |= ImGui::ColorEdit3("Color##Sheen", glm::value_ptr(component.sheenColor));
								bMark |= ImGui::DragFloat("Roughness##Sheen", &component.sheenRoughness, 0.01f, 0.0f, 1.0f);

								if (ImGui::Button("SheenTex")) ImGui::ContentBrowserSetup = eContentButton_Sheen;
								ImGui::SameLine(); ImGui::Text(component.sheenTex.c_str());
							}

							if (ImGui::CollapsingHeader("Subsurface"))
							{
								bMark |= ImGui::DragFloat("Factor##SS", &component.subsurface, 0.01f, 0.0f, 1.0f);
							}

							if (ImGui::CollapsingHeader("Transmission"))
							{
								bMark |= ImGui::DragFloat("Factor##Trans", &component.transmission, 0.01f, 0.0f, 1.0f);
							}

							ImGui::Separator();
							ImGui::Text("Alpha Cutoff");
							bMark |= ImGui::DragFloat("##AlphaCutoff", &component.alphaCutoff, 0.01f, 0.0f, 1.0f);
							ImGui::Text("SpecularStrength");
							bMark |= ImGui::DragFloat("##Specular", &component.specularStrength, 0.01f, 0.0f, 1.0f);
							ImGui::Text("SpecularColor");
							bMark |= ImGui::ColorEdit3("##SpecColor", glm::value_ptr(component.specularColor));
							ImGui::Text("IOR");
							bMark |= ImGui::DragFloat("##IOR", &component.ior, 0.01f, 1.0f, 10.0f, "%.2f");
						}
					}

					if (bMark)
					{
						m_pScene->Registry().patch< MaterialComponent >(ImGui::SelectedEntity.ID(), [](auto&) {});
					}
				}
			}

			if (ImGui::SelectedEntity.HasAll< LightComponent >())
			{
				bool bMark = false;
				if (ImGui::CollapsingHeader("Light"))
				{
					auto& component = ImGui::SelectedEntity.GetComponent< LightComponent >();

					const char* lightTypes[] = { "Directional", "Spot", "Area", "Sphere", "Disk", "Tube" };
					int currentType = (int)component.type;
					if (ImGui::Combo("Type", &currentType, lightTypes, IM_ARRAYSIZE(lightTypes)))
					{
						component.type = (eLightType)currentType;

						switch (component.type)
						{
						case eLightType::Directional:
							component.SetDefaultDirectionalLight();
							break;
						case eLightType::Spot:
							component.SetDefaultSpot();
							break;
						case eLightType::Area:
							component.SetDefaultArea();
							break;
						case eLightType::Sphere:
							component.SetDefaultSphere();
							break;
						case eLightType::Disk:
							component.SetDefaultDisk();
							break;
						case eLightType::Tube:
							component.SetDefaultTube();
							break;
						}

						bMark = true;
					}

					bMark |= ImGui::ColorEdit3("Color", &component.color.x);

					bMark |= ImGui::DragFloat("Temperature (K)", &component.temperatureK, 10.0f, 0.0f, 10000.0f);
					if (ImGui::IsItemHovered())
					{
						ImGui::BeginTooltip();
						ImGui::Text("Use RGB color if temperature is 0");
						ImGui::EndTooltip();
					}

					switch (component.type)
					{

					case eLightType::Directional:
					{
						bMark |= ImGui::DragFloat("Illuminance (lux)", &component.illuminanceLux, 0.01f, 0.0f, 10.0f, "%.2f");
						if (ImGui::IsItemHovered())
						{
							ImGui::BeginTooltip();
							ImGui::EndTooltip();
						}

						float angularRadius = glm::degrees(component.angularRadiusRad);
						if (ImGui::DragFloat("Angular Radius (deg)", &angularRadius, 0.01f, 0.0f, 10.0f, "%.3f"))
						{
							component.angularRadiusRad = glm::radians(angularRadius);

							bMark = true;
						}
						break;
					}
					case eLightType::Spot:
					{
						bMark |= ImGui::DragFloat("Power (lm)", &component.luminousFluxLm, 10.0f, 0.0f, 10000.0f, "%.0f");
						bMark |= ImGui::DragFloat("Source Radius (m)", &component.radiusM, 0.001f, 0.001f, 1.0f, "%.3f");

						float innerAngle = glm::degrees(component.innerConeAngleRad);
						float outerAngle = glm::degrees(component.outerConeAngleRad);

						if (ImGui::DragFloat("Inner Angle (deg)", &innerAngle, 1.0f, 0.0f, 90.0f, "%.1f"))
						{
							component.innerConeAngleRad = glm::radians(innerAngle);

							if (component.outerConeAngleRad <= component.innerConeAngleRad)
								component.outerConeAngleRad = component.innerConeAngleRad + glm::radians(1.0f);

							bMark = true;
						}

						if (ImGui::DragFloat("Outer Angle (deg)", &outerAngle, 1.0f, 0.0f, 90.0f, "%.1f"))
						{
							component.outerConeAngleRad = glm::radians(outerAngle);

							if (component.innerConeAngleRad >= component.outerConeAngleRad)
								component.innerConeAngleRad = component.outerConeAngleRad - glm::radians(1.0f);

							bMark = true;
						}
						break;
					}

					case eLightType::Area:
					{
						bMark |= ImGui::DragFloat("Power (lm)", &component.luminousFluxLm, 1.0f, 0.0f, 10000.0f, "%.0f");
						if (ImGui::IsItemHovered())
						{
							ImGui::BeginTooltip();
							ImGui::EndTooltip();
						}
						break;
					}

					case eLightType::Sphere:
					{
						bMark |= ImGui::DragFloat("Power (lm)", &component.luminousFluxLm, 1.0f, 0.0f, 10000.0f, "%.0f");
						if (ImGui::IsItemHovered())
						{
							ImGui::BeginTooltip();
							ImGui::EndTooltip();
						}
						bMark |= ImGui::DragFloat("Source Radius (m)", &component.radiusM, 0.001f, 0.001f, 100.0f, "%.3f");
						break;
					}

					case eLightType::Disk:
					{
						bMark |= ImGui::DragFloat("Power (lm)", &component.luminousFluxLm, 1.0f, 0.0f, 10000.0f, "%.0f");
						bMark |= ImGui::DragFloat("Disk Radius (m)", &component.diskRadiusM, 0.05f, 0.05f, 10.0f, "%.2f");
						break;
					}

					case eLightType::Tube:
					{
						bMark |= ImGui::DragFloat("Power (lm)", &component.luminousFluxLm, 1.0f, 0.0f, 10000.0f, "%.0f");
						bMark |= ImGui::DragFloat("Length (m)", &component.tubeLengthM, 0.1f, 0.1f, 10.0f, "%.2f");
						bMark |= ImGui::DragFloat("Tube Radius (m)", &component.tubeRadiusM, 0.01f, 0.0f, 1.0f, "%.3f");
						break;
					}

					}
				}

				if (bMark)
				{
					m_pScene->Registry().patch< LightComponent >(ImGui::SelectedEntity.ID(), [](auto&) {});
				}
			}

			if (ImGui::SelectedEntity.HasAll< AtmosphereComponent >())
			{
				bool bMark = false;
				if (ImGui::CollapsingHeader("Atmosphere"))
				{
					ImGui::Indent();
					{
						auto& component = ImGui::SelectedEntity.GetComponent< AtmosphereComponent >();

						// planet
						if (ImGui::CollapsingHeader("Planet"))
						{
							float height = component.atmosphereRadiusKm - component.planetRadiusKm;
							if (ImGui::DragFloat("Atmosphere Height (km)", &height, 1.0f, 1.0f, 200.0f, "%.1f"))
							{
								component.atmosphereRadiusKm = component.planetRadiusKm + height;

								bMark = true;
							}
						}

						// rayleigh
						if (ImGui::CollapsingHeader("Rayleigh"))
						{
							float3 rayleighScattering = component.rayleighScattering * 1e3f;
							if (ImGui::DragFloat3("Rayleigh Scattering Coefficient", glm::value_ptr(rayleighScattering), 0.01f, 0.0f, 1000.0f, "%.3f"))
							{
								component.rayleighScattering = rayleighScattering * 1e-3f;

								bMark = true;
							}
							if (ImGui::IsItemHovered())
							{
								ImGui::BeginTooltip();
								ImGui::Text("Rayleigh Scattering Scale : 0.001");
								ImGui::EndTooltip();
							}
							bMark |= ImGui::DragFloat("Rayleigh Density (km)", &component.rayleighDensityKm, 0.1f, 0.1f, 20.0f, "%.3f");
							if (ImGui::IsItemHovered())
							{
								ImGui::BeginTooltip();
								ImGui::Text("Reduce 40%% of scattering effect per Km");
								ImGui::EndTooltip();
							}
						}

						// mie
						if (ImGui::CollapsingHeader("Mie"))
						{
							float mieScattering = component.mieScattering * 1e3f;
							if (ImGui::DragFloat("Mie Scattering Coefficient", &mieScattering, 0.01f, 0.0f, 1000.0f, "%.3f"))
							{
								component.mieScattering = mieScattering * 1e-3f;

								bMark = true;
							}
							if (ImGui::IsItemHovered())
							{
								ImGui::BeginTooltip();
								ImGui::Text("Mie Scattering Scale : 0.001");
								ImGui::EndTooltip();
							}
							float mieAbsorption = component.mieAbsorption * 1e3f;
							if (ImGui::DragFloat("Mie Absorption Coefficient", &mieAbsorption, 0.01f, 0.0f, 1000.0f, "%.3f"))
							{
								component.mieAbsorption = mieAbsorption * 1e-3f;

								bMark = true;
							}
							if (ImGui::IsItemHovered())
							{
								ImGui::BeginTooltip();
								ImGui::Text("Mie Absorption Scale : 0.001");
								ImGui::EndTooltip();
							}
							bMark |= ImGui::DragFloat("Mie Density (km)", &component.mieDensityKm, 0.01f, 0.01f, 10.0f, "%.3f");
							if (ImGui::IsItemHovered())
							{
								ImGui::BeginTooltip();
								ImGui::Text("Reduce 40%% of mie effect per Km");
								ImGui::EndTooltip();
							}
							bMark |= ImGui::DragFloat("Mie Phase", &component.miePhaseG, 0.01f, 0.0f, 1.0f, "%.3f");
						}

						// ozone
						if (ImGui::CollapsingHeader("Ozone"))
						{
							float3 ozoneAbsorption = component.ozoneAbsorption * 1e3f;
							if (ImGui::DragFloat3("Ozone Absorption Coefficient", glm::value_ptr(ozoneAbsorption), 0.01f, 0.0f, 1000.0f, "%.3f"))
							{
								component.ozoneAbsorption = ozoneAbsorption * 1e-3f;

								bMark = true;
							}
							if (ImGui::IsItemHovered())
							{
								ImGui::BeginTooltip();
								ImGui::Text("Ozone Absorption Scale : 0.001");
								ImGui::EndTooltip();
							}
							bMark |= ImGui::DragFloat("Ozone Center (km)", &component.ozoneCenterKm, 1.0f, 1.0f, 60.0f, "%.1f");
							bMark |= ImGui::DragFloat("Ozone Width (km)", &component.ozoneWidthKm, 1.0f, 1.0f, 20.0f, "%.1f");

							if (ImGui::BeginCombo("##Resolution", "Raymarch Resolution"))
							{
								if (ImGui::Selectable("Low", component.raymarchResolution == eRaymarchResolution::Low))
								{
									component.raymarchResolution = eRaymarchResolution::Low;

									bMark = true;
								}
								if (ImGui::Selectable("Middle", component.raymarchResolution == eRaymarchResolution::Middle))
								{
									component.raymarchResolution = eRaymarchResolution::Middle;

									bMark = true;
								}
								if (ImGui::Selectable("High", component.raymarchResolution == eRaymarchResolution::High))
								{
									component.raymarchResolution = eRaymarchResolution::High;

									bMark = true;
								}

								ImGui::EndCombo();
							}
						}

						if (ImGui::CollapsingHeader("Skybox"))
						{
							if (ImGui::Button("SkyboxLUT")) ImGui::ContentBrowserSetup = eContentButton_Skybox;
							ImGui::SameLine(); ImGui::Text(component.skybox.c_str());
						}
					}

					ImGui::Unindent();
				}

				if (bMark)
				{
					m_pScene->Registry().patch< AtmosphereComponent >(ImGui::SelectedEntity.ID(), [](auto&) {});
				}
			}

			if (ImGui::SelectedEntity.HasAll< CloudComponent >())
			{
				bool bMark = false;
				if (ImGui::CollapsingHeader("Cloud"))
				{
					ImGui::Indent();
					{
						auto& component = ImGui::SelectedEntity.GetComponent< CloudComponent >();

						if (ImGui::CollapsingHeader("Basic"))
						{
							bMark |= ImGui::DragFloat("Cloud Bottom Height (km)", &component.bottomHeightKm, 0.1f, 0.0f, 10.0f, "%.1f");
							bMark |= ImGui::DragFloat("Cloud Thickness (km)", &component.layerThicknessKm, 0.1f, 0.1f, 20.0f, "%.1f");
							bMark |= ImGui::DragFloat("Shadow Tracing Distance Multiplier", &component.shadowTracingDistanceMultiplier, 0.01f, 0.1f, 10.0f, "%.2f");

							bMark |= ImGui::ColorEdit3("Cloud Albedo", glm::value_ptr(component.albedo));
							bMark |= ImGui::DragFloat("Albedo Scale", &component.albedoScale, 0.01f, 0.1f, 3.0f, "%.2f");

							bMark |= ImGui::DragFloat("Ground Contribution", &component.groundContributionStrength, 0.001f, 0.0f, 1.0f, "%.3f");

							if (ImGui::DragFloat3("Wind Direction", glm::value_ptr(component.windDirection), 0.01f, 0.0f, 1.0f, "%.2f"))
							{
								component.windDirection = glm::normalize(component.windDirection);

								bMark = true;
							}
							bMark |= ImGui::DragFloat("Wind Speed(m/s)", &component.windSpeedMps, 0.1f, 0.0f, 1000.0f, "%.1f");
						}
						if (ImGui::CollapsingHeader("Shape"))
						{
							bMark |= ImGui::DragFloat("Cloud Floor Variation(Clear)", &component.floorVariationClear, 0.01f, 0.0f, 10.0f, "%.3f");
							bMark |= ImGui::DragFloat("Cloud Floor Variation(Cloudy)", &component.floorVariationCloudy, 0.01f, 0.0f, 10.0f, "%.3f");

							bMark |= ImGui::DragFloat("Cloud Scale", &component.cloudsScale, 0.01f, 0.1f, 10.0f, "%.2f");
							if (ImGui::IsItemHovered())
							{
								ImGui::BeginTooltip();
								ImGui::Text("Determines overall amount of clouds");
								ImGui::EndTooltip();
							}
							bMark |= ImGui::DragFloat("Cloud Macro UVScale", &component.cloudsMacroUvScale, 1.0f, 1.0f, 100000.0f, "%.0f");
							bMark |= ImGui::DragFloat("Cloud Coverage", &component.cloudsCoverage, 0.001f, 0.0f, 5.0f, "%.3f");
							bMark |= ImGui::DragFloat("Clumps Variation", &component.clumpsVariation, 0.001f, 0.1f, 3.0f, "%.3f");
							if (ImGui::IsItemHovered())
							{
								ImGui::BeginTooltip();
								ImGui::Text("Determines size of clumps");
								ImGui::EndTooltip();
							}

							bMark |= ImGui::DragFloat("Base Density", &component.baseDensity, 0.001f, 0.0f, 10.0f, "%.3f");
							bMark |= ImGui::DragFloat("Base Erosion Scale", &component.baseErosionScale, 0.001f, 0.01f, 10.0f, "%.3f");
							bMark |= ImGui::DragFloat("Base Erosion Power", &component.baseErosionPower, 0.001f, 0.0f, 10.0f, "%.3f");
							bMark |= ImGui::DragFloat("Base Erosion Strength", &component.baseErosionStrength, 0.01f, 0.0f, 10.0f, "%.2f");

							bMark |= ImGui::DragFloat("High Frequency Erosion Strength", &component.hfErosionStrength, 0.001f, 0.0f, 1.0f, "%.2f");
							bMark |= ImGui::DragFloat("High Frequency Erosion Distortion", &component.hfErosionDistortion, 0.001f, 0.0f, 5.0f, "%.2f");
						}
						if (ImGui::CollapsingHeader("Shade"))
						{
							bMark |= ImGui::ColorEdit3("Scattering Scale", glm::value_ptr(component.scatteringScale));
							bMark |= ImGui::DragFloat("Extinction Scale", &component.extinctionScale, 0.01f, 1.0f, 20.0f, "%.3f");

							bMark |= ImGui::DragFloat("MultiScattering Contribution", &component.msContribution, 0.001f, 0.0f, 1.0f, "%.3f");
							bMark |= ImGui::DragFloat("MultiScattering Occlusion", &component.msOcclusion, 0.001f, 0.0f, 1.0f, "%.3f");
							bMark |= ImGui::DragFloat("MultiScattering Eccentricity", &component.msEccentricity, 0.001f, 0.0f, 1.0f, "%.3f");

							bMark |= ImGui::DragFloat("SilverLining Strength", &component.silverScatterG, 0.001f, 0.01f, 0.99f, "%.3f");

							bMark |= ImGui::DragFloat("Ambient Intensity", &component.ambientIntensity, 0.001f, 0.0f, 10.0f, "%.3f");
							bMark |= ImGui::DragFloat("Ambient Saturation", &component.ambientSaturation, 0.001f, 0.0f, 10.0f, "%.3f");
							bMark |= ImGui::DragFloat("Top Ambient Scale", &component.topAmbientScale, 0.001f, 0.0f, 10.0f, "%.3f");
							bMark |= ImGui::DragFloat("Bottom Ambient Scale", &component.bottomAmbientScale, 0.001f, 0.0f, 10.0f, "%.3f");
						}
						if (ImGui::CollapsingHeader("Performance"))
						{
							auto currentType = magic_enum::enum_name(component.uprezRatio);
							if (ImGui::BeginCombo("Cloud Up-resolution Ratio", currentType.data()))
							{
								if (ImGui::Selectable("Original", component.uprezRatio == eCloudUprezRatio::X1))
								{
									component.uprezRatio = eCloudUprezRatio::X1;

									bMark = true;
								}
								else if (ImGui::Selectable("Half", component.uprezRatio == eCloudUprezRatio::X2))
								{
									component.uprezRatio = eCloudUprezRatio::X2;

									bMark = true;
								}
								else if (ImGui::Selectable("Quarter", component.uprezRatio == eCloudUprezRatio::X4))
								{
									component.uprezRatio = eCloudUprezRatio::X4;

									bMark = true;
								}

								ImGui::EndCombo();
							}

							bMark |= ImGui::DragInt("Steps of Raymarch to Cloud", &component.numCloudRaymarchSteps, 1, 32, 284);
							bMark |= ImGui::DragInt("Steps of Raymarch to Light", &component.numLightRaymarchSteps, 1, 6, 64);
							bMark |= ImGui::DragFloat("Front Depth Bias", &component.frontDepthBias, 0.001f, 0.001f, 1.0f, "%.3f");
							bMark |= ImGui::DragFloat("Blend Alpha for Temporal Accumulation", &component.temporalBlendAlpha, 0.001f, 0.01f, 1.0f, "%.3f");
						}
					}

					ImGui::Unindent();
				}

				if (bMark)
				{
					m_pScene->Registry().patch< CloudComponent >(ImGui::SelectedEntity.ID(), [](auto&) {});
				}
			}

			if (ImGui::SelectedEntity.HasAll< PostProcessComponent >())
			{
				if (ImGui::CollapsingHeader("PostProcess"))
				{
					bool bMark = false;
					ImGui::Indent();
					{
						auto& component = ImGui::SelectedEntity.GetComponent< PostProcessComponent >();
						if (ImGui::CollapsingHeader("Height Fog(TODO)"))
						{
							bool bApply = component.effectBits & ePostProcess::HeightFog;
							bMark |= ImGui::Checkbox("Apply HeightFog", &bApply);
							component.effectBits =
								(component.effectBits & ~(1 << ePostProcess::AntiAliasing)) | (bApply << ePostProcess::HeightFog);

							ImGui::DragFloat("ExponentialFactor", &component.heightFog.exponentialFactor, 0.1f, 0.0f, 20.0f, "%.1f");
						}

						if (ImGui::CollapsingHeader("Bloom(TODO)"))
						{
							bool bApply = component.effectBits & ePostProcess::Bloom;
							bMark |= ImGui::Checkbox("Apply Bloom", &bApply);
							component.effectBits =
								(component.effectBits & ~(1 << ePostProcess::AntiAliasing)) | (bApply << ePostProcess::Bloom);

							ImGui::DragInt("FilterSize", &component.bloom.filterSize, 1, 1, 16);
						}

						if (ImGui::CollapsingHeader("AntiAliasing"))
						{
							bool bApply = component.effectBits & (1 << ePostProcess::AntiAliasing);
							bMark |= ImGui::Checkbox("Apply Anti-Aliasing", &bApply);
							component.effectBits =
								(component.effectBits & ~(1 << ePostProcess::AntiAliasing)) | (bApply << ePostProcess::AntiAliasing);

							auto svCurrentType = magic_enum::enum_name(component.aa.type);
							if (ImGui::BeginCombo("AntiAliasing Type", svCurrentType.data()))
							{
								if (ImGui::Selectable("TAA", component.aa.type == eAntiAliasingType::TAA))
								{
									component.aa.type = eAntiAliasingType::TAA;

									bMark = true;
								}
								if (ImGui::Selectable("FXAA", component.aa.type == eAntiAliasingType::FXAA))
								{
									component.aa.type = eAntiAliasingType::FXAA;

									bMark = true;
								}

								ImGui::EndCombo();
							}

							if (component.aa.type == eAntiAliasingType::TAA)
							{
								bMark |= ImGui::DragFloat("Blend Factor", &component.aa.blendFactor, 0.01f, 0.0f, 1.0f, "%.2f");
								bMark |= ImGui::DragFloat("Sharpness", &component.aa.sharpness, 0.01f, 0.0f, 1.0f, "%.2f");
							}
						}

						if (ImGui::CollapsingHeader("ToneMapping"))
						{
							auto svCurrentOp = magic_enum::enum_name(component.tonemap.op);
							if (ImGui::BeginCombo("ToneMap Operation", svCurrentOp.data()))
							{
								if (ImGui::Selectable("None", component.tonemap.op == eToneMappingOp::None))
								{
									component.tonemap.op = eToneMappingOp::None;

									bMark = true;
								}
								if (ImGui::Selectable("Reinhard", component.tonemap.op == eToneMappingOp::Reinhard))
								{
									component.tonemap.op = eToneMappingOp::Reinhard;

									bMark = true;
								}
								if (ImGui::Selectable("ACES", component.tonemap.op == eToneMappingOp::ACES))
								{
									component.tonemap.op = eToneMappingOp::ACES;

									bMark = true;
								}
								if (ImGui::Selectable("Uncharted2", component.tonemap.op == eToneMappingOp::Uncharted2))
								{
									component.tonemap.op = eToneMappingOp::Uncharted2;

									bMark = true;
								}

								ImGui::EndCombo();
							}

							bMark |= ImGui::DragFloat("EV100", &component.tonemap.ev100, 0.01f, -15.0f, 15.0f, "%.2f");
							bMark |= ImGui::DragFloat("Gamma", &component.tonemap.gamma, 0.1f, 0.1f, 10.0f, "%.1f");
						}
					}
					ImGui::Unindent();

					if (bMark)
					{
						m_pScene->Registry().patch< PostProcessComponent >(ImGui::SelectedEntity.ID(), [](auto&) {});
					}
				}
			}

			if (ImGui::SelectedEntity.HasAll< ScriptComponent >())
			{
				if (ImGui::CollapsingHeader("Behaviour"))
				{
					auto& scriptComponent = ImGui::SelectedEntity.GetComponent< ScriptComponent >();

					ImGui::Checkbox("Move", &scriptComponent.bMove);
					ImGui::DragFloat3("MoveVelocity", glm::value_ptr(scriptComponent.moveVelocity), 0.01f, -10.0f, 10.0f);

					ImGui::Checkbox("Rotate", &scriptComponent.bRotate);
					ImGui::DragFloat3("RotationVelocity", glm::value_ptr(scriptComponent.rotationVelocity), 0.001f, -1.0f, 1.0f);
				}
			}

			if (ImGui::Button("Add Component"))
				ImGui::OpenPopup("AddComponentPopup");

			if (ImGui::BeginPopup("AddComponentPopup")) 
			{
				/*if (!ImGui::SelectedEntity.HasAny< CameraComponent >())
				{
					if (ImGui::MenuItem("Camera"))
					{
						m_ImGuiMutex.lock();

						ImGui::SelectedEntity.AttachComponent< CameraComponent >();

						m_ImGuiMutex.unlock();
					}
				}*/

				if (!ImGui::SelectedEntity.HasAny< StaticMeshComponent >())
				{
					if (ImGui::MenuItem("StaticMesh"))
					{
						m_ImGuiMutex.lock();

						ImGui::SelectedEntity.AttachComponent< StaticMeshComponent >();

						m_ImGuiMutex.unlock();
					}
				}
				else if (!ImGui::SelectedEntity.HasAny< MaterialComponent >())
				{
					if (ImGui::MenuItem("Material"))
					{
						m_ImGuiMutex.lock();

						ImGui::SelectedEntity.AttachComponent< MaterialComponent >();

						m_ImGuiMutex.unlock();
					}
				}

				ImGui::EndPopup();
			}
		}
		ImGui::End();
	}



	// **
	// content browser
	// **
	if (ImGui::ContentBrowserSetup)
	{
		ImGui::Begin("Content Browser");
		{
			if (m_CurrentDirectory != fs::path(ASSET_PATH))
			{
				if (ImGui::Button("<"))
				{
					m_CurrentDirectory = m_CurrentDirectory.parent_path();
				}
			}

			if (m_CurrentDirectory != m_CachedBrowserPath)
			{
				m_CachedDirectoryEntries.clear();
				for (auto& entry : fs::directory_iterator(m_CurrentDirectory))
					m_CachedDirectoryEntries.push_back(entry);
				m_CachedBrowserPath = m_CurrentDirectory;
			}

			for (auto& entry : m_CachedDirectoryEntries)
			{
				const auto& path = entry.path();
				auto relativePath = fs::relative(entry.path(), ASSET_PATH);
				auto filenameStr = relativePath.filename().string();
				if (entry.is_directory())
				{
					if (ImGui::Button(filenameStr.c_str()))
					{
						m_CurrentDirectory /= path.filename();
					}
				}
				else
				{
					std::string extensionStr = path.extension().string();

					switch(ImGui::ContentBrowserSetup)
					{
					case eContentButton_Mesh:
						if (extensionStr == ".fbx" || extensionStr == ".obj" || extensionStr == ".gltf")
						{
							bool bMark = false;
							if (ImGui::Selectable(filenameStr.c_str()))
							{
								if (ImGui::SelectedEntity.HasAll< StaticMeshComponent >())
								{
									auto& component = ImGui::SelectedEntity.GetComponent< StaticMeshComponent >();
									if (component.path != path.string())
									{
										m_pScene->ImportModel(ImGui::SelectedEntity, path, {});

										component.path = path.string();
										bMark = true;
									}
								}

								ImGui::ContentBrowserSetup = 0;
							}

							if (bMark)
							{
								m_pScene->Registry().patch< StaticMeshComponent >(ImGui::SelectedEntity.ID(), [](auto&) {});
							}
						}
						break;

					case eContentButton_Albedo:
					case eContentButton_Normal:
					case eContentButton_Ao:
					case eContentButton_Metallic:
					case eContentButton_Roughness:
					case eContentButton_Emission:
						if (extensionStr == ".png" || extensionStr == ".jpg")
						{
							bool bMark = false;
							if (ImGui::Selectable(filenameStr.c_str())) 
							{
								if (ImGui::SelectedEntity.HasAll< StaticMeshComponent, MaterialComponent >())
								{
									auto& component = ImGui::SelectedEntity.GetComponent< MaterialComponent >();
									switch(ImGui::ContentBrowserSetup)
									{
									case eContentButton_Albedo:
										if (component.albedoTex != path.string())
										{
											component.albedoTex = path.string();
											bMark = true;
										}
										break;
									case eContentButton_Normal:
										if (component.normalTex != path.string())
										{
											component.normalTex = path.string();
											bMark = true;
										}
										break;
									case eContentButton_Ao:
										if (component.aoTex != path.string())
										{
											component.aoTex = path.string();
											bMark = true;
										}
										break;
									case eContentButton_Metallic:
										if (component.metallicTex != path.string())
										{
											component.metallicTex = path.string();
											bMark = true;
										}
										break;
									case eContentButton_Roughness:
										if (component.roughnessTex != path.string())
										{
											component.roughnessTex = path.string();
											bMark = true;
										}
										break;
									case eContentButton_Emission:
										if (component.emissionTex != path.string())
										{
											component.emissionTex = path.string();
											bMark = true;
										}
										break;
									default:
										break;
									}
								}

								ImGui::ContentBrowserSetup = 0;
							}

							if (bMark)
							{
								m_pScene->Registry().patch< MaterialComponent >(ImGui::SelectedEntity.ID(), [](auto&) {});
							}
						}
						break;

					case eContentButton_Skybox:
						if (extensionStr == ".png" || extensionStr == ".jpg" || extensionStr == ".dds" || extensionStr == ".hdr" || extensionStr == ".ktx")
						{
							if (ImGui::Selectable(filenameStr.c_str()))
							{
								auto& component = ImGui::SelectedEntity.GetComponent< AtmosphereComponent >();

								if (component.skybox != path.string())
								{
									component.skybox = path.string();

									m_pScene->Registry().patch< AtmosphereComponent >(ImGui::SelectedEntity.ID(), [](auto&) {});
								}

								ImGui::ContentBrowserSetup = 0;
							}
						}
						break;

					default:
						break;
					}
				}
			}
		}
		ImGui::End();
	}
}

void Engine::DrawEntityNode(Entity entity)
{
	const auto& registry = m_pScene->Registry();

	const auto& tag = registry.get< TagComponent >(entity).tag;
	auto& hierarchy = registry.get< TransformComponent >(entity).hierarchy;

	ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow;
	if (ImGui::SelectedEntity == entity)
		flags |= ImGuiTreeNodeFlags_Selected;
	if (hierarchy.firstChild == entt::null)
		flags |= ImGuiTreeNodeFlags_Leaf;

	ImGui::SetNextItemOpen(false, ImGuiCond_Once);
	auto id = entity.ID();
	bool bOpen = ImGui::TreeNodeEx((void*)(u64)id, flags, "%s", tag.c_str());

	if (ImGui::BeginDragDropSource())
	{
		ImGui::SetDragDropPayload("ENTITY_HIERARCHY", &id, sizeof(entt::entity));
		ImGui::Text("Moving Entity %d", (int)id);
		ImGui::EndDragDropSource();
	}

	if (ImGui::BeginDragDropTarget())
	{
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ENTITY_HIERARCHY"))
		{
			entt::entity droppedEntity = *(entt::entity*)payload->Data;
			entity.AttachChild(droppedEntity);
		}
		ImGui::EndDragDropTarget();
	}

	if (ImGui::IsItemClicked())
		ImGui::SelectedEntity = entity;

	if (bOpen)
	{
		entt::entity child = hierarchy.firstChild;
		while (child != entt::null)
		{
			DrawEntityNode(Entity{ m_pScene, child });
			child = registry.get< TransformComponent >(child).hierarchy.nextSibling;
		}
		ImGui::TreePop();
	}
}

void Engine::ProcessInput()
{
	if (glfwGetKey(m_pWindow->Handle(), GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS && glfwGetKey(m_pWindow->Handle(), GLFW_KEY_V))
	{
		if (s_RendererAPI != eRendererAPI::Vulkan)
		{
			m_bRunning = false;
			// NOTE. There is a bug which the window image is not properly updated 
			//       i.e. the last image output by d3d12 renderer remains intact.
			//       While the rendering-to-present process is executed normally(according to RenderDoc and PIX).
			//       It is hard to debug. So bypassed by window recreation for now.
			Release();
			Initialize(eRendererAPI::Vulkan);

			m_bRunning = true;
			m_RenderThread = std::thread(&Engine::RenderLoop, this);
		}
	}
	else if (glfwGetKey(m_pWindow->Handle(), GLFW_KEY_LEFT_SHIFT) && glfwGetKey(m_pWindow->Handle(), GLFW_KEY_D))
	{
		if (s_RendererAPI != eRendererAPI::D3D12)
		{
			m_bRunning = false;

			Release();
			Initialize(eRendererAPI::D3D12);

			m_bRunning = true;
			m_RenderThread = std::thread(&Engine::RenderLoop, this);
		}
	}
}

} // namespace baamboo
