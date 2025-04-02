#pragma once
#include "BaambooCore/RendererAPI.h"

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
	Engine() = default;
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
	virtual void Update(float dt);

	virtual bool InitWindow() { return false; }
	virtual bool LoadScene() { return false; }
	virtual void DrawUI();

	virtual void ProcessInput();

protected:
	class Window* m_pWindow = nullptr;
	class Scene*  m_pScene = nullptr;

	RendererAPI* m_pRendererBackend = nullptr;

	int  m_resizeWidth = 0;
	int  m_resizeHeight = 0;
	bool m_bWindowResized = false;

private:
	eRendererAPI m_eBackendAPI;
	double       m_runningTime = 0.0;

	friend void ImGui::DrawUI(baamboo::Engine& engine);
};

} // namespace baamboo