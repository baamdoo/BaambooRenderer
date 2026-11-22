#include "ShaderShared/Scene.h"
#include "ShaderShared/Mesh.h"
#include "ShaderShared/VoxelTerrain.h"
#include "Common/Binding.slangh"
#include "Common/SurfaceResolve.slangh"
#include "Common/VisibilityBuffer.slangh"
#include "Common/VoxelTerrain.slangh"

struct SurfaceResolvePush
{
    uint ID_Scene;
    uint ID_VBuf0;
    uint ID_VBuf1;
    uint ID_CoreNormal;
    uint ID_CoreMaterial;
    uint ID_Velocity;
    uint ID_VoxelChunkDescs;
    uint ID_VoxelVertices;
    uint ID_VoxelMeshlets;
    uint ID_VoxelMeshletVertices;
    uint ID_VoxelMeshletTriangles;
    uint ID_ErosionDetailMap;
};

[[vk::push_constant]] ConstantBuffer< SurfaceResolvePush > g_Push : register(b0, ROOT_CONSTANT_SPACE);

// Re-derives the triangle the mesh shader rasterized: morphed corners, unmorphed normals for dicing, morphed normals for shading
ResolvedSurface ResolveVoxelSurface(uint v0, uint v1, float2 pixelCenter, float2 viewport, float4x4 viewProj, float3 cameraPosWS)
{
    StructuredBuffer< VoxelChunkDesc > chunks           = GetResource(g_Push.ID_VoxelChunkDescs);
    StructuredBuffer< VoxelVertex >    vertices         = GetResource(g_Push.ID_VoxelVertices);
    StructuredBuffer< Meshlet >        meshlets         = GetResource(g_Push.ID_VoxelMeshlets);
    StructuredBuffer< uint >           meshletVertices  = GetResource(g_Push.ID_VoxelMeshletVertices);
    StructuredBuffer< uint >           meshletTriangles = GetResource(g_Push.ID_VoxelMeshletTriangles);
    Texture2DArray< float4 >           erosionMap       = GetResource(g_Push.ID_ErosionDetailMap);

    const VoxelChunkDesc chunk = chunks[VisChunkIndex(v0) - kVoxelChunkInstanceBase];

    // VBuf1 holds the meshlet index relative to the chunk's page
    const Meshlet meshlet     = meshlets[chunk.mOffset + VisVoxelMeshletIndex(v1)];
    const uint    subTriPlus1 = VisVoxelSubTriPlus1(v1); // 0 = undiced base triangle
    const uint    tPacked3    = meshletTriangles[chunk.mtOffset + meshlet.triangleOffset + VisVoxelTriLocal(v1)];

    float3 posWS[3];
    float3 normalBase[3];
    float3 normal[3];
    [unroll]
    for (uint k = 0; k < 3; ++k)
    {
        const uint local = (tPacked3 >> (8u * k)) & 0xFFu;
        const uint vi    = chunk.vOffset + meshletVertices[chunk.mvOffset + meshlet.vertexOffset + local];

        const VoxelVertex vv  = vertices[vi];
        const float3      pWS = VoxelUnpackPosTransition(vv, chunk.chunkSizeMeter, chunk.lodAndMask) + chunk.originWS;
        normalBase[k] = VoxelUnpackNormal(vv);
        posWS[k]      = VoxelMorphPosWS(vv, pWS, normalBase[k], cameraPosWS, chunk);
        normal[k]     = VoxelMorphNormal(vv, normalBase[k], pWS, cameraPosWS, chunk);
    }

    if (subTriPlus1 != 0u && chunk.diceMaxLevel != 0u)
    {
        const uint le0 = DiceEdgeLevel(posWS[0], posWS[1], cameraPosWS, chunk);
        const uint le1 = DiceEdgeLevel(posWS[1], posWS[2], cameraPosWS, chunk);
        const uint le2 = DiceEdgeLevel(posWS[2], posWS[0], cameraPosWS, chunk);
        const uint lt  = max(le0, max(le1, le2));

        if (lt != 0u)
        {
            const uint3 le  = uint3(le0, le1, le2);
            const uint3 sub = DiceSubTriVerts(subTriPlus1 - 1u, lt);

            // the corners are both the lerp input and the output
            const float3 cornerWS[3] = { posWS[0], posWS[1], posWS[2] };
            const float3 cornerN[3]  = { normal[0], normal[1], normal[2] };
            [unroll]
            for (uint k = 0; k < 3; ++k)
            {
                const uint3 coord = DiceSubVertexCoordInt(sub[k], lt);

                float3 subWS, subNBase, subN;
                DiceSubVertex(coord, lt, le, cornerWS[0], cornerWS[1], cornerWS[2], normalBase[0], normalBase[1], normalBase[2], subWS, subNBase);
                DiceSubVertex(coord, lt, le, cornerWS[0], cornerWS[1], cornerWS[2], cornerN[0], cornerN[1], cornerN[2], subWS, subN);

                posWS[k]  = DisplaceVoxelDice(subWS, subNBase.y, cameraPosWS, chunk, erosionMap, g_LinearClampSampler);
                normal[k] = subN;
            }
        }
    }

    float4 c[3];
    [unroll]
    for (uint k = 0; k < 3; ++k)
        c[k] = mul(viewProj, float4(posWS[k], 1.0));

    const float3 bary = Barycentrics(PixelCenterToNDC(pixelCenter, viewport), c[0], c[1], c[2]);
    float3       N    = normalize(bary.x * normal[0] + bary.y * normal[1] + bary.z * normal[2]);
    const float3 pWS  = bary.x * posWS[0] + bary.y * posWS[1] + bary.z * posWS[2];

    const float baseNy = saturate(N.y);

    // Detail normal (bake slope tier + geometry-locked micro band); coarse-LOD chunks carry no erosion slice
    if (chunk.erosionSlice != kInvalidIndex)
    {
        uint mapW, mapH, mapSlices;
        erosionMap.GetDimensions(mapW, mapH, mapSlices);

        const float3 eroUV = VoxelErosionUV(chunk, pWS.xz, float(mapW));
        const float4 ero   = erosionMap.SampleLevel(g_LinearClampSampler, eroUV, 0.0);

        const float texelUV = 1.0 / float(mapW);
        const float texelM  = max(chunk.chunkSizeMeter, 1e-3) / (float(mapW) - 2.0 * float(kVoxelErosionApron)); // world pitch per texel
        const float hE = erosionMap.SampleLevel(g_LinearClampSampler, eroUV + float3(texelUV, 0.0, 0.0), 0.0).r;
        const float hW = erosionMap.SampleLevel(g_LinearClampSampler, eroUV - float3(texelUV, 0.0, 0.0), 0.0).r;
        const float hN = erosionMap.SampleLevel(g_LinearClampSampler, eroUV + float3(0.0, texelUV, 0.0), 0.0).r;
        const float hS = erosionMap.SampleLevel(g_LinearClampSampler, eroUV - float3(0.0, texelUV, 0.0), 0.0).r;

        // heightfield gate: only surfaces on the baked height receive erosion detail
        const float hfMask = saturate(1.0 - abs(pWS.y - ero.b) * 0.5);
        float2      ds     = float2(hE - hW, hN - hS) / (2.0 * texelM) * hfMask;

        if (chunk.microOctaves != 0u && chunk.diceMaxLevel != 0u)
        {
            const DiceMicroParams mpar = DiceMicroFromDesc(chunk);
            const float dCam = length(pWS - cameraPosWS);
            // geometry-locked: same level and gates as the displacement, so shading shows only what is carved
            const float  lv      = min(DiceLodLevel(dCam, chunk.voxelSizeMeter, chunk), float(chunk.diceMaxLevel));
            const float  wDist   = saturate((chunk.diceRadiusMeter - dCam) / chunk.diceFadeWidthMeter);
            const float  creaseA = 1.0 + chunk.microCreaseBoost * saturate(-ero.g);
            const float3 micro   = VoxelMicroHeightDeriv(pWS.xz, lv, mpar);
            ds += micro.yz * (creaseA * hfMask * wDist * baseNy);
        }

        if (N.y > 0.05)
        {
            const float2 slopeAll = -N.xz / N.y + ds; // slope-space composite
            N = normalize(float3(-slopeAll.x, 1.0, -slopeAll.y));
        }
    }

    ResolvedSurface rs;
    rs.N         = N;
    rs.roughness = baseNy; // base Ng.y, the lighting pass's cliff-blend input
    rs.matClass  = MATCLASS_STANDARD;
    rs.posWS     = pWS;
    return rs;
}

// Texture-UV with y down, uv = (pixel + 0.5) / size
float2 ClipToUV(float4 posCS)
{
    const float2 ndc = posCS.xy / posCS.w;
    return float2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
}

// Both terms use unjittered projections, so the TAA jitter cancels; w = 0 points (sky directions) work too
float2 ScreenVelocity(float4 posWS, float4x4 viewProjUnjittered, float4x4 viewProjUnjitteredPrev)
{
    return ClipToUV(mul(viewProjUnjittered, posWS)) - ClipToUV(mul(viewProjUnjitteredPrev, posWS));
}

[numthreads(16, 16, 1)]
void main(uint3 tID : SV_DispatchThreadID)
{
    RWTexture2D< float2 > coreNormal   = GetResource(g_Push.ID_CoreNormal);
    RWTexture2D< float4 > coreMaterial = GetResource(g_Push.ID_CoreMaterial);
    RWTexture2D< float2 > velocity     = GetResource(g_Push.ID_Velocity);

    uint2 size;
    coreNormal.GetDimensions(size.x, size.y);

    const uint2 px = tID.xy;
    if (any(px >= size))
        return;

    Texture2D< uint >                vbuf0 = GetResource(g_Push.ID_VBuf0);
    Texture2D< uint >                vbuf1 = GetResource(g_Push.ID_VBuf1);
    StructuredBuffer< SceneGlobals > scene = GetResource(g_Push.ID_Scene);

    const float2   viewport               = float2(size);
    const float2   pixelCenter            = float2(px) + 0.5;
    const float4x4 viewProjUnjittered     = scene[0].camera.mViewProjUnjittered;
    const float4x4 viewProjUnjitteredPrev = scene[0].camera.mViewProjUnjitteredPrev;

    const uint v0 = vbuf0.Load(int3(int2(px), 0));
    if (VisIsSky(v0))
    {
        const float3 dirWS = ViewRayDirWS(PixelCenterToNDC(pixelCenter, viewport), scene[0].camera.mProj, scene[0].camera.mView);

        coreNormal[px]   = float2(0.0);
        coreMaterial[px] = float4(0.0);
        velocity[px]     = ScreenVelocity(float4(dirWS, 0.0), viewProjUnjittered, viewProjUnjitteredPrev);
        return;
    }

    const uint     v1       = vbuf1.Load(int3(int2(px), 0));
    const float4x4 viewProj = scene[0].camera.mViewProj;

    ResolvedSurface s;
    if (VisIsVoxel(v0))
        s = ResolveVoxelSurface(v0, v1, pixelCenter, viewport, viewProj, scene[0].frozenCamera.position);
    else
        s = ResolveMeshSurface(scene[0].buffers, viewProj, v0, v1, pixelCenter, viewport);

    coreNormal[px]   = OctEncode(s.N);
    coreMaterial[px] = float4(s.roughness, float(s.matClass) / 255.0, 0.0, 0.0);
    velocity[px]     = ScreenVelocity(float4(s.posWS, 1.0), viewProjUnjittered, viewProjUnjitteredPrev);
}
