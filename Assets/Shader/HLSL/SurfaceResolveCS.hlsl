#define _CAMERA
#define _MESH
#define _TRANSFORM
#define _MATERIAL
#include "Common.hlsli"
#include "SurfaceResolve.hlsli"

cbuffer PushConstants : register(b0, ROOT_CONSTANT_SPACE)
{
    float2 g_Viewport;
};

ConstantBuffer< DescriptorHeapIndex > g_VBuf0          : register(b1, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_VBuf1          : register(b2, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_CoreNormal     : register(b3, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_CoreMaterial   : register(b4, ROOT_CONSTANT_SPACE);


[numthreads(16, 16, 1)]
void main(uint3 tID : SV_DispatchThreadID)
{
    uint2 px = tID.xy;
    if (px.x >= (uint)g_Viewport.x || px.y >= (uint)g_Viewport.y)
        return;

    RWTexture2D< float2 > CoreNormal   = GetResource(g_CoreNormal.index);
    RWTexture2D< float4 > CoreMaterial = GetResource(g_CoreMaterial.index);
    Texture2D< uint >     VBuf0        = GetResource(g_VBuf0.index);
    Texture2D< uint >     VBuf1        = GetResource(g_VBuf1.index);

    uint v0 = VBuf0.Load(int3(px, 0));

    if (VisIsSky(v0))
    {
        CoreNormal[px]   = float2(0.0, 0.0);
        CoreMaterial[px] = float4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    uint   v1          = VBuf1.Load(int3(px, 0));
    float2 pixelCenter = float2(px) + 0.5;
    ResolvedSurface s  = ResolveMeshSurface(v0, v1, pixelCenter, g_Viewport);

    CoreNormal[px]   = OctEncode(s.N);
    CoreMaterial[px] = float4(s.roughness, (float)s.matClass / 255.0, 0.0, 0.0);
}
