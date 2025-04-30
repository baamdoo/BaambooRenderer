#pragma once
#include "BaambooCore/Common.h"

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
}

enum
{
	eTextureIndex_Invalid = -1,
	eTextureIndex_DefaultWhite = 0,
	eTextureIndex_DefaultBlack = 1,
};

struct VertexHandle
{
	u32 vb;
	u32 vOffset;
	u32 vCount;
};

struct IndexHandle
{
	u32 ib;
	u32 iOffset;
	u32 iCount;
};

using TextureHandle = u32;

struct ResourceManagerAPI
{
	virtual ~ResourceManagerAPI() = default;

	virtual VertexHandle CreateVertexBuffer(std::wstring_view name, u32 numVertices, u64 elementSizeInBytes, void* data) = 0;
	virtual IndexHandle CreateIndexBuffer(std::wstring_view name, u32 numIndices, u64 elementSizeInBytes, void* data) = 0;
	virtual TextureHandle CreateTexture(std::string_view filepath, bool bGenerateMips) = 0;
};

struct RendererAPI
{
	virtual ~RendererAPI() = default;

	virtual void SetRendererType(eRendererType type) = 0;
	virtual void NewFrame() = 0;
	virtual void Render(const baamboo::SceneRenderView& renderView) = 0;

	virtual void OnWindowResized(int width, int height) = 0;

	[[nodiscard]]
	virtual ResourceManagerAPI& GetResourceManager() = 0;
};