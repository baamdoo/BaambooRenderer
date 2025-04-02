#include "RendererPch.h"
#include "VkResourceManager.h"

namespace vk
{

ResourceManager::ResourceManager(RenderContext& context)
	: m_renderContext(context)
{
}

ResourceManager::~ResourceManager()
{
}

} // namespace vk