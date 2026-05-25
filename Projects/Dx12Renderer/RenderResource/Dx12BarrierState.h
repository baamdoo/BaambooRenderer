#pragma once

namespace dx12
{

struct BarrierState
{
	constexpr BarrierState() {}

	// Sync + Access only (for buffers - layout remains UNDEFINED)
	constexpr BarrierState(D3D12_BARRIER_SYNC sync, D3D12_BARRIER_ACCESS access)
		: Sync(sync), Access(access) {
	}

	// Full state (for textures)
	constexpr BarrierState(D3D12_BARRIER_SYNC sync, D3D12_BARRIER_ACCESS access, D3D12_BARRIER_LAYOUT layout)
		: Sync(sync), Access(access), Layout(layout) {
	}

	bool operator==(const BarrierState& other) const { return Sync == other.Sync && Access == other.Access && Layout == other.Layout; }
	bool operator!=(const BarrierState& other) const { return !(*this == other); }

	D3D12_BARRIER_SYNC   Sync   = D3D12_BARRIER_SYNC_NONE;
	D3D12_BARRIER_ACCESS Access = D3D12_BARRIER_ACCESS_NO_ACCESS;
	D3D12_BARRIER_LAYOUT Layout = D3D12_BARRIER_LAYOUT_UNDEFINED;
};

// =========================================================================
// BarrierState Presets
// =========================================================================
namespace BarrierStates
{

	// Common / undefined
	inline constexpr BarrierState Undefined = {};

	// ---- Texture layouts ----
	inline constexpr BarrierState Common
	{
		D3D12_BARRIER_SYNC_NONE,
		D3D12_BARRIER_ACCESS_NO_ACCESS,
		D3D12_BARRIER_LAYOUT_COMMON
	};

	inline constexpr BarrierState RenderTarget
	{
		D3D12_BARRIER_SYNC_RENDER_TARGET,
		D3D12_BARRIER_ACCESS_RENDER_TARGET,
		D3D12_BARRIER_LAYOUT_RENDER_TARGET
	};

	inline constexpr BarrierState DepthStencilWrite
	{
		D3D12_BARRIER_SYNC_DEPTH_STENCIL,
		D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE,
		D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE
	};

	inline constexpr BarrierState DepthStencilRead
	{
		D3D12_BARRIER_SYNC_DEPTH_STENCIL,
		D3D12_BARRIER_ACCESS_DEPTH_STENCIL_READ,
		D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_READ
	};

	inline constexpr BarrierState ShaderResource
	{
		D3D12_BARRIER_SYNC_ALL_SHADING,
		D3D12_BARRIER_ACCESS_SHADER_RESOURCE,
		D3D12_BARRIER_LAYOUT_SHADER_RESOURCE
	};

	inline constexpr BarrierState PixelShaderResource
	{
		D3D12_BARRIER_SYNC_PIXEL_SHADING,
		D3D12_BARRIER_ACCESS_SHADER_RESOURCE,
		D3D12_BARRIER_LAYOUT_SHADER_RESOURCE
	};

	inline constexpr BarrierState NonPixelShaderResource
	{
		D3D12_BARRIER_SYNC_NON_PIXEL_SHADING,
		D3D12_BARRIER_ACCESS_SHADER_RESOURCE,
		D3D12_BARRIER_LAYOUT_SHADER_RESOURCE
	};

	inline constexpr BarrierState UnorderedAccess
	{
		D3D12_BARRIER_SYNC_COMPUTE_SHADING,
		D3D12_BARRIER_ACCESS_UNORDERED_ACCESS,
		D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS
	};

	inline constexpr BarrierState CopyDest
	{
		D3D12_BARRIER_SYNC_COPY,
		D3D12_BARRIER_ACCESS_COPY_DEST,
		D3D12_BARRIER_LAYOUT_COPY_DEST
	};

	inline constexpr BarrierState CopySource
	{
		D3D12_BARRIER_SYNC_COPY,
		D3D12_BARRIER_ACCESS_COPY_SOURCE,
		D3D12_BARRIER_LAYOUT_COPY_SOURCE
	};

	inline constexpr BarrierState ResolveDest
	{
		D3D12_BARRIER_SYNC_RESOLVE,
		D3D12_BARRIER_ACCESS_RESOLVE_DEST,
		D3D12_BARRIER_LAYOUT_RESOLVE_DEST
	};

	inline constexpr BarrierState ResolveSource
	{
		D3D12_BARRIER_SYNC_RESOLVE,
		D3D12_BARRIER_ACCESS_RESOLVE_SOURCE,
		D3D12_BARRIER_LAYOUT_RESOLVE_SOURCE
	};

	inline constexpr BarrierState Present
	{
		D3D12_BARRIER_SYNC_NONE,
		D3D12_BARRIER_ACCESS_NO_ACCESS,
		D3D12_BARRIER_LAYOUT_PRESENT
	};

	inline constexpr BarrierState IndirectArgument
	{
		D3D12_BARRIER_SYNC_EXECUTE_INDIRECT,
		D3D12_BARRIER_ACCESS_INDIRECT_ARGUMENT,
		D3D12_BARRIER_LAYOUT_UNDEFINED
	};

	inline constexpr BarrierState GenericRead
	{
		D3D12_BARRIER_SYNC_ALL_SHADING,
		D3D12_BARRIER_ACCESS_SHADER_RESOURCE,
		D3D12_BARRIER_LAYOUT_GENERIC_READ
	};

	inline constexpr BarrierState RayTraceRead
	{
		D3D12_BARRIER_SYNC_RAYTRACING,
		D3D12_BARRIER_ACCESS_SHADER_RESOURCE,
		D3D12_BARRIER_LAYOUT_GENERIC_READ
	};

	// ---- Buffer-only states (no layout) ----
	inline constexpr BarrierState BufferCommon
	{
		D3D12_BARRIER_SYNC_NONE,
		D3D12_BARRIER_ACCESS_NO_ACCESS
	};

	inline constexpr BarrierState BufferClearUAV
	{
		D3D12_BARRIER_SYNC_CLEAR_UNORDERED_ACCESS_VIEW,
		D3D12_BARRIER_ACCESS_UNORDERED_ACCESS
	};

	inline constexpr BarrierState VertexBuffer
	{
		D3D12_BARRIER_SYNC_VERTEX_SHADING,
		D3D12_BARRIER_ACCESS_VERTEX_BUFFER
	};

	inline constexpr BarrierState IndexBuffer
	{
		D3D12_BARRIER_SYNC_INDEX_INPUT,
		D3D12_BARRIER_ACCESS_INDEX_BUFFER
	};

	inline constexpr BarrierState ConstantBuffer
	{
		D3D12_BARRIER_SYNC_NON_PIXEL_SHADING,
		D3D12_BARRIER_ACCESS_CONSTANT_BUFFER
	};

	inline constexpr BarrierState PixelConstantBuffer
	{
		D3D12_BARRIER_SYNC_PIXEL_SHADING,
		D3D12_BARRIER_ACCESS_CONSTANT_BUFFER
	};

	inline constexpr BarrierState ComputeConstantBuffer
	{
		D3D12_BARRIER_SYNC_COMPUTE_SHADING,
		D3D12_BARRIER_ACCESS_CONSTANT_BUFFER
	};

	inline constexpr BarrierState BufferUnorderedAccess
	{
		D3D12_BARRIER_SYNC_COMPUTE_SHADING,
		D3D12_BARRIER_ACCESS_UNORDERED_ACCESS
	};

	inline constexpr BarrierState BufferCopyDest
	{
		D3D12_BARRIER_SYNC_COPY,
		D3D12_BARRIER_ACCESS_COPY_DEST
	};

	inline constexpr BarrierState BufferCopySource
	{
		D3D12_BARRIER_SYNC_COPY,
		D3D12_BARRIER_ACCESS_COPY_SOURCE
	};

	inline constexpr BarrierState BufferShaderResource
	{
		D3D12_BARRIER_SYNC_ALL_SHADING,
		D3D12_BARRIER_ACCESS_SHADER_RESOURCE
	};

	inline constexpr BarrierState BufferIndirectArgument
	{
		D3D12_BARRIER_SYNC_EXECUTE_INDIRECT,
		D3D12_BARRIER_ACCESS_INDIRECT_ARGUMENT
	};

	// ---- Raytracing states ----
	inline constexpr BarrierState AccelerationStructure
	{
		D3D12_BARRIER_SYNC_BUILD_RAYTRACING_ACCELERATION_STRUCTURE,
		D3D12_BARRIER_ACCESS_RAYTRACING_ACCELERATION_STRUCTURE_WRITE
	};

	inline constexpr BarrierState AccelerationStructureRead
	{
		D3D12_BARRIER_SYNC_RAYTRACING,
		D3D12_BARRIER_ACCESS_RAYTRACING_ACCELERATION_STRUCTURE_READ
	};

}; // namespace BarrierStates

#define DX12_BARRIER_STATE(state, is_non_pixel) ConvertToDx12BarrierState(state, is_non_pixel)
static BarrierState ConvertToDx12BarrierState(render::eTextureLayout layout, bool bNonPixelShader)
{
	using namespace render;
	switch (layout)
	{
	case eTextureLayout::Undefined             : return BarrierStates::Undefined;
	case eTextureLayout::General               : return BarrierStates::UnorderedAccess;
	case eTextureLayout::ColorAttachment       : return BarrierStates::RenderTarget;
	case eTextureLayout::DepthStencilAttachment: return BarrierStates::DepthStencilWrite;
	case eTextureLayout::DepthStencilReadOnly  : return BarrierStates::DepthStencilRead;
	case eTextureLayout::ShaderReadOnly        : return bNonPixelShader ? BarrierStates::NonPixelShaderResource : BarrierStates::ShaderResource;
	case eTextureLayout::TransferSource        : return BarrierStates::CopySource;
	case eTextureLayout::TransferDest          : return BarrierStates::CopyDest;
	case eTextureLayout::Present               : return BarrierStates::Present;

	default:
		assert(false && "Invalid barrier state!"); break;
	}

	return BarrierStates::Undefined;
}

} // namespace dx12