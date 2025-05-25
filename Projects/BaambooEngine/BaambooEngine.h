#pragma once
#include "BaambooCore/BackendAPI.h"
#include "BaambooCore/ThreadQueue.hpp"
#include "Scene/Scene.h"
#include "Scene/Camera.h"

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
	void Release();

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

	int  m_resizeWidth = -1;
	int  m_resizeHeight = -1;
	bool m_bWindowResized = false;

	eRendererAPI m_eBackendAPI;

private:
	double       m_runningTime = 0.0;

	std::thread                    m_renderThread;
	ThreadQueue< SceneRenderView > m_renderViewQueue;
	std::atomic_bool               m_bRunning;

	std::mutex m_imguiMutex; // mutex for sync between writing entity-components data in render-thread and reading(view-each) in game-thread
	fs::path m_currentDirectory;
	friend void ImGui::DrawUI(baamboo::Engine& engine);
};

} // namespace baamboo