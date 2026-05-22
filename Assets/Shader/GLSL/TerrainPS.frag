#version 460

// =========================================================================
// TerrainPS.frag — GLSL/Vulkan port of TerrainPS.hlsl.
// Debug shading: world-space normal visualised as RGB.
// =========================================================================

// MS -> PS interface (mirrors HLSL MSOutput). Only normalWS is shaded; uv is
// declared to match the mesh shader's output interface.
layout(location = 0) in vec3 inNormalWS;
layout(location = 1) in vec2 inUv;

layout(location = 0) out vec4 outColor;

void main()
{
    vec3 color = normalize(inNormalWS) * 0.5 + 0.5;
    outColor = vec4(color, 1.0);
}
