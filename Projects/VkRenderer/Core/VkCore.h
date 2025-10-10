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


//-------------------------------------------------------------------------
// Render Resource
//-------------------------------------------------------------------------
#include "RenderCommon/RenderResources.h"

#define VK_BUFFER_USAGE_FLAGS(flags) ConvertToVkBufferUsageFlags(flags)
static VkBufferUsageFlagBits ConvertToVkBufferUsageBits(u32 bit)
{
	using namespace render;
	switch (bit)
	{
		case eBufferUsage_TransferSource :
			return VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		case eBufferUsage_TransferDest :
			return VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		case eBufferUsage_UniformTexel :
			return VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT;
		case eBufferUsage_StorageTexel :
			return VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT;
		case eBufferUsage_Uniform :
			return VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
		case eBufferUsage_Storage :
			return VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
		case eBufferUsage_Index :
			return VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
		case eBufferUsage_Vertex :
			return VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
		case eBufferUsage_Indirect :
			return VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
		case eBufferUsage_ShaderDeviceAddress :
			return VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
	}

	return VK_BUFFER_USAGE_FLAG_BITS_MAX_ENUM;
}

static VkBufferUsageFlags ConvertToVkBufferUsageFlags(u64 flags)
{
	VkBufferUsageFlags vkFlags = 0;

	u32 i = 0;
	while (flags)
	{
		if (flags & (1LL << i))
		{
			vkFlags |= ConvertToVkBufferUsageBits((1LL << i));
			flags &= ~(1LL << i);
		}

		++i;
	}

	return vkFlags;
}

#define VK_IMAGE_USAGE_FLAGS(flags) ConvertToVkImageUsageFlags(flags)
static VkImageUsageFlagBits ConvertToVkImageUsageBits(u32 bit)
{
	using namespace render;
	switch (bit)
	{
		case eTextureUsage_TransferSource :
			return VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
		case eTextureUsage_TransferDest :
			return VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		case eTextureUsage_Sample :
			return VK_IMAGE_USAGE_SAMPLED_BIT;
		case eTextureUsage_Storage :
			return VK_IMAGE_USAGE_STORAGE_BIT;
		case eTextureUsage_ColorAttachment :
			return VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
		case eTextureUsage_DepthStencilAttachment :
			return VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
	}

	return VK_IMAGE_USAGE_FLAG_BITS_MAX_ENUM;
}

static VkImageUsageFlags ConvertToVkImageUsageFlags(u64 flags)
{
	VkImageUsageFlags vkFlags = 0;

	u32 i = 0;
	while (flags)
	{
		if (flags & (1LL << i))
		{
			vkFlags |= ConvertToVkImageUsageBits((1LL << i));
			flags &= ~(1LL << i);
		}

		++i;
	}

	return vkFlags;
}

#define VK_LAYOUT(layout) ConvertToVkLayout(layout)
static VkImageLayout ConvertToVkLayout(render::eTextureLayout layout)
{
	using namespace render;
	switch (layout)
	{
	case eTextureLayout::Undefined              : return VK_IMAGE_LAYOUT_UNDEFINED;
	case eTextureLayout::General                : return VK_IMAGE_LAYOUT_GENERAL;
	case eTextureLayout::ColorAttachment        : return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	case eTextureLayout::DepthStencilAttachment : return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	case eTextureLayout::DepthStencilReadOnly   : return VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
	case eTextureLayout::ShaderReadOnly         : return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	case eTextureLayout::TransferSource         : return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	case eTextureLayout::TransferDest           : return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	case eTextureLayout::Present                : return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
		
	default:
		assert(false && "Invalid image layout!"); break;
	}

	return VK_IMAGE_LAYOUT_UNDEFINED;
}

#define VK_FORMAT(format) ConvertToVkFormat(format)
static VkFormat ConvertToVkFormat(render::eFormat format)
{
	using namespace render;
	switch (format)
	{
	case eFormat::UNKNOWN              : return VK_FORMAT_UNDEFINED;

	case eFormat::RGBA32_FLOAT         : return VK_FORMAT_R32G32B32A32_SFLOAT;
	case eFormat::RGBA32_UINT          : return VK_FORMAT_R32G32B32A32_UINT;
	case eFormat::RGBA32_SINT          : return VK_FORMAT_R32G32B32A32_SINT;
	case eFormat::RGB32_UINT           : return VK_FORMAT_R32G32B32_UINT;
	case eFormat::RGB32_SINT           : return VK_FORMAT_R32G32B32_SINT;
	case eFormat::RG32_FLOAT           : return VK_FORMAT_R32G32_SFLOAT;
	case eFormat::RG32_UINT            : return VK_FORMAT_R32G32_UINT;
	case eFormat::RG32_SINT            : return VK_FORMAT_R32G32_SINT;
	case eFormat::R32_FLOAT            : return VK_FORMAT_R32_SFLOAT;
	case eFormat::R32_UINT             : return VK_FORMAT_R32_UINT;
	case eFormat::R32_SINT             : return VK_FORMAT_R32_SINT;

	case eFormat::RGBA16_FLOAT : return VK_FORMAT_R16G16B16A16_SFLOAT;
	case eFormat::RGBA16_UNORM : return VK_FORMAT_R16G16B16A16_UNORM;
	case eFormat::RGBA16_UINT  : return VK_FORMAT_R16G16B16A16_UINT;
	case eFormat::RGBA16_SNORM : return VK_FORMAT_R16G16B16A16_SNORM;
	case eFormat::RGBA16_SINT  : return VK_FORMAT_R16G16B16A16_SINT;
	case eFormat::RG16_FLOAT   : return VK_FORMAT_R16G16_SFLOAT;
	case eFormat::RG16_UNORM   : return VK_FORMAT_R16G16_UNORM;
	case eFormat::RG16_SNORM   : return VK_FORMAT_R16G16_SNORM;
	case eFormat::RG16_UINT    : return VK_FORMAT_R16G16_UINT;
	case eFormat::RG16_SINT    : return VK_FORMAT_R16G16_SINT;
	case eFormat::R16_FLOAT    : return VK_FORMAT_R16_SFLOAT;
	case eFormat::R16_UNORM    : return VK_FORMAT_R16_UNORM;
	case eFormat::R16_SNORM    : return VK_FORMAT_R16_SNORM;
	case eFormat::R16_UINT     : return VK_FORMAT_R16_UINT;
	case eFormat::R16_SINT     : return VK_FORMAT_R16_SINT;

	case eFormat::RGBA8_UNORM  : return VK_FORMAT_R8G8B8A8_UNORM;
	case eFormat::RGBA8_SNORM  : return VK_FORMAT_R8G8B8A8_SNORM;
	case eFormat::RGBA8_UINT   : return VK_FORMAT_R8G8B8A8_UINT;
	case eFormat::RGBA8_SINT   : return VK_FORMAT_R8G8B8A8_SINT;
	case eFormat::RGBA8_SRGB   : return VK_FORMAT_R8G8B8A8_SRGB;
	case eFormat::RGB8_UNORM   : return VK_FORMAT_R8G8B8_UNORM;
	case eFormat::RGB8_SNORM   : return VK_FORMAT_R8G8B8_SNORM;
	case eFormat::RGB8_USCALED : return VK_FORMAT_R8G8B8_USCALED;
	case eFormat::RGB8_SSCALED : return VK_FORMAT_R8G8B8_SSCALED;
	case eFormat::RGB8_UINT    : return VK_FORMAT_R8G8B8_UINT;
	case eFormat::RGB8_SINT    : return VK_FORMAT_R8G8B8_SINT;
	case eFormat::RGB8_SRGB    : return VK_FORMAT_R8G8B8_SRGB;
	case eFormat::RG8_UNORM    : return VK_FORMAT_R8G8_UNORM;
	case eFormat::RG8_SNORM    : return VK_FORMAT_R8G8_SNORM;
	case eFormat::RG8_USCALED  : return VK_FORMAT_R8G8_USCALED;
	case eFormat::RG8_SSCALED  : return VK_FORMAT_R8G8_SSCALED;
	case eFormat::RG8_UINT     : return VK_FORMAT_R8G8_UINT;
	case eFormat::RG8_SINT     : return VK_FORMAT_R8G8_SINT;
	case eFormat::RG8_SRGB     : return VK_FORMAT_R8G8_SRGB;
	case eFormat::R8_UNORM     : return VK_FORMAT_R8_UNORM;
	case eFormat::R8_SNORM     : return VK_FORMAT_R8_SNORM;
	case eFormat::R8_UINT      : return VK_FORMAT_R8_UINT;
	case eFormat::R8_SINT      : return VK_FORMAT_R8_SINT;
	case eFormat::A8_UNORM     : return VK_FORMAT_A8_UNORM;

	case eFormat::RG11B10_UFLOAT : return VK_FORMAT_B10G11R11_UFLOAT_PACK32;

	case eFormat::D32_FLOAT            : return VK_FORMAT_D32_SFLOAT;
	case eFormat::D24_UNORM_S8_UINT    : return VK_FORMAT_D32_SFLOAT_S8_UINT;
	case eFormat::D16_UNORM            : return VK_FORMAT_D16_UNORM;

	default:
		assert(false && "Invalid format!"); break;
	}

	return VK_FORMAT_UNDEFINED;
}

#define VK_SHADER_STAGE(stage) ConvertToVkShaderStage(stage)
static VkShaderStageFlagBits ConvertToVkShaderStage(render::eShaderStage stage)
{
	using namespace render;
	switch (stage)
	{
	case eShaderStage::Vertex      : return VK_SHADER_STAGE_VERTEX_BIT;
	case eShaderStage::Hull        : return VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
	case eShaderStage::Domain      : return VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
	case eShaderStage::Geometry    : return VK_SHADER_STAGE_GEOMETRY_BIT;
	case eShaderStage::Fragment    : return VK_SHADER_STAGE_FRAGMENT_BIT;
	case eShaderStage::Compute     : return VK_SHADER_STAGE_COMPUTE_BIT;
	case eShaderStage::AllGraphics : return VK_SHADER_STAGE_ALL_GRAPHICS;
	case eShaderStage::AllStage    : return VK_SHADER_STAGE_ALL;

	case eShaderStage::RayGeneration : return VK_SHADER_STAGE_RAYGEN_BIT_KHR;
	case eShaderStage::AnyHit        : return VK_SHADER_STAGE_ANY_HIT_BIT_KHR;
	case eShaderStage::ClosestHit    : return VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
	case eShaderStage::Miss          : return VK_SHADER_STAGE_MISS_BIT_KHR;
	case eShaderStage::Interaction   : return VK_SHADER_STAGE_INTERSECTION_BIT_KHR;
	case eShaderStage::Callable      : return VK_SHADER_STAGE_CALLABLE_BIT_KHR;

	case eShaderStage::Task : return VK_SHADER_STAGE_TASK_BIT_EXT;
	case eShaderStage::Mesh : return VK_SHADER_STAGE_MESH_BIT_EXT;

	default:
		assert(false && "Invalid shader stage bit!"); break;
	}

	return VkShaderStageFlagBits(0);
}