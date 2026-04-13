#define _CAMERA
#define _MESH
#define _CULL
#define _TRANSFORM
#include "Common.hlsli"
#include "HelperFunctions.hlsli"
#include "OcclusionCulling.hlsli"


#define PHASE1_CULL  0u
#define PHASE2_CULL  1u

cbuffer PushConstants : register(b0, ROOT_CONSTANT_SPACE)
{
    uint g_NumInstances;
    uint g_CullingPhase;
    uint g_HiZMipCount;
    uint g_HiZWidth;
    uint g_HiZHeight;
};

// All buffer/texture bindings via DescriptorHeapIndex (bindless pattern)
ConstantBuffer< DescriptorHeapIndex > g_IndirectCommands : register(b1, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_DrawCount        : register(b2, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_VisibilityBuffer : register(b3, ROOT_CONSTANT_SPACE);
ConstantBuffer< DescriptorHeapIndex > g_HiZTexture       : register(b4, ROOT_CONSTANT_SPACE);

static StructuredBuffer< MeshData >      Meshes     = GetResource(g_Meshes.index);
static StructuredBuffer< InstanceData >  Instances  = GetResource(g_Instances.index);
static StructuredBuffer< TransformData > Transforms = GetResource(g_Transforms.index);


void EmitDrawCommand(uint instanceID, MeshData mesh, uint lod,
                     RWStructuredBuffer< IndirectCommandData > IndirectCommands,
                     RWByteAddressBuffer DrawCount)
{
    uint outID;
    DrawCount.InterlockedAdd(0, 1, outID);

    IndirectCommands[outID].drawID     = instanceID;
    IndirectCommands[outID].groupCountX = roundUpAndDivide(mesh.lods[lod].mCount, 32u);
    IndirectCommands[outID].groupCountY = 1;
    IndirectCommands[outID].groupCountZ = 1;
}


[numthreads(64, 1, 1)]
void main(uint3 Gid : SV_DispatchThreadID, uint3 GTid : SV_GroupThreadID)
{
    uint instanceID = Gid.x;
    if (instanceID >= g_NumInstances)
    {
        return;
    }

    // --- Phase 1: only render instances visible last frame (read-only) ---
    RWStructuredBuffer< uint > VisibilityBuffer = GetResource(g_VisibilityBuffer.index);
    if (g_CullingPhase == PHASE1_CULL && VisibilityBuffer[instanceID] == 0u)
    {
        return; // Not visible last frame — Phase 2 will catch it
    }

    RWStructuredBuffer< IndirectCommandData > IndirectCommands = GetResource(g_IndirectCommands.index);
    RWByteAddressBuffer                       DrawCount        = GetResource(g_DrawCount.index);

    InstanceData  instance  = Instances[instanceID];
    MeshData      mesh      = Meshes[instance.meshID];
    TransformData transform = Transforms[instance.transformID];

    float scaleX = length(transform.mLocalToWorld[0].xyz);
    float scaleY = length(transform.mLocalToWorld[1].xyz);
    float scaleZ = length(transform.mLocalToWorld[2].xyz);
    float maxScale = max(scaleX, max(scaleY, scaleZ));

    float4 center = mul(transform.mLocalToWorld, float4(mesh.centerX, mesh.centerY, mesh.centerZ, 1.0));
    float  radius = mesh.radius * maxScale;

    uint lod = CalculateLODLevel(g_Camera.posWORLD, center.xyz, radius, float2(g_CullData.lodNear, g_CullData.lodFar), mesh.maxLOD);

    // --- Frustum culling ---
    bool bVisible = true;
    for (int i = 0; i < 5; ++i)
        bVisible = bVisible && dot(g_CullData.frustum[i], center) + radius > 0.0;

    // --- Occlusion culling (Phase 2 only — Phase 1 skips HZB) ---
    uint prevVisibility = VisibilityBuffer[instanceID];
    if (bVisible && g_CullingPhase == PHASE2_CULL)
    {
        Texture2D< float > HiZ = GetResource(g_HiZTexture.index);

        bool bOccluded = IsOccluded(
            center.xyz,
            radius,
            g_Camera.mView,
            g_Camera.mProj[0][0],
            g_Camera.mProj[1][1],
            g_Camera.zNear,
            HiZ,
            g_LinearClampMinSampler,
            g_HiZWidth,
            g_HiZHeight);
        bVisible = bVisible && !bOccluded;

        VisibilityBuffer[instanceID] = bVisible ? 1u : 0u; // Update visibility bit for next frame (Phase 1 read)
    }

    // --- Emit draw (Phase 1 survivors, or Phase 2 newly visible) ---
    if (bVisible && (g_CullingPhase == PHASE1_CULL || prevVisibility == 0u))
    {
        EmitDrawCommand(instanceID, mesh, lod, IndirectCommands, DrawCount);
    }
}
