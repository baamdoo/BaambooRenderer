#pragma once
struct SceneRenderView;

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

enum
{
	eTextureIndex_Invalid = 0xffffffff,
	eTextureIndex_DefaultWhite = 0,
	eTextureIndex_DefaultBlack = 1,
};

struct RendererAPI
{
	virtual ~RendererAPI() = default;

	virtual void SetRendererType(eRendererType type) = 0;
	virtual void NewFrame() = 0;
	virtual void Render(const SceneRenderView& renderView) = 0;

	virtual void OnWindowResized(int width, int height) = 0;
};