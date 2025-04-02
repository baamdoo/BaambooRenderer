#include "ExampleApp.h"
#include "BaambooCore/Window.h"
#include "World/Entity.h"
#include "BaambooCore/Common.h"

#include <imgui/backends/imgui_impl_glfw.h>

using namespace baamboo;

void ExampleApp::Update(float dt)
{
	Super::Update(dt);
}

bool ExampleApp::InitWindow()
{
	// **
	// Create window
	// **
	WindowDescriptor windowDesc = { .numDesiredImages = 3 };
	m_pWindow = new Window(windowDesc);


	// **
	// Set callbacks
	// **
	glfwSetWindowUserPointer(m_pWindow->Handle(), this);
	m_pWindow->SetKeyCallback([](GLFWwindow* window, i32 key, i32 scancode, i32 action, i32 mods)
		{
			ImGui_ImplGlfw_KeyCallback(window, key, scancode, action, mods);

			ImGuiIO& io = ImGui::GetIO();

			if (!io.WantCaptureKeyboard)
			{
				switch (key)
				{
				case GLFW_KEY_ESCAPE:
					if (action == GLFW_PRESS)
						glfwSetWindowShouldClose(window, GLFW_TRUE);
					break;

				default:
					break;
				}
			}
		});

	m_pWindow->SetMouseButtonCallback([](GLFWwindow* window, i32 button, i32 action, i32 mods)
		{
			ImGui_ImplGlfw_MouseButtonCallback(window, button, action, mods);

			ImGuiIO& io = ImGui::GetIO();
			if (!io.WantCaptureMouse)
			{
				printf("MouseClicked on scene!\n");
			}
			else
			{
				printf("MouseClicked on ui!\n");
			}
		});

	m_pWindow->SetMouseMoveCallback([](GLFWwindow* window, double xpos, double ypos)
		{
			ImGui_ImplGlfw_CursorPosCallback(window, xpos, ypos);

			ImGuiIO& io = ImGui::GetIO();
			if (!io.WantCaptureMouse)
			{
				printf("x: %lf, y: %lf\n", xpos, ypos);
			}
		});

	m_pWindow->SetResizeCallback([](GLFWwindow* window, i32 width, i32 height)
		{
			auto app = reinterpret_cast<ExampleApp*>(glfwGetWindowUserPointer(window));
			if (app)
			{
				app->m_bWindowResized = true;
				app->m_resizeWidth = width;
				app->m_resizeHeight = height;
			}
		});

	return true;
}

bool ExampleApp::LoadScene()
{
	m_pScene = new Scene("ExampleScene");
	auto entity0 = m_pScene->CreateEntity("Test0");
	m_pScene->RemoveEntity(entity0);

	return true;
}

void ExampleApp::DrawUI()
{
	Super::DrawUI();


}
