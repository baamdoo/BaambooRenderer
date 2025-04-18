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

namespace baamboo
{
	struct SceneRenderView;
	template< typename T >
	class ThreadQueue;
}

struct RendererAPI
{
	virtual ~RendererAPI() = default;

	virtual void SetRendererType(eRendererType type) = 0;
	virtual void NewFrame() = 0;
	virtual void Render(const baamboo::SceneRenderView& renderView) = 0;

	virtual void OnWindowResized(int width, int height) = 0;
};