#include "RendererPch.h"
#include "Dx12ForwardModule.h"
#include "RenderDevice/Dx12CommandContext.h"
#include "RenderDevice/Dx12RootSignature.h"
#include "RenderDevice/Dx12RenderPipeline.h"
#include "RenderResource/Dx12Shader.h"
#include "RenderResource/Dx12Buffer.h"
#include "RenderResource/Dx12Texture.h"
#include "RenderResource/Dx12RenderTarget.h"
#include "RenderResource/Dx12SceneResource.h"

namespace dx12
{

ForwardModule::ForwardModule(RenderDevice& device)
	: Super(device)
{
}

ForwardModule::~ForwardModule()
{
}

void ForwardModule::Apply(CommandContext& context)
{
}

} // namespace dx12