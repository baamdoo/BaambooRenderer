#pragma once
#include "BaambooCore/Common.h"

#include <functional>

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
	virtual void Render(SceneRenderView&& renderView) = 0;

	virtual void OnWindowResized(int width, int height) = 0;
};