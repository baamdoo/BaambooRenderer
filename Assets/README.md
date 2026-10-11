# Runtime scene assets

The `.gitignore` allowlist includes the engine inputs for the five PathTracer images currently displayed in the root README. Shared meshes, textures, density grids and environment-sampling CDFs are included. Other generated tests, reference-renderer scenes and render outputs remain ignored. Assets already tracked before this selection remain tracked.

| README image | Engine scene ID |
|---|---|
| `PT_water_glass.png` | `gallery_glass_of_water` |
| `PT_complex_scene.png` | `complex_room` |
| `PT_killeroo.png` | `gallery_nlayer_killeroo_n4_alpha01_bright10x` |
| `PT_shaderball.png` | `gallery_nlayer_shaderball_n3` |
| `PT_smoke_in_the_glassball.png` | `gallery_medium_stage3_showcase_color_dense_v1_hero` |

Select a scene with `Application.exe --pt --pt-scene <scene-id>`. `complex_room` is configured in `RayTracingApp.cpp`; the other entries use `Generated/<scene-id>/scene.baamboo`. Scene paths are relative to `Assets/`.

The smoke image in the README uses the color/dense hero scene. The later `gallery_medium_final_filled_v1` variant is a different scene and is outside this image-based selection.

Environment `.envcdf.bin` files are runtime inputs: omitting them changes environment importance sampling. `.mip` sidecars are not needed by this DXR EXR-loading path; pre-existing tracked sidecars have been left intact.

## Other sample assets

`ExampleApp`, `LightingApp` and `BistroApp` use `Model/cube.obj`, its `cube.mtl`, `Model/sphere.obj`, `Model/kitten.obj` and the DamagedHelmet glTF with its buffer/images. Their common sky, cloud and LTC textures are also tracked. Terrain geometry is generated on the GPU.

`sphere.obj` is an original procedural unit UV sphere added to satisfy the existing ExampleApp material sweep: Y-up, 64 longitude segments, 32 latitude segments, outward normals and 3,968 triangles. `cube.mtl` supplies the previously missing `Material` declaration; sample code overrides its material values. These two files were generated to fill missing inputs, rather than recovered from an earlier render.

## Source and attribution records

- Glass of Water: aXel's scene, obtained through Benedikt Bitterli's rendering resources. Its [CC0 notice](Generated/gallery_glass_of_water/LICENSE.txt) accompanies the generated geometry.
- Shaderball: [USD Shader Ball for glTF credits](Model/USDShaderBallForGltf/README.md#legal) and [CC BY 4.0 license](Model/USDShaderBallForGltf/LICENSE.md). The PLY files in `gallery_nlayer_shaderball_shared` derive from that model with scale/coordinate conversion and engine material overrides.
- Killeroo: derived from [PBRT-v4 scene geometry](https://github.com/mmp/pbrt-v4-scenes). The existing [scene header](Generated/gallery_nlayer_killeroo_n4_alpha01_bright10x/scene.baamboo) records that PBRT uses it with permission and says not to redistribute the copied geometry. That source restriction is preserved; local staging is not a grant of redistribution rights.
- Smoke: the [source manifest](Generated/volume_smoke_v1/source_manifest.json) identifies the PBRT/OpenVDB sample and its checksum; the [extraction record](Generated/volume_smoke_v1/extraction.json) describes the density-only, uncompressed conversion. The original preparation did not establish a sample-data redistribution license. OpenVDB's software license is not asserted as the volume-data license.
- `kitten.obj` and `studio_small.exr` are existing local assets; no adjacent source/license record was found in this checkout, so no new license attribution is asserted here.
