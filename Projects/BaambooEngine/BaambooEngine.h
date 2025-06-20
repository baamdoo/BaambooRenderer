#pragma once
#include "ThreadQueue.hpp"
#include "BaambooCore/Timer.h"
#include "BaambooCore/BackendAPI.h"
#include "BaambooScene/Scene.h"
#include "BaambooScene/Camera.h"

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
	RendererAPI* GetRenderer() const { return m_pRendererBackend; }
	[[nodiscard]]
	class Window* GetWindow() const { return m_pWindow; }

	[[nodiscard]]
	eRendererAPI BackendAPI() const { return m_eBackendAPI; }

protected:
	virtual void Release();

	virtual void Update(float dt);
	virtual void GameLoop(float dt);
	virtual void RenderLoop();

	virtual bool InitWindow() { return false; }
	virtual bool LoadScene() { return false; }

	virtual void DrawUI();
	virtual void DrawEntityNode(Entity entity);

	virtual void ProcessInput();

protected:
	class Window* m_pWindow = nullptr;
	class Scene*  m_pScene = nullptr;
	RendererAPI*  m_pRendererBackend = nullptr;
	EditorCamera* m_pCamera = nullptr;

	int  m_ResizeWidth = -1;
	int  m_ResizeHeight = -1;
	bool m_bWindowResized = false;

	eRendererAPI m_eBackendAPI;

private:
	double m_RunningTime = 0.0;

	std::thread                    m_RenderThread;
	ThreadQueue< SceneRenderView > m_RenderViewQueue;
	std::atomic_bool               m_bRunning;

	Timer m_GameTimer   = {};
	Timer m_RenderTimer = {};


	// mutex for sync between writing entity-components data in render-thread and reading(view-each) in game-thread
	std::mutex m_ImGuiMutex;
	fs::path   m_CurrentDirectory;
	friend void ImGui::DrawUI(baamboo::Engine& engine);
};

} // namespace baamboo