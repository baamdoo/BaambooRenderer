#include "BaambooPch.h"
#include "BaambooEngine.h"
#include "BaambooCore/Timer.h"
#include "BaambooCore/Window.h"
#include "BaambooCore/EngineCore.h"
#include "World/Scene.h"

#include <imgui/imgui.h>

namespace ImGui
{

ImGuiContext* InitUI()
{
	IMGUI_CHECKVERSION();
	auto pContext = ImGui::CreateContext();
	ImGui::StyleColorsDark();

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
	ImGui::Render();
}

void Destroy()
{
	if (ImGui::GetCurrentContext())
		ImGui::DestroyContext();
}

} // namespace ImGui

namespace baamboo
{

Engine::~Engine()
{
	RELEASE(m_pScene);
	RELEASE(m_pRendererBackend);
	RELEASE(m_pWindow);

	ImGui::Destroy();
}

void Engine::Initialize(eRendererAPI eApi)
{
	m_eBackendAPI = eApi;

	auto pImGuiContext = ImGui::InitUI();
	if (!InitWindow())
		throw std::runtime_error("Failed to initialize window!");
	if (!LoadRenderer(m_eBackendAPI, m_pWindow, pImGuiContext, &m_pRendererBackend))
		throw std::runtime_error("Failed to load backend!");
	if (!LoadScene())
		throw std::runtime_error("Failed to create world!");

}

i32 Engine::Run()
{
	m_runningTime = 0.0;

	Timer timer{};
	while (m_pWindow->PollEvent())
	{
		timer.Tick();

		auto dt = timer.GetDeltaSeconds();
		m_runningTime += dt;

		Update(static_cast<float>(dt));
	}

	return 0;
}

void Engine::Update(f32 dt)
{
	if (m_bWindowResized && m_resizeWidth > 0 && m_resizeHeight > 0)
	{
		// TODO: handle negative viewport
		m_pWindow->OnWindowResized(m_resizeWidth, m_resizeHeight);
		m_pRendererBackend->OnWindowResized(m_resizeWidth, m_resizeHeight);

		m_bWindowResized = false;
		m_resizeWidth = m_resizeHeight = 0;
	}

	ProcessInput();

	ImGui::DrawUI(*this);
	m_pRendererBackend->Render();
}

void Engine::DrawUI()
{
	ImGui::ShowDemoWindow();
	/*ImGui::Begin("Engine Profile");
	{
		ImGui::Text("Application average %.3f ms(frame: %.1f FPS)", 1000.f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
	}
	ImGui::End();*/
}

void Engine::ProcessInput()
{
	if (glfwGetKey(m_pWindow->Handle(), GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS && glfwGetKey(m_pWindow->Handle(), GLFW_KEY_V))
	{
		if (m_eBackendAPI != eRendererAPI::Vulkan)
		{
			RELEASE(m_pRendererBackend);
			m_eBackendAPI = eRendererAPI::Vulkan;

			if (!LoadRenderer(m_eBackendAPI, m_pWindow, ImGui::GetCurrentContext(), &m_pRendererBackend))
				throw std::runtime_error("Failed to load backend!");
		}
	}
	else if (glfwGetKey(m_pWindow->Handle(), GLFW_KEY_LEFT_SHIFT) && glfwGetKey(m_pWindow->Handle(), GLFW_KEY_D))
	{
		if (m_eBackendAPI != eRendererAPI::D3D12)
		{
			RELEASE(m_pRendererBackend);
			m_eBackendAPI = eRendererAPI::D3D12;

			if (!LoadRenderer(m_eBackendAPI, m_pWindow, ImGui::GetCurrentContext(), &m_pRendererBackend))
				throw std::runtime_error("Failed to load backend!");
		}
	}
}

} // namespace baamboo
