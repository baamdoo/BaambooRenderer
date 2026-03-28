#define _CAMERA
#define _TRANSFORM
#define _MESH
#include "Common.hlsli"

cbuffer PushConstants : register(b0, ROOT_CONSTANT_SPACE)
{
    uint g_DrawID;
};


static StructuredBuffer< Meshlet >       Meshlets   = GetResource(g_Meshlets.index);
static StructuredBuffer< MeshData >      Meshes     = GetResource(g_Meshes.index);
static StructuredBuffer< InstanceData >  Instances  = GetResource(g_Instances.index);
static StructuredBuffer< TransformData > Transforms = GetResource(g_Transforms.index);

bool ConeCull(float4 cone, float4 sphere, float3 viewPos)
{
    return dot(cone.xyz, normalize(sphere.xyz - viewPos)) > cone.w * length(sphere.xyz - viewPos) + sphere.w;
}

struct AmplificationPayload
{
	uint meshletIndices[32];
};

groupshared AmplificationPayload PayloadData;

[numthreads(32, 1, 1)]
void main(uint3 Gid : SV_DispatchThreadID, uint3 GTid : SV_GroupThreadID)
{
	InstanceData instance = Instances[g_DrawID];
	MeshData     mesh     = Meshes[instance.meshID];

    uint mi = mesh.mOffset + Gid.x;
    uint ti = GTid.x;

    bool accept = false;
    if (Gid.x < mesh.mCount)
    {
        Meshlet       meshlet   = Meshlets[mi];
        TransformData transform = Transforms[instance.transformID];

        float scaleX   = length(transform.mLocalToWorld[0].xyz);
        float scaleY   = length(transform.mLocalToWorld[1].xyz);
        float scaleZ   = length(transform.mLocalToWorld[2].xyz);
        float maxScale = max(scaleX, max(scaleY, scaleZ));

        float3 coneAxisWORLD     = normalize((mul(transform.mLocalToWorld, float4(meshlet.coneAxisX, meshlet.coneAxisY, meshlet.coneAxisZ, 0.0))).xyz);
        float3 sphereBoundsWORLD = (mul(transform.mLocalToWorld, float4(meshlet.centerX, meshlet.centerY, meshlet.centerZ, 1.0))).xyz;

        accept = !ConeCull(float4(coneAxisWORLD, meshlet.coneCutoff), float4(sphereBoundsWORLD, meshlet.radius * maxScale), g_Camera.posWORLD);
    }

    uint payloadIndex = WavePrefixCountBits(accept);

    if (accept)
        PayloadData.meshletIndices[payloadIndex] = mi;

    uint numMeshlets = WaveActiveCountBits(accept);
	DispatchMesh(numMeshlets, 1, 1, PayloadData);
}