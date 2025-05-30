#pragma once
#include "Singleton.h"

#include <bitset>
#include <glfw/glfw3.h>

namespace baamboo
{

class Input : public Singleton< Input >
{
public:
	void EndFrame()
	{
		m_TransientKeyStates.reset();
		m_TransientMouseStates.reset();

		m_MouseWheel = 0.0f;
		m_MouseDeltaX = m_MouseDeltaY = 0.0f;
	}
	void UpdateKey(int keycode, bool bPressed)
	{
		m_TransientKeyStates.set(keycode, bPressed);
		m_PersistentKeyStates.set(keycode, bPressed);
	}
	void UpdateMouse(int button, bool bClicked)
	{
		m_TransientMouseStates.set(button, bClicked);
		m_PersistentMouseStates.set(button, bClicked);
	}
	void UpdateMouseWheel(float mouseWheel)
	{
		m_MouseWheel = mouseWheel;
	}
	void UpdateMousePosition(float x, float y)
	{
		m_MouseDeltaX = x - m_MousePositionX;
		m_MouseDeltaY = y - m_MousePositionY;

		m_MousePositionX = x;
		m_MousePositionY = y;
	}

	[[nodiscard]]
	bool IsKeyDown(int keycode) const { return m_TransientKeyStates.test(keycode); }
	[[nodiscard]]
	bool IsKeyPressed(int keycode) const { return m_PersistentKeyStates.test(keycode); }

	[[nodiscard]]
	bool IsMouseDown(int button) const { return m_TransientMouseStates.test(button); }
	[[nodiscard]]
	bool IsMousePressed(int button) const { return m_PersistentMouseStates.test(button); }

	[[nodiscard]]
	float GetMouseDeltaX() const { return m_MouseDeltaX; }
	[[nodiscard]]
	float GetMouseDeltaY() const { return m_MouseDeltaY; }
	[[nodiscard]]
	float GetMousePositionX() const { return m_MousePositionX; }
	[[nodiscard]]
	float GetMousePositionY() const { return m_MousePositionY; }
	[[nodiscard]]
	float GetMouseWheelDelta() const { return m_MouseWheel; }

private:
	std::bitset< GLFW_KEY_LAST > m_TransientKeyStates = {};
	std::bitset< GLFW_KEY_LAST > m_PersistentKeyStates = {};

	std::bitset< GLFW_MOUSE_BUTTON_LAST > m_TransientMouseStates = {};
	std::bitset< GLFW_MOUSE_BUTTON_LAST > m_PersistentMouseStates = {};

	float m_MouseDeltaX = 0.0f;
	float m_MouseDeltaY = 0.0f;
	float m_MousePositionX = 0.0f;
	float m_MousePositionY = 0.0f;
	float m_MouseWheel = 0.0f;
};

} // namespace baamboo