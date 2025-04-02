#pragma once
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_core.h>

//-------------------------------------------------------------------------
// Assertion
//-------------------------------------------------------------------------
#define VK_CHECK(value) ThrowIfFailed(value)
inline void ThrowIfFailed(VkResult result)
{
	if (!(result == VK_SUCCESS))
	{
		printf("CHECK() failed at %s:%i\n", __FILE__, __LINE__);

		__debugbreak();
	}
}


//-------------------------------------------------------------------------
// Resource Paths
//-------------------------------------------------------------------------
#define PIPELINE_PATH GetPipelinePath()
constexpr inline std::string GetPipelinePath()
{
	return "./../../Output/Pipeline/Vulkan/";
}

#define SPIRV_PATH GetSpirvPath()
constexpr inline std::string GetSpirvPath()
{
	return "./../../Output/Shader/spv/";
}


//-------------------------------------------------------------------------
// Pre-defined Values
//-------------------------------------------------------------------------
constexpr u32 MAX_BINDLESS_DESCRIPTOR_RESOURCE_SIZE = 1024;
enum : u8
{
	eDescriptorSet_Static = 0,
	eDescriptorSet_Push = 1,

	eNumDescriptorSet
};


//-------------------------------------------------------------------------
// Render Context
//-------------------------------------------------------------------------
#include "RenderDevice/VkRenderContext.h"