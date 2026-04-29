# Bug Report

## Occlusion Culling: False Negative

- **HiZ pyramid generate/test sampler**: 
    - Issue: `POINT + MIN` reduction is effectively a no-op since POINT selects only one texel. When the projected AABB straddles a mip texel boundary, a single-texel sample misses one side. 
    - Fix: Switching to `LINEAR + MIN` makes `SampleLevel` return the min of the 2x2 bilinear footprint, naturally covering straddled boundaries.

## DX12 MeshletVisibilityBuffer Resize: Stale View NumElements

- **Symptom**: After `MeshletVisibilityBuffer` is resized (first frame when scene needs more capacity than `NUM_INITIAL_MESHLET_VISIBILITY_WORDS`), with meshlet-level occlusion ON, Phase 2 emits ALL visible meshlets every frame instead of just newly-disoccluded ones which means MeshletVisibilityBuffer is not properly read/written by TaskShader.

- **PIX investigation** :

    1. Captured Phase 1 + Phase 2 task-shader reads of `MeshletVisibilityBuffer` in frame N+1. Both phases showed **identical** contents (`[0]=16383`, `[1..37]=0`, `[38..45]=non-zero`, rest zero) and **same heap idx (14)** — ruling out descriptor staleness, UAV barriers, and cross-phase sync hazards.

    2. The "Resources" tab listed **5 entries** named `GBufferPass::MeshletVisibilityBuffer`:
        - `ID 47/48` (obj#32): pre-resize SRV/UAV — `EC = 2048`, `stride = 4`. Not referenced by Phase 1/2.
        - `ID 181/182` (obj#94): post-resize SRV/UAV — **`EC = 2048` (stale!)**, `stride = 4`. Referenced by both phases.
        - `ID 204` (obj#94): post-resize underlying resource — total bytes = 613,888, **`EC = 0` (stale!)**.

    3. Key observation: the post-resize UAV view (ID 182) had `NumElements = 2048` while the underlying buffer was 613,888 bytes (= 153,472 `uint`s). The view exposed only the first **65,536 bits**. Any instance with `visOffset >= 65536` accessed out-of-view-bounds — HLSL OOB semantics silently return `0` on read and silently drop on write. So `atomicOr` writes for those instances vanished, `bPrevVis` always read 0, and the visibility-persistence loop never converged.

- **Root cause** (`Dx12Buffer::Resize`):
    - `m_Count = m_BufferSize / m_ElementSize` was at the **end** of `Resize`, *after* `CreateViews()`.
    - `CreateViews()` for Structured buffers feeds `m_Count` directly into `D3D12_BUFFER_UAV_DESC::NumElements`. So the new view inherited the **pre-resize** count even though the new resource and `m_BufferSize` were already updated.
