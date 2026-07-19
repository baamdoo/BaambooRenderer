#define _CAMERA
#define _FROZENCAMERA
#define _LIGHT
#define _MESH
#define _TRANSFORM
#define _MATERIAL
#include "Common.hlsli"
#include "AtmosphereCommon.hlsli"
#include "Lighting.hlsli"
#include "LightCullingCommon.hlsli"
#include "SurfaceResolve.hlsli"

ConstantBuffer< DescriptorHeapIndex > g_DepthBuffer          : register(b6, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_AerialPerspectiveLUT : register(b7, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_CloudScatteringLUT   : register(b8, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_SkyboxLUT            : register(b9, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_OutSceneTexture      : register(b10, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_LtcMatrixLUT         : register(b11, ROOT_CONSTANT_SPACE); // (m00, m02, m11, m20)
ConstantBuffer< DescriptorHeapIndex > g_LtcAmplitudeLUT      : register(b12, ROOT_CONSTANT_SPACE); // (magnitude, F0_lerp, F90_lerp, edge)
ConstantBuffer< DescriptorHeapIndex > g_LightGridBuffer      : register(b13, ROOT_CONSTANT_SPACE); // uint2(offset, count) per cluster
ConstantBuffer< DescriptorHeapIndex > g_LightListDataBuffer  : register(b14, ROOT_CONSTANT_SPACE); // uint (type:3 + idx:29) packed
ConstantBuffer< DescriptorHeapIndex > g_VBuf0                : register(b15, ROOT_CONSTANT_SPACE); // visibility surface ID
ConstantBuffer< DescriptorHeapIndex > g_VBuf1                : register(b16, ROOT_CONSTANT_SPACE); // visibility primitive ID
ConstantBuffer< DescriptorHeapIndex > g_CoreNormal           : register(b17, ROOT_CONSTANT_SPACE); // RG16_SNORM signed oct normal
ConstantBuffer< DescriptorHeapIndex > g_CoreMaterial         : register(b18, ROOT_CONSTANT_SPACE); // R roughness (terrain: base Ng.y) | G matClass | B dice-level debug (Lt/5) | A micro cavity

ConstantBuffer< VoxelChunkDesc > g_VoxelChunkDesc : register(b1, space1);

cbuffer DebugConstants : register(b0, ROOT_CONSTANT_SPACE)
{
    uint g_DebugView;
};


static const float MIN_ROUGHNESS = 0.045;


[numthreads(16, 16, 1)]
void main(uint3 tID : SV_DispatchThreadID)
{
    RWTexture2D< float4 > OutSceneTexture = GetResource(g_OutSceneTexture.index);

    int2 texCoords = tID.xy;

    uint width, height;
    OutSceneTexture.GetDimensions(width, height);

    if (tID.x >= width || tID.y >= height)
        return;

    float2 uv = (float2(texCoords.xy) + 0.5) / float2(width, height);

    Texture2D< float > DepthBuffer = GetResource(g_DepthBuffer.index);
    float depth = DepthBuffer.SampleLevel(g_PointClampSampler, uv, 0);

    float3 color = 0.0;
    if (depth == 0.0)
    {
        TextureCube< float3 > SkyboxLUT = GetResource(g_SkyboxLUT.index);

        float3 rayDir = normalize(ReconstructWorldPos(uv, 0.0, g_Camera.mViewProjInv));

        color = SkyboxLUT.Sample(g_LinearClampSampler, rayDir).rgb;
    }
    else
    {
        Texture2D< uint > VBuf0 = GetResource(g_VBuf0.index);
        uint v0 = VBuf0.Load(int3(texCoords, 0));

        float3 albedo;
        float  ao;
        float3 N;
        uint   materialID;
        float3 emissive;
        float  roughness;
        float  metallic;

        if (VisIsTerrain(v0))
        {
            Texture2D< float2 > CoreNormal   = GetResource(g_CoreNormal.index);
            Texture2D< float4 > CoreMaterial = GetResource(g_CoreMaterial.index);

            // resolve composes the detail normal; core = (base Ng.y, matClass, Lt/5 debug, micro cavity)
            N           = OctDecode(CoreNormal.Load(int3(texCoords, 0)));
            float4 core = CoreMaterial.Load(int3(texCoords, 0));

            VoxelChunkDesc chunk = g_VoxelChunkDesc;

            // TODO(you)
            // fallback keeps terrain visible until the material path is ported
            const float3 surfaceAlbedo = float3(0.45, 0.38, 0.28);
            const float  surfaceRough  = 0.85;
            const float3 cliffAlbedo   = float3(0.30, 0.27, 0.22);
            const float  cliffRough    = 0.95;
            float sBlend = smoothstep(0.35, 0.65, saturate(1.0 - core.r)); // 0 flat .. 1 vertical

            albedo    = lerp(surfaceAlbedo, cliffAlbedo, sBlend);
            roughness = max(lerp(surfaceRough, cliffRough, sBlend), MIN_ROUGHNESS);

            // micro cavity: valleys darken (self-shadow proxy computed by resolve)
            if ((chunk.debugFlags & 4u) != 0u)
                albedo *= 1.0 - core.a;

            if ((chunk.debugFlags & 1u) != 0u)
            {
                uint lvl = (uint)round(core.b * 5.0);
                const float3 kLevelTint[6] = {
                    float3(0.55, 0.55, 0.55),
                    float3(0.25, 0.45, 0.95),
                    float3(0.20, 0.85, 0.85),
                    float3(0.25, 0.80, 0.25),
                    float3(0.95, 0.85, 0.25),
                    float3(0.95, 0.30, 0.25)
                };
                albedo = lerp(albedo, kLevelTint[min(lvl, 5u)], 0.8);
            }

            ao         = 1.0;
            materialID = INVALID_INDEX;
            emissive   = float3(0.0, 0.0, 0.0);
            metallic   = 0.0;
        }
        else
        {
            Texture2D< float2 > CoreNormal   = GetResource(g_CoreNormal.index);
            Texture2D< float4 > CoreMaterial = GetResource(g_CoreMaterial.index);
            Texture2D< uint >   VBuf1        = GetResource(g_VBuf1.index);

            N         = OctDecode(CoreNormal.Load(int3(texCoords, 0)));
            roughness = max(CoreMaterial.Load(int3(texCoords, 0)).r, MIN_ROUGHNESS);

            uint v1 = VBuf1.Load(int3(texCoords, 0));
            ResolvedMaterial m = ResolveMaterial(v0, v1, float2(texCoords) + 0.5, float2(width, height));

            albedo     = m.baseColor;
            ao         = m.ao;
            materialID = m.materialID;
            emissive   = m.emissive;
            metallic   = m.metallic;
        }

        float3 posWORLD = ReconstructWorldPos(uv, depth, g_Camera.mViewProjInv);

        float3 V  = normalize(posWORLD - g_Camera.posWORLD);
        float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);

        if (materialID != INVALID_INDEX)
        {
            StructuredBuffer< MaterialData > Materials = GetResource(g_Materials.index);
            MaterialData material = Materials[materialID];
            if (material.ior > 1.0)
            {
                float f0 = pow((material.ior - 1.0) / (material.ior + 1.0), 2.0);
                F0 = float3(f0, f0, f0);
            }
        }

        if (g_DebugView != 0)
        {
            float3 dbg = float3(0.0, 0.0, 0.0);
            if (g_DebugView == 1)
            {
                if (VisIsSky(v0))          dbg = float3(0.0, 0.0, 1.0);
                else if (VisIsTerrain(v0)) dbg = float3(1.0, 0.0, 0.0);
                else                       dbg = float3(0.0, 1.0, 0.0);
            }
            else if (g_DebugView == 2)
            {
                dbg = N * 0.5 + 0.5;
            }
            else if (g_DebugView == 3)
            {
                float3 nLive = N;
                if (VisIsMesh(v0))
                {
                    Texture2D< uint > VBuf1dbg = GetResource(g_VBuf1.index);

                    uint v1dbg = VBuf1dbg.Load(int3(texCoords, 0));
                    nLive = ResolveMeshSurface(v0, v1dbg, float2(texCoords) + 0.5, float2(width, height)).N;
                }
                dbg = nLive * 0.5 + 0.5;
            }
            else if (g_DebugView == 4) dbg = albedo;
            else if (g_DebugView == 5) dbg = float3(roughness, roughness, roughness);
            else if (g_DebugView == 6) dbg = float3(ao, ao, ao);
            else if (g_DebugView == 7)
            {
                float3 bcol = float3(0.0, 0.0, 0.0);
                if (VisIsMesh(v0))
                {
                    Texture2D< uint > VBuf1bg = GetResource(g_VBuf1.index);
                    uint v1bg = VBuf1bg.Load(int3(texCoords, 0));
                    bcol = DebugVisBary(v0, v1bg, float2(texCoords) + 0.5, float2(width, height));
                }
                dbg = bcol;
            }
            OutSceneTexture[texCoords] = float4(dbg, 1.0);
            return;
        }

        Texture2D< float4 > LtcMatrix    = GetResource(g_LtcMatrixLUT.index);
        Texture2D< float4 > LtcAmplitude = GetResource(g_LtcAmplitudeLUT.index);

        float3 Lo = float3(0.0, 0.0, 0.0);

        for (uint i = 0; i < g_Lights.numDirectionals; ++i)
        {
            Lo += ApplyDirectionalLight(g_Lights.directionals[i], N, V, albedo, metallic, roughness, F0);
        }

        float viewZ      = mul(g_FrozenCamera.mView, float4(posWORLD, 1.0)).z;
        uint  clusterIdx = PixelToClusterIdx(uint2(texCoords), viewZ, g_FrozenCamera.zNear, g_FrozenCamera.zFar, width, height);

        StructuredBuffer< uint2 > LightGrid     = GetResource(g_LightGridBuffer.index);
        StructuredBuffer< uint  > LightListData = GetResource(g_LightListDataBuffer.index);

        uint2 cell = LightGrid[clusterIdx];
        [loop] for (uint i = 0; i < cell.y; ++i)
        {
            uint packed = LightListData[cell.x + i];

            uint type, idx;
            DecodeLightIndex(packed, type, idx);

            switch (type)
            {
            case LIGHT_TYPE_SPOT:
                Lo += ApplySpotLight(g_Lights.spots[idx], posWORLD, N, V, albedo, metallic, roughness, F0);
				break;
            case LIGHT_TYPE_SPHERE:
            	Lo += ApplySphereLight(g_Lights.spheres[idx], posWORLD, N, V, albedo, metallic, roughness, F0);
                break;
            case LIGHT_TYPE_TUBE:
                Lo += ApplyTubeLight(g_Lights.tubes[idx], posWORLD, N, V, albedo, metallic, roughness, F0);
                break;
            case LIGHT_TYPE_AREA:
				Lo += ApplyAreaLight(g_Lights.areas[idx], posWORLD, N, V, albedo, metallic, roughness, F0, LtcMatrix, LtcAmplitude);
                break;
            case LIGHT_TYPE_DISK:
                Lo += ApplyDiskLight(g_Lights.disks[idx], posWORLD, N, V, albedo, metallic, roughness, F0, LtcMatrix, LtcAmplitude);
                break;
            default:
                break;
            }
        }

        // TODO. sky IBL
        color = Lo + emissive;

        // Aerial perspective
        {
            Texture3D< float4 > AerialPerspectiveLUT = GetResource(g_AerialPerspectiveLUT.index);

            float viewDistance = max(0.0, length(posWORLD));

            uint3 texSize;
            AerialPerspectiveLUT.GetDimensions(texSize.x, texSize.y, texSize.z);

            float slice = viewDistance * (1.0 / AP_KM_PER_SLICE) * M_TO_KM;
            float w = sqrt(slice / float(texSize.z));
            slice = w * texSize.z;

            float4 ap = AerialPerspectiveLUT.SampleLevel(g_LinearClampSampler, float3(uv, w), 0);

            // prevents an abrupt appearance of fog on objects close to the camera
            float weight = 1.0;
            if (slice < sqrt(0.5))
            {
                weight = clamp((slice * slice * 2.0), 0.0, 1.0);
            }
            ap.rgb *= weight;
            ap.a = 1.0 - weight * (1.0 - ap.a);

            // FinalColor = (SurfaceColor * Transmittance) + InScatteredLight
            color = color * ap.a + ap.rgb;
        }
    }
    // Cloud
    {
        Texture2D< float4 > CloudScatteringLUT = GetResource(g_CloudScatteringLUT.index);

        float4 cloud = CloudScatteringLUT.SampleLevel(g_LinearClampSampler, uv, 0);

        color = color * cloud.a + cloud.rgb;
    }

    OutSceneTexture[texCoords] = float4(color, 1.0);
}
