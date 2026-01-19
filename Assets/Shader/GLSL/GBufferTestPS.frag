#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : enable

layout(location = 0) in vec4 color;

layout(location = 0) out vec4 outGBuffer0; // albedo.rgb + AO.a
layout(location = 1) out vec4 outGBuffer1; // Normal.xyz + MaterialID.w
layout(location = 2) out vec4 outGBuffer2; // Emissive.rgb
layout(location = 3) out vec4 outGBuffer3; // MotionVectors.xy + Roughness.z + Metallic.w

void main()
{
	outGBuffer0 = color;
	outGBuffer1 = color;
	outGBuffer2 = color;
	outGBuffer3 = color;
}