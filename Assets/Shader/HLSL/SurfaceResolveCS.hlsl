#define _CAMERA
#define _FROZENCAMERA
#define _MESH
#define _TRANSFORM
#define _MATERIAL
#include "Common.hlsli"
#include "SurfaceResolve.hlsli"
#include "VoxelTerrainCommon.hlsli"

cbuffer PushConstants : register(b0, ROOT_CONSTANT_SPACE)
{
    float2 g_Viewport;
};

ConstantBuffer< DescriptorHeapIndex > g_VBuf0          : register(b1, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_VBuf1          : register(b2, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_CoreNormal     : register(b3, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_CoreMaterial   : register(b4, ROOT_CONSTANT_SPACE);

ConstantBuffer< DescriptorHeapIndex > g_VoxelChunkDescs       : register(b11, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_ErosionDetailMap      : register(b5, ROOT_CONSTANT_SPACE);

ConstantBuffer< DescriptorHeapIndex > g_VoxelVertices         : register(b6, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_VoxelMeshlets         : register(b7, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_VoxelMeshletVertices  : register(b8, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_VoxelMeshletTriangles : register(b9, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_Velocity              : register(b10, ROOT_CONSTANT_SPACE);


ResolvedSurface ResolveVoxelSurface(uint v0, uint v1, float2 pixelCenter, float2 viewport)
{
    StructuredBuffer< VoxelVertex > Vertices     = GetResource(g_VoxelVertices.index);
    StructuredBuffer< Meshlet > Meshlets         = GetResource(g_VoxelMeshlets.index);
    StructuredBuffer< uint >    MeshletVertices  = GetResource(g_VoxelMeshletVertices.index);
    StructuredBuffer< uint >    MeshletTriangles = GetResource(g_VoxelMeshletTriangles.index);

    StructuredBuffer< VoxelChunkDesc > VoxelChunkDescs = GetResource(g_VoxelChunkDescs.index);
    VoxelChunkDesc chunk = VoxelChunkDescs[VisChunkIndex(v0) - VOXEL_CHUNK_INSTANCE_BASE];

    uint meshletIdx  = chunk.mOffset + VisVoxelMeshletIndex(v1); // page-relative voxel meshlet-pool index
    uint triLocal    = VisVoxelTriLocal(v1);
    uint subTriPlus1 = VisVoxelSubTriPlus1(v1);  // 0 = undiced base triangle

    Meshlet meshlet = Meshlets[meshletIdx];

    uint tPacked3  = MeshletTriangles[chunk.mtOffset + meshlet.triangleOffset + triLocal];
    uint locals[3] = { tPacked3 & 0xFF, (tPacked3 >> 8) & 0xFF, (tPacked3 >> 16) & 0xFF };

    float3 originWS = float3(chunk.originX, chunk.originY, chunk.originZ);

    float3 posWS[3];
    float3 normal[3];
    [unroll] for (uint k = 0; k < 3; ++k)
    {
        uint vi = chunk.vOffset + MeshletVertices[chunk.mvOffset + meshlet.vertexOffset + locals[k]];

        VoxelVertex vv  = Vertices[vi];
        float3      nq  = VoxelUnpackNormal(vv);
        float3      pWS = VoxelUnpackPosTransition(vv, chunk.chunkSizeMeter, chunk.lodAndMask) + originWS;
        posWS[k]  = VoxelMorphPosWS(vv, pWS, nq, g_FrozenCamera.posWORLD, chunk);
        normal[k] = VoxelMorphNormal(vv, nq, pWS, g_FrozenCamera.posWORLD, chunk);
    }

    if (subTriPlus1 != 0u && chunk.diceMaxLevel != 0u)
    {
        float3 cameraPosWS = g_FrozenCamera.posWORLD;

        uint le0  = DiceEdgeLevel(posWS[0], posWS[1], cameraPosWS, chunk);
        uint le1  = DiceEdgeLevel(posWS[1], posWS[2], cameraPosWS, chunk);
        uint le2  = DiceEdgeLevel(posWS[2], posWS[0], cameraPosWS, chunk);
        uint Lt   = max(le0, max(le1, le2));

        if (Lt != 0u)
        {
            Texture2DArray< float4 > ErosionMap = GetResource(g_ErosionDetailMap.index);

            uint  subIdx = subTriPlus1 - 1u;
            uint3 le     = uint3(le0, le1, le2);

            uint3 sub = DiceSubTriVerts(subIdx, Lt);
            uint3 coords[3];
            [unroll] for (uint k = 0; k < 3; ++k)
                coords[k] = DiceSubVertexCoordInt(sub[k], Lt);

            // stage corners and sub-corner normals: posWS is both lerp input and output
            float3 cornerWS[3] = { posWS[0], posWS[1], posWS[2] };
            float3 subNrm[3];
            [unroll] for (uint k = 0; k < 3; ++k)
            {
                float3 subWS, subN;
                DiceSubVertex(coords[k], Lt, le, cornerWS[0], cornerWS[1], cornerWS[2], normal[0], normal[1], normal[2], subWS, subN);

                posWS[k]  = DisplaceVoxelDice(subWS, subN.y, g_FrozenCamera.posWORLD, chunk, ErosionMap, g_LinearClampSampler);
                subNrm[k] = subN;
            }
            normal[0] = subNrm[0];
            normal[1] = subNrm[1];
            normal[2] = subNrm[2];
        }
    }

    float4 c[3];
    [unroll] for (uint k = 0; k < 3; ++k)
        c[k] = mul(g_Camera.mViewProj, float4(posWS[k], 1.0));

    float2 ndc = (pixelCenter / viewport) * 2.0 - 1.0;
    ndc.y = -ndc.y; // NDC y-up vs pixel y-down

    float3 bary = Barycentrics(ndc, c[0], c[1], c[2]);
    float3 N    = normalize(bary.x * normal[0] + bary.y * normal[1] + bary.z * normal[2]);

    float baseNy = saturate(N.y);

    float3 pWS = bary.x * posWS[0] + bary.y * posWS[1] + bary.z * posWS[2];

    // Detail normal (bake slope tier + geometry-locked micro band); coarse-LOD chunks carry no erosion slice
    if (chunk.erosionSlice != INVALID_INDEX)
    {
        Texture2DArray< float4 > ErosionMap = GetResource(g_ErosionDetailMap.index);

        uint mapW, mapH, mapSlices;
        ErosionMap.GetDimensions(mapW, mapH, mapSlices);

        float3 eroUV = VoxelErosionUV(chunk, pWS.xz, (float)mapW);
        float4 ero   = ErosionMap.SampleLevel(g_LinearClampSampler, eroUV, 0.0);

        float texelUV = 1.0 / (float)mapW;
        float texelM  = max(chunk.chunkSizeMeter, 1e-3) / ((float)mapW - 2.0 * float(VOXEL_EROSION_APRON)); // world pitch per texel
        float hE = ErosionMap.SampleLevel(g_LinearClampSampler, eroUV + float3(texelUV, 0.0, 0.0), 0.0).r;
        float hW = ErosionMap.SampleLevel(g_LinearClampSampler, eroUV - float3(texelUV, 0.0, 0.0), 0.0).r;
        float hN = ErosionMap.SampleLevel(g_LinearClampSampler, eroUV + float3(0.0, texelUV, 0.0), 0.0).r;
        float hS = ErosionMap.SampleLevel(g_LinearClampSampler, eroUV - float3(0.0, texelUV, 0.0), 0.0).r;

        // heightfield gate: only surfaces on the baked height receive erosion detail
        float  hfMask = saturate(1.0 - abs(pWS.y - ero.b) * 0.5);
        float2 ds     = float2(hE - hW, hN - hS) / (2.0 * texelM) * hfMask;

        if (chunk.microOctaves != 0u && chunk.diceMaxLevel != 0u)
        {
            DiceMicroParams mpar = DiceMicroFromDesc(chunk);
            float dCam    = length(pWS - g_FrozenCamera.posWORLD);
            // geometry-locked: same level and gates as the displacement — shading shows only what is carved
            float lv      = min(DiceLodLevel(dCam, chunk.voxelSizeMeter, chunk), (float)chunk.diceMaxLevel);
            float wDist   = saturate((chunk.diceRadiusMeter - dCam) / chunk.diceFadeWidthMeter);
            float creaseA = 1.0 + chunk.microCreaseBoost * saturate(-ero.g);
            float3 micro  = VoxelMicroHeightDeriv(pWS.xz, lv, mpar);
            float  gate   = creaseA * hfMask * wDist * baseNy;
            ds += micro.yz * gate;
        }

        if (N.y > 0.05)
        {
            float2 slopeAll = -N.xz / N.y + ds; // slope-space composite
            N = normalize(float3(-slopeAll.x, 1.0, -slopeAll.y));
        }
    }

    ResolvedSurface rs;
    rs.matClass  = MATCLASS_STANDARD;
    rs.N         = N;
    rs.roughness = baseNy; // terrain reuses the R channel as base Ng.y (cliff-blend input)
    rs.baseColor = float3(0.5, 0.5, 0.5);
    rs.metallic  = 0.0;
    rs.posWS     = pWS;
    return rs;
}


[numthreads(16, 16, 1)]
void main(uint3 tID : SV_DispatchThreadID)
{
    uint2 px = tID.xy;
    if (px.x >= (uint)g_Viewport.x || px.y >= (uint)g_Viewport.y)
        return;

    RWTexture2D< float2 > CoreNormal   = GetResource(g_CoreNormal.index);
    RWTexture2D< float4 > CoreMaterial = GetResource(g_CoreMaterial.index);
    RWTexture2D< float2 > Velocity     = GetResource(g_Velocity.index);
    Texture2D< uint >     VBuf0        = GetResource(g_VBuf0.index);
    Texture2D< uint >     VBuf1        = GetResource(g_VBuf1.index);

    uint v0 = VBuf0.Load(int3(px, 0));

    if (VisIsSky(v0))
    {
        CoreNormal[px]   = float2(0.0, 0.0);
        CoreMaterial[px] = float4(0.0, 0.0, 0.0, 0.0);
        Velocity[px]     = float2(0.0, 0.0);
        return;
    }

    uint   v1          = VBuf1.Load(int3(px, 0));
    float2 pixelCenter = float2(px) + 0.5;

    ResolvedSurface s;
    if (VisIsVoxel(v0))
        s = ResolveVoxelSurface(v0, v1, pixelCenter, g_Viewport);
    else
        s = ResolveMeshSurface(v0, v1, pixelCenter, g_Viewport);

    CoreNormal[px]   = OctEncode(s.N);
    CoreMaterial[px] = float4(s.roughness, (float)s.matClass / 255.0, 0.0, 0.0);

    float2 currUV = float2(pixelCenter.x / g_Viewport.x, 1.0 - pixelCenter.y / g_Viewport.y) - g_Camera.jitterUV;
    float4 prevCS = mul(g_Camera.mViewProjUnjitteredPrev, float4(s.posWS, 1.0));
    float2 prevUV = (prevCS.xy / prevCS.w) * 0.5 + 0.5;
    Velocity[px]  = currUV - prevUV;
}
