#pragma once

namespace vk
{

struct BarrierState
{
	constexpr BarrierState() = default;

	// Access + Stage only (for buffers — layout remains UNDEFINED)
	constexpr BarrierState(VkAccessFlags2 access_, VkPipelineStageFlags2 stage_)
		: access(access_), stage(stage_) {}

	// Full state (for textures)
	constexpr BarrierState(VkAccessFlags2 access_, VkPipelineStageFlags2 stage_, VkImageLayout layout_)
		: access(access_), stage(stage_), layout(layout_) {}

	bool operator==(const BarrierState& other) const { return access == other.access && stage == other.stage && layout == other.layout; }
	bool operator!=(const BarrierState& other) const { return !(*this == other); }

	VkAccessFlags2        access = 0;
	VkPipelineStageFlags2 stage  = 0;
	VkImageLayout         layout = VK_IMAGE_LAYOUT_UNDEFINED;
};


// =========================================================================
// BarrierState Presets
// =========================================================================
namespace BarrierStates
{

	inline constexpr BarrierState Undefined = {};

	// ---- Buffer states (no layout) ----
	inline constexpr BarrierState BufferTransferDest
	{
		VK_ACCESS_2_TRANSFER_WRITE_BIT,
		VK_PIPELINE_STAGE_2_TRANSFER_BIT
	};

	inline constexpr BarrierState BufferTransferSource
	{
		VK_ACCESS_2_TRANSFER_READ_BIT,
		VK_PIPELINE_STAGE_2_TRANSFER_BIT
	};

	inline constexpr BarrierState BufferComputeShaderRead
	{
		VK_ACCESS_2_SHADER_READ_BIT,
		VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT
	};

	inline constexpr BarrierState BufferVertexShaderRead
	{
		VK_ACCESS_2_SHADER_READ_BIT,
		VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT
	};

	inline constexpr BarrierState BufferPixelShaderRead
	{
		VK_ACCESS_2_SHADER_READ_BIT,
		VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT
	};

	inline constexpr BarrierState BufferTaskShaderRead
	{
		VK_ACCESS_2_SHADER_READ_BIT,
		VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT
	};

	inline constexpr BarrierState BufferMeshShaderRead
	{
		VK_ACCESS_2_SHADER_READ_BIT,
		VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT
	};

	inline constexpr BarrierState BufferStorageWrite
	{
		VK_ACCESS_2_SHADER_WRITE_BIT,
		VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT
	};

	inline constexpr BarrierState BufferStorageReadWrite
	{
		VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT,
		VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT
	};

	inline constexpr BarrierState BufferIndirectArgument
	{
		VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT,
		VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT
	};

	inline constexpr BarrierState BufferIndexRead
	{
		VK_ACCESS_2_INDEX_READ_BIT,
		VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT
	};

	inline constexpr BarrierState BufferVertexRead
	{
		VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT,
		VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT
	};

	inline constexpr BarrierState BufferUniformRead
	{
		VK_ACCESS_2_UNIFORM_READ_BIT,
		VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT
	};

	inline constexpr BarrierState BufferHostWrite
	{
		VK_ACCESS_2_HOST_WRITE_BIT,
		VK_PIPELINE_STAGE_2_HOST_BIT
	};

	// ---- Texture states ----
	inline constexpr BarrierState ColorAttachmentWrite
	{
		VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
		VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
	};

	inline constexpr BarrierState DepthStencilWrite
	{
		VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
		VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
		VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
	};

	inline constexpr BarrierState DepthStencilRead
	{
		VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
		VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
		VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
	};

	inline constexpr BarrierState PixelShaderReadOnly
	{
		VK_ACCESS_2_SHADER_READ_BIT,
		VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
	};

	inline constexpr BarrierState ComputeShaderReadOnly
	{
		VK_ACCESS_2_SHADER_READ_BIT,
		VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
	};

	inline constexpr BarrierState TaskShaderReadOnly
	{
		VK_ACCESS_2_SHADER_READ_BIT,
		VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
	};

	inline constexpr BarrierState MeshShaderReadOnly
	{
		VK_ACCESS_2_SHADER_READ_BIT,
		VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
	};

	inline constexpr BarrierState StorageImage
	{
		VK_ACCESS_2_SHADER_WRITE_BIT,
		VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
		VK_IMAGE_LAYOUT_GENERAL
	};

	inline constexpr BarrierState TransferDest
	{
		VK_ACCESS_2_TRANSFER_WRITE_BIT,
		VK_PIPELINE_STAGE_2_TRANSFER_BIT,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
	};

	inline constexpr BarrierState TransferSource
	{
		VK_ACCESS_2_TRANSFER_READ_BIT,
		VK_PIPELINE_STAGE_2_TRANSFER_BIT,
		VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
	};

	inline constexpr BarrierState Present
	{
		0,
		VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
		VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
	};


	// ---- Acceleration Structure ----
	inline constexpr BarrierState AccelerationStructureBuild
	{
		VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
		VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR
	};

	inline constexpr BarrierState AccelerationStructureRead
	{
		VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR,
		VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR
	};

};

} // namespace vk