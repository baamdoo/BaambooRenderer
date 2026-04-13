# Bug Report

## Occlusion Culling: False Negative

- **HiZ pyramid generate/test sampler**: 
    - Issue: `POINT + MIN` reduction is effectively a no-op since POINT selects only one texel. When the projected AABB straddles a mip texel boundary, a single-texel sample misses one side. 
    - Fix: Switching to `LINEAR + MIN` makes `SampleLevel` return the min of the 2x2 bilinear footprint, naturally covering straddled boundaries.
