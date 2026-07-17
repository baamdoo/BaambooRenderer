#pragma once
#include "Singleton.h"

#include <bitset>
#include <glfw/glfw3.h>

namespace baamboo
{

class Input : public Singleton< Input >
{
public:
	void Reset()
	{
		m_TransientKeyStates.reset();
		m_PersistentKeyStates.reset();

		m_TransientMouseStates.reset();
		m_PersistentMouseStates.reset();

		m_MouseWheel = 0.0f;
		m_MouseDeltaX = m_MouseDeltaY = 0.0f;
	}

	void EndFrame()
	{
		m_TransientKeyStates.reset();
		m_TransientMouseStates.reset();

		m_MouseWheel  = 0.0f;
		m_MouseDeltaX = m_MouseDeltaY = 0.0f;
	}
	void UpdateKey(int keycode, bool bPressed)
	{
		if (!IsValidKey(keycode))
			return;

		const size_t index = static_cast<size_t>(keycode);
		m_TransientKeyStates.set(index, bPressed);
		m_PersistentKeyStates.set(index, bPressed);
	}
	void UpdateMouse(int button, bool bClicked)
	{
		if (!IsValidMouseButton(button))
			return;

		const size_t index = static_cast<size_t>(button);
		m_TransientMouseStates.set(index, bClicked);
		m_PersistentMouseStates.set(index, bClicked);
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
	bool IsKeyDown(int keycode) const { return IsValidKey(keycode) && m_TransientKeyStates.test(static_cast<size_t>(keycode)); }
	[[nodiscard]]
	bool IsKeyPressed(int keycode) const { return IsValidKey(keycode) && m_PersistentKeyStates.test(static_cast<size_t>(keycode)); }

	[[nodiscard]]
	bool IsMouseDown(int button) const { return IsValidMouseButton(button) && m_TransientMouseStates.test(static_cast<size_t>(button)); }
	[[nodiscard]]
	bool IsMousePressed(int button) const { return IsValidMouseButton(button) && m_PersistentMouseStates.test(static_cast<size_t>(button)); }

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
	static constexpr bool IsValidKey(int keycode) { return keycode >= 0 && keycode <= GLFW_KEY_LAST; }
	static constexpr bool IsValidMouseButton(int button) { return button >= 0 && button <= GLFW_MOUSE_BUTTON_LAST; }

	std::bitset< GLFW_KEY_LAST + 1 > m_TransientKeyStates = {};
	std::bitset< GLFW_KEY_LAST + 1 > m_PersistentKeyStates = {};

	std::bitset< GLFW_MOUSE_BUTTON_LAST + 1 > m_TransientMouseStates = {};
	std::bitset< GLFW_MOUSE_BUTTON_LAST + 1 > m_PersistentMouseStates = {};

	float m_MouseDeltaX    = 0.0f;
	float m_MouseDeltaY    = 0.0f;
	float m_MousePositionX = 0.0f;
	float m_MousePositionY = 0.0f;
	float m_MouseWheel     = 0.0f;
};

} // namespace baamboo