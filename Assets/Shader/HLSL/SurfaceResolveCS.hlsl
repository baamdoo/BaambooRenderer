#define _CAMERA
#define _MESH
#define _TRANSFORM
#define _MATERIAL
#include "Common.hlsli"
#include "TerrainCommon.hlsli"
#include "SurfaceResolve.hlsli"

cbuffer PushConstants : register(b0, ROOT_CONSTANT_SPACE)
{
    float2 g_Viewport;
};

ConstantBuffer< DescriptorHeapIndex > g_VBuf0        : register(b1, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_VBuf1        : register(b2, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_CoreNormal   : register(b3, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_CoreMaterial : register(b4, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_DepthBuffer  : register(b5, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_Heightmap    : register(b6, ROOT_CONSTANT_SPACE);


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

    if (VisIsTerrain(v0))
    {
        Texture2D< float > Heightmap   = GetResource(g_Heightmap.index);
        Texture2D< float > DepthBuffer = GetResource(g_DepthBuffer.index);

        float2 uv    = (float2(px) + 0.5) / g_Viewport;
        float  depth = DepthBuffer.Load(int3(px, 0));

        float3 worldPos  = ReconstructWorldPos(uv, depth, g_Camera.mViewProjInv);
        float2 terrainUV = (worldPos.xz - float2(g_Terrain.TerrainOriginX, g_Terrain.TerrainOriginZ)) / g_Terrain.TerrainSizeMeter;

        const float2 texel = float2(g_Terrain.HeightmapTexel, g_Terrain.HeightmapTexel);
        const float hL = Heightmap.SampleLevel(g_LinearClampSampler, terrainUV - float2(texel.x, 0.0), 0);
        const float hR = Heightmap.SampleLevel(g_LinearClampSampler, terrainUV + float2(texel.x, 0.0), 0);
        const float hD = Heightmap.SampleLevel(g_LinearClampSampler, terrainUV - float2(0.0, texel.y), 0);
        const float hU = Heightmap.SampleLevel(g_LinearClampSampler, terrainUV + float2(0.0, texel.y), 0);

        const float dhdx = (hR - hL) * g_Terrain.HeightRangeMeter / (2.0 * g_Terrain.WorldPerTexel);
        const float dhdz = (hU - hD) * g_Terrain.HeightRangeMeter / (2.0 * g_Terrain.WorldPerTexel);

        float3 N = normalize(float3(-dhdx, 1.0, -dhdz));

        CoreNormal[px]   = OctEncode(N);
        CoreMaterial[px] = float4(0.85, (float)MATCLASS_TERRAIN / 255.0, 0.0, 0.0);

        return;
    }

    uint   v1          = VBuf1.Load(int3(px, 0));
    float2 pixelCenter = float2(px) + 0.5;
    ResolvedSurface s  = ResolveMeshSurface(v0, v1, pixelCenter, g_Viewport);

    CoreNormal[px]   = OctEncode(s.N);
    CoreMaterial[px] = float4(s.roughness, (float)s.matClass / 255.0, 0.0, 0.0);
}
