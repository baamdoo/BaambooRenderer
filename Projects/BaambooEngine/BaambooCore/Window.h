#pragma once
#if defined(_WIN32)
	#define GLFW_EXPOSE_NATIVE_WIN32
#endif
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

namespace baamboo
{

class IEventCallback
{
public:
	virtual ~IEventCallback() = default;
};

template< typename TReturn, typename... TArgs >
class EventCallback : public IEventCallback
{
public:
	EventCallback(std::function< TReturn(TArgs...) > func) : m_func(func) {}
	~EventCallback() = default;

	TReturn operator()(TArgs&&... args) {
		return std::invoke(m_func, std::forward< TArgs >(args)...);
	}

private:
	std::function< TReturn(TArgs...) > m_func;
};

struct WindowDescriptor
{
	std::string title;

	i32  width = 1280;
	i32  height = 720;
	u32  numDesiredImages = 2;
	bool bFullscreen = false;
	bool bVSync = false;
};

class Window
{
public:
	explicit Window(const WindowDescriptor& desc);
	Window(const Window&) = delete;
	~Window();

	[[nodiscard]]
	bool PollEvent();
	void NewFrame();

	void OnWindowResized(i32 width, i32 height);

	// template< typename TReturn, typename ...TArgs >
	// void AddCallback(std::wstring_view eventName, EventCallback< TReturn, TArgs... >&& callback)
	// {
	// 	auto pCallback = new EventCallback< TReturn, TArgs... >(std::move(callback));
	// 	m_callbacks.insert(std::make_pair(eventName, std::move(pCallback)));
	// }

	void SetKeyCallback(GLFWkeyfun&& func) { glfwSetKeyCallback(m_Handle, std::move(func)); }
	void SetMouseButtonCallback(GLFWmousebuttonfun&& func) { glfwSetMouseButtonCallback(m_Handle, std::move(func)); }
	void SetMouseMoveCallback(GLFWcursorposfun&& func) { glfwSetCursorPosCallback(m_Handle, std::move(func)); }
	void SetMouseWheelCallback(GLFWcursorposfun&& func) { glfwSetScrollCallback(m_Handle, std::move(func)); }
	void SetResizeCallback(GLFWwindowsizefun&& func) { glfwSetWindowSizeCallback(m_Handle, std::move(func)); }
	void SetIconifyCallback(GLFWwindowiconifyfun&& func) { glfwSetWindowIconifyCallback(m_Handle, std::move(func)); }
	void SetCharCallback(GLFWcharfun&& func) { glfwSetCharCallback(m_Handle, std::move(func)); }

	[[nodiscard]]
	inline GLFWwindow* Handle() const noexcept { return m_Handle; }
	[[nodiscard]]
	inline HWND WinHandle() const noexcept { return m_WinHandle; }

	[[nodiscard]]
	inline i32 Width() const { return m_Desc.width; }
	[[nodiscard]]
	inline i32 Height() const { return m_Desc.height; }
	[[nodiscard]]
	inline bool Minimized() const { return m_Desc.width == 0 || m_Desc.height == 0; }
	[[nodiscard]]
	inline const WindowDescriptor& Desc() const { return m_Desc; }

private:
	WindowDescriptor m_Desc;

	GLFWwindow* m_Handle = nullptr;
	HWND        m_WinHandle = nullptr;

	// std::unordered_map< std::wstring, IEventCallback* > m_callbacks;
};

}