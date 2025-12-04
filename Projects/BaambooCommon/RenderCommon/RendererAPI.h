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
	D3D11  = 0,
	D3D12  = 1,
	Vulkan = 2,
	OpenGL = 3,
	Metal  = 4,
};
static mat4 ApplyRhiNDC(const mat4& mProj_, eRendererAPI api)
{
	mat4 mProj = mProj_;
	mProj[1][1] *= api == eRendererAPI::Vulkan ? -1.0f : 1.0f;
	return mProj;
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