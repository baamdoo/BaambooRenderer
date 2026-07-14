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

ConstantBuffer< VoxelChunkDesc >      g_VoxelChunkDesc        : register(b0, space1);
ConstantBuffer< DescriptorHeapIndex > g_ErosionDetailMap      : register(b5, ROOT_CONSTANT_SPACE);

static float s_DebugDiceLevel = 0.0;
static float s_MicroCavity = 0.0;
ConstantBuffer< DescriptorHeapIndex > g_VoxelVertices         : register(b6, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_VoxelMeshlets         : register(b7, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_VoxelMeshletVertices  : register(b8, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_VoxelMeshletTriangles : register(b9, ROOT_CONSTANT_SPACE);


ResolvedSurface ResolveVoxelSurface(uint v0, uint v1, float2 pixelCenter, float2 viewport)
{
    StructuredBuffer< Vertex >  Vertices         = GetResource(g_VoxelVertices.index);
    StructuredBuffer< Meshlet > Meshlets         = GetResource(g_VoxelMeshlets.index);
    StructuredBuffer< uint >    MeshletVertices  = GetResource(g_VoxelMeshletVertices.index);
    StructuredBuffer< uint >    MeshletTriangles = GetResource(g_VoxelMeshletTriangles.index);

    uint meshletIdx  = VisVoxelMeshletIndex(v1); // absolute voxel meshlet-pool index (task shader baked the offset)
    uint triLocal    = VisVoxelTriLocal(v1);
    uint subTriPlus1 = VisVoxelSubTriPlus1(v1);  // 0 = undiced base triangle

    VoxelChunkDesc chunk   = g_VoxelChunkDesc; // TODO: multi-chunk voxels
    Meshlet        meshlet = Meshlets[meshletIdx];

    uint tPacked3  = MeshletTriangles[chunk.meshletTriangleOffset + meshlet.triangleOffset + triLocal];
    uint locals[3] = { tPacked3 & 0xFF, (tPacked3 >> 8) & 0xFF, (tPacked3 >> 16) & 0xFF };

    float3 originWS = float3(chunk.originX, chunk.originY, chunk.originZ);

    float3 pos[3]; // chunk-local
    float3 posWS[3];
    float3 nrm[3];
    [unroll] for (uint k = 0; k < 3; ++k)
    {
        uint vi = chunk.vertexOffset + MeshletVertices[chunk.meshletVertexOffset + meshlet.vertexOffset + locals[k]];

        Vertex vv = Vertices[vi];
        pos[k]   = float3(vv.posX, vv.posY, vv.posZ);
        posWS[k] = originWS + pos[k];
        nrm[k]   = float3(vv.normalX, vv.normalY, vv.normalZ);
    }

    if (subTriPlus1 != 0u && chunk.diceMaxLevel != 0u)
    {
        float3 cameraPos = g_FrozenCamera.posWORLD - float3(chunk.originX, chunk.originY, chunk.originZ);

        uint   le0  = DiceEdgeLevel(pos[0], pos[1], cameraPos, chunk);
        uint   le1  = DiceEdgeLevel(pos[1], pos[2], cameraPos, chunk);
        uint   le2  = DiceEdgeLevel(pos[2], pos[0], cameraPos, chunk);
        uint   Lt   = max(le0, max(le1, le2));
        s_DebugDiceLevel = (float)Lt / 5.0;

        if (Lt != 0u)
        {
            Texture2D< float4 > ErosionMap = GetResource(g_ErosionDetailMap.index);

            uint  subIdx = subTriPlus1 - 1u;
            uint3 le     = uint3(le0, le1, le2);

            // Sub-vertex integer coords: flat (Lt <= 3) or hierarchical child*64 + local.
            uint3 coords[3];
            if (Lt <= 3u)
            {
                uint3 sub = DiceSubTriVerts(subIdx, Lt);
                [unroll] for (uint k = 0; k < 3; ++k)
                    coords[k] = DiceSubVertexCoordInt(sub[k], Lt);
            }
            else
            {
                uint3 cc0, cc1, cc2;
                DiceChildCorners(subIdx >> 6u, Lt, cc0, cc1, cc2);

                uint3 sub = DiceSubTriVerts(subIdx & 63u, 3u);
                [unroll] for (uint k = 0; k < 3; ++k)
                    coords[k] = DiceHierCoord(cc0, cc1, cc2, DiceSubVertexCoordInt(sub[k], 3u));
            }

            // stage sub-corner normals, assign after the loop
            float3 subNrm[3];
            [unroll] for (uint k = 0; k < 3; ++k)
            {
                float3 subL, subN;
                DiceSubVertex(coords[k], Lt, le, pos[0], pos[1], pos[2], nrm[0], nrm[1], nrm[2], subL, subN);

                posWS[k]  = DisplaceVoxelDice(originWS + subL, subN.y, g_FrozenCamera.posWORLD,
                                              chunk, ErosionMap, g_LinearClampSampler);
                subNrm[k] = subN;
            }
            nrm[0] = subNrm[0];
            nrm[1] = subNrm[1];
            nrm[2] = subNrm[2];
        }
    }

    float4 c[3];
    [unroll] for (uint k = 0; k < 3; ++k)
        c[k] = mul(g_Camera.mViewProj, float4(posWS[k], 1.0));

    float2 ndc = (pixelCenter / viewport) * 2.0 - 1.0;
    ndc.y = -ndc.y; // NDC y-up vs pixel y-down

    float3 bary = Barycentrics(ndc, c[0], c[1], c[2]);
    float3 N    = normalize(bary.x * nrm[0] + bary.y * nrm[1] + bary.z * nrm[2]);

    float baseNy = saturate(N.y);

    // Detail normal (bake slope tier + geometry-locked micro band)
    {
        Texture2D< float4 > ErosionMap = GetResource(g_ErosionDetailMap.index);

        float3 pWS   = bary.x * posWS[0] + bary.y * posWS[1] + bary.z * posWS[2];
        float2 eroUV = (pWS.xz - float2(chunk.originX, chunk.originZ)) / max(chunk.chunkSizeMeter, 1e-3);
        float4 ero   = ErosionMap.SampleLevel(g_LinearClampSampler, eroUV, 0.0);

        uint mapW, mapH;
        ErosionMap.GetDimensions(mapW, mapH);
        float texelUV = 1.0 / (float)mapW;
        float texelM  = max(chunk.chunkSizeMeter, 1e-3) * texelUV;
        float hE = ErosionMap.SampleLevel(g_LinearClampSampler, eroUV + float2(texelUV, 0.0), 0.0).r;
        float hW = ErosionMap.SampleLevel(g_LinearClampSampler, eroUV - float2(texelUV, 0.0), 0.0).r;
        float hN = ErosionMap.SampleLevel(g_LinearClampSampler, eroUV + float2(0.0, texelUV), 0.0).r;
        float hS = ErosionMap.SampleLevel(g_LinearClampSampler, eroUV - float2(0.0, texelUV), 0.0).r;

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

            s_MicroCavity = 0.6 * saturate(-micro.x * gate / max(chunk.microAmplitudeMeter * 2.0, 1e-4));
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

    ResolvedSurface s;
    if (VisIsVoxel(v0))
        s = ResolveVoxelSurface(v0, v1, pixelCenter, g_Viewport);
    else
        s = ResolveMeshSurface(v0, v1, pixelCenter, g_Viewport);

    CoreNormal[px]   = OctEncode(s.N);
    CoreMaterial[px] = float4(s.roughness, (float)s.matClass / 255.0, s_DebugDiceLevel, s_MicroCavity); // .b = Lt/5 (dice debug) | .a = micro cavity
}
