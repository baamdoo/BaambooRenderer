#include "ShaderShared/Scene.h"
#include "ShaderShared/Mesh.h"
#include "ShaderShared/Culling.h"
#include "Common/Binding.slangh"
#include "Common/Culling.slangh"

struct InstanceCullingPush
{
    InstanceCullingConstants constants;

    uint ID_Scene;
    uint ID_IndirectCommands;
    uint ID_DrawIDs;
    uint ID_DrawCount;
    uint ID_VisibilityBuffer;
    uint ID_HiZTexture;
};

[[vk::push_constant]] ConstantBuffer< InstanceCullingPush > g_Push : register(b0, ROOT_CONSTANT_SPACE);

uint CalculateLODLevelSSE(MeshData mesh, float3 centerWS, float instMaxScale, float3 cameraPos, float fovScale, float viewportHeight, float sseThresholdPx)
{
    const float dist = max(length(cameraPos - centerWS) - mesh.radius * instMaxScale, 1e-3);
    const float k    = abs(fovScale) * viewportHeight / (2.0 * dist);

    uint chosen = 0u;
    for (uint i = mesh.maxLOD; i > 0u; --i)
    {
        const float pxError = mesh.lods[i].simplifyError * instMaxScale * k;
        if (pxError <= sseThresholdPx)
        {
            chosen = i;
            break;
        }
    }
    return chosen;
}

[numthreads(64, 1, 1)]
void main(uint3 Gid : SV_DispatchThreadID)
{
    StructuredBuffer< SceneGlobals > sceneBuf = GetResource(g_Push.ID_Scene);
    const SceneBufferIndices buffers = sceneBuf[0].buffers;

    const uint instanceID = Gid.x;
    if (instanceID >= buffers.instanceCount)
        return;

    const uint phase = g_Push.constants.cullingPhase;

    RWStructuredBuffer< uint > visibility = GetResource(g_Push.ID_VisibilityBuffer);
    const uint prevVisibility = visibility[instanceID];
    if (phase == kCullPhase1 && prevVisibility == 0u)
        return;

    StructuredBuffer< MeshData >      meshes     = GetResource(buffers.meshes);
    StructuredBuffer< InstanceData >  instances  = GetResource(buffers.instances);
    StructuredBuffer< TransformData > transforms = GetResource(buffers.transforms);

    const InstanceData  instance  = instances[instanceID];
    const MeshData      mesh      = meshes[instance.meshID];
    const TransformData transform = transforms[instance.transformID];
    const CullData      cull      = sceneBuf[0].cull;

    const float  maxScale = MaxAxisScale(transform.mLocalToWorld);
    const float3 center   = mul(transform.mLocalToWorld, float4(mesh.center, 1.0)).xyz;
    const float  radius   = mesh.radius * maxScale;

    const uint lod = CalculateLODLevelSSE(
        mesh, center, maxScale,
        sceneBuf[0].frozenCamera.position, sceneBuf[0].frozenCamera.mProj[1][1],
        cull.viewportHeight, cull.sseThresholdPx);

    bool bVisible = !IsFrustumCulled(cull.frustum, center, radius);
    if (phase == kCullPhase2)
    {
        if (bVisible)
        {
            Texture2D< float > hiZ = GetResource(g_Push.ID_HiZTexture);
            bVisible = !IsOccluded(center, radius, sceneBuf[0].camera, hiZ, uint2(cull.hiZWidth, cull.hiZHeight));
        }
        visibility[instanceID] = bVisible ? 1u : 0u;
    }

    // Phase 2 emits only newly visible instances, or every visible one when meshlet occlusion needs them for disocclusion
    const bool bMeshletOcclusion = (cull.cullFlags & kCullFlagMeshletOcclusion) != 0u;
    const bool bEmitLate         = bMeshletOcclusion || prevVisibility == 0u;
    if (!bVisible || (phase == kCullPhase2 && !bEmitLate))
        return;

    RWByteAddressBuffer                       drawCount        = GetResource(g_Push.ID_DrawCount);
    RWStructuredBuffer< IndirectCommandData > indirectCommands = GetResource(g_Push.ID_IndirectCommands);
    RWStructuredBuffer< uint >                drawIDs          = GetResource(g_Push.ID_DrawIDs);

    uint outID;
    drawCount.InterlockedAdd(0, 1u, outID);

    IndirectCommandData command;
    command.drawID      = outID;
    command.groupCountX = (mesh.lods[lod].mCount + 31u) / 32u;
    command.groupCountY = 1u;
    command.groupCountZ = 1u;
    indirectCommands[outID] = command;

    // Phase 2 emits only frustum-visible instances, so a set previous bit means phase 1 drew this one
    const uint earlyBit = (phase == kCullPhase2 && prevVisibility != 0u) ? kDrawIDEarlyBit : 0u;
    drawIDs[outID] = earlyBit | (lod << 24) | (instanceID & 0x00FFFFFFu);
}
