#include "BaambooPch.h"
#include "Window.h"

#include <imgui/backends/imgui_impl_glfw.h>

namespace
{

GLFWwindow* InitWindow(baamboo::WindowDescriptor& windowDesc)
{
	if (!glfwInit())
		return nullptr;
	glfwSetErrorCallback([](int error, const char* desc) 
		{
			printf("GLFW Error (%i): %s\n", error, desc);
		});


	// **
	// Set viewport
	// **
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

	GLFWmonitor* pMonitor = glfwGetPrimaryMonitor();
	i32 x = 0; i32 y = 0;
	i32 w = windowDesc.width; i32 h = windowDesc.height;
	if (windowDesc.bFullscreen)
		glfwGetMonitorWorkarea(pMonitor, &x, &y, &w, &h);


	// **
	// Create window
	// **
	GLFWwindow* window = glfwCreateWindow(w, h, windowDesc.title.c_str(), nullptr, nullptr);
	if (!window)
	{
		glfwTerminate();
		return nullptr;
	}

	if (windowDesc.bFullscreen)
		glfwSetWindowPos(window, x, y);
	glfwGetWindowSize(window, &w, &h);	

	windowDesc.width = w;
	windowDesc.height = h;

	return window;
}

}


namespace baamboo
{

Window::Window(const WindowDescriptor& desc)
	: m_Desc(desc)
{
	if ((m_Handle = InitWindow(m_Desc)) == nullptr)
		throw std::runtime_error("Failed to create window!");

	m_WinHandle = glfwGetWin32Window(m_Handle);

	ImGui_ImplGlfw_InitForOther(m_Handle, false);
}

Window::~Window()
{
	ImGui_ImplGlfw_Shutdown();

	glfwDestroyWindow(m_Handle);
	glfwTerminate();
}

bool Window::PollEvent()
{
	{
		glfwPollEvents();
	}

	return !glfwWindowShouldClose(m_Handle);
}

void Window::NewFrame()
{
	ImGui_ImplGlfw_NewFrame();
}

void Window::OnWindowResized(i32 width, i32 height)
{
	m_Desc.width  = width;
	m_Desc.height = height;
}

} // namespace baamboo