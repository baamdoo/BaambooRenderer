#define _CAMERA
#define _MESH
#define _CULL
#define _TRANSFORM
#include "Common.hlsli"
#include "HelperFunctions.hlsli"

cbuffer PushConstants : register(b0, ROOT_CONSTANT_SPACE)
{
    uint g_NumInstances;
};

RWStructuredBuffer< IndirectCommandData > g_IndirectCommands : register(u0, space1);
RWByteAddressBuffer                       g_DrawCount        : register(u1, space1);


static StructuredBuffer< MeshData >      Meshes     = GetResource(g_Meshes.index);
static StructuredBuffer< InstanceData >  Instances  = GetResource(g_Instances.index);
static StructuredBuffer< TransformData > Transforms = GetResource(g_Transforms.index);

[numthreads(64, 1, 1)]
void main(uint3 Gid : SV_DispatchThreadID, uint3 GTid : SV_GroupThreadID)
{
    uint instanceID = Gid.x;
    if (instanceID >= g_NumInstances)
    {
        return;
    }

    InstanceData  instance  = Instances[instanceID];
    MeshData      mesh      = Meshes[instance.meshID];
    TransformData transform = Transforms[instance.transformID];

    float scaleX = length(transform.mLocalToWorld[0].xyz);
    float scaleY = length(transform.mLocalToWorld[1].xyz);
    float scaleZ = length(transform.mLocalToWorld[2].xyz);
    float maxScale = max(scaleX, max(scaleY, scaleZ));

    float4 center = mul(transform.mLocalToWorld, float4(mesh.centerX, mesh.centerY, mesh.centerZ, 1.0));
    float  radius = mesh.radius * maxScale;

    bool bVisible = true;
    for (int i = 0; i < 5; ++i)
        bVisible = bVisible && dot(g_CullData.frustum[i], center) + radius > 0.0;

    if (bVisible)
    {
        uint outID;
        //InterlockedAdd(g_DrawCount[0], 1, outID);
        g_DrawCount.InterlockedAdd(0, 1, outID);

        g_IndirectCommands[outID].drawID = instanceID;

        g_IndirectCommands[outID].groupCountX = roundUpAndDivide(mesh.mCount, 32u);
        g_IndirectCommands[outID].groupCountY = 1;
        g_IndirectCommands[outID].groupCountZ = 1;
    }
}