#pragma once
#include "Defines.h"
#include "Pointer.hpp"
#include "MathTypes.h"

struct ImGuiContext;
struct SceneRenderView;
namespace baamboo
{
    class Window;
    class RenderGraph;
}

enum class eRendererAPI
{
	D3D11,
	D3D12,
	Vulkan,
	OpenGL,
	Metal,
};

enum
{
	eTextureIndex_Invalid      = 0xffffffff,
	eTextureIndex_DefaultWhite = 0,
	eTextureIndex_DefaultBlack = 1,
};

namespace render
{

// Forward declarations
class CommandContext;
class GraphicsPipeline;
class ComputePipeline;
class Texture;
class Buffer;
class RenderTarget;
class Sampler;
class RenderDevice;

class Renderer 
{
public:
    virtual ~Renderer() = default;

    virtual void NewFrame() = 0;

	virtual Arc< render::CommandContext > BeginFrame() = 0;
	virtual void EndFrame(Arc< render::CommandContext >&& context, Arc< render::Texture > scene, bool bDrawUI) = 0;

	virtual void WaitIdle() = 0;
    virtual void Resize(i32 width, i32 height) = 0;

    virtual RenderDevice* GetDevice() = 0;
    virtual eRendererAPI GetAPIType() const = 0;
};

}

static eRendererAPI s_RendererAPI;