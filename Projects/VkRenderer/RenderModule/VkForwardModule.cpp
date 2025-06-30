#include "RendererPch.h"
#include "VkForwardModule.h"
#include "RenderDevice/VkResourceManager.h"
#include "RenderDevice/VkRenderPipeline.h"
#include "RenderDevice/VkCommandContext.h"
#include "RenderResource/VkShader.h"
#include "RenderResource/VkTexture.h"
#include "RenderResource/VkRenderTarget.h"
#include "RenderResource/VkSceneResource.h"

namespace vk
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

void ForwardModule::Resize(u32 width, u32 height, u32 depth)
{
	
}

} // namespace vk
