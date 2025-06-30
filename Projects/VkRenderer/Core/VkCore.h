#pragma once

//-------------------------------------------------------------------------
// Vulkan Includes
//-------------------------------------------------------------------------
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_core.h>
#include <vk_mem_alloc.h>


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
#include <filesystem>
namespace fs = std::filesystem;

#define PIPELINE_PATH GetPipelinePath()
inline fs::path GetPipelinePath()
{
	return "Output/Pipeline/Vulkan/";
}

#define SPIRV_PATH GetSpirvPath()
inline fs::path GetSpirvPath()
{
	return "Output/Shader/spv/";
}


//-------------------------------------------------------------------------
// Pre-defined Values
//-------------------------------------------------------------------------
constexpr u32 MAX_FRAMES_IN_FLIGHT = 3;
constexpr u32 DEFAULT_DESCRIPTOR_POOL_SIZE = 1024;
constexpr u32 MAX_BINDLESS_DESCRIPTOR_RESOURCE_COUNT = 1024;
enum eDescriptorSetIndexType : u8
{
	eDescriptorSet_Static = 0,
	eDescriptorSet_Push = 1,
	eNumPreallocatedSetIndex,
};


//-------------------------------------------------------------------------
// Shader Types
//-------------------------------------------------------------------------
struct IndirectDrawData
{
	u32 indexCount;
	u32 instanceCount;
	u32 firstIndex;
	i32 vertexOffset;
	u32 firstInstance;

	u32	materialIndex;
	u32 transformID;
	u32 transformCount;

	u32 boneTransformID;
	u32 bSkinning;

	float2 padding0;
};


//-------------------------------------------------------------------------
// Render Device
//-------------------------------------------------------------------------
#include "RenderDevice/VkRenderDevice.h"