#pragma once
#include <functional>

enum class eRendererAPI
{
	D3D11,
	D3D12,
	Vulkan,
	OpenGL,
	Metal,
};

enum class eRendererType
{
	Forward,
	Deferred,
	Indirect,
};

struct RendererAPI
{
	virtual ~RendererAPI() = default;

	virtual void SetRendererType(eRendererType type) = 0;
	virtual void NewFrame() = 0;
	virtual void Render() = 0;

	virtual void OnWindowResized(int width, int height) = 0;
};