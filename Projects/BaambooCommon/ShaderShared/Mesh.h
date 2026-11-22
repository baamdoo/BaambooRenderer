#pragma once
#include "Types.h"

#define LOD_COUNT 8

struct MeshLOD
{
    u32 iOffset;

    u32 mCount;
    u32 mOffset;
    u32 mvOffset;
    u32 mtOffset;

    float simplifyError; // for lod scale
};

struct MeshData
{
    u32 vOffset;

    float3 center;
    float  radius;

    u32     maxLOD;
    MeshLOD lods[LOD_COUNT];
};

struct BB_GPU_ALIGN(16) InstanceData
{
    u32 meshID;
    u32 transformID;
    u32 materialID;
    u32 mediumID;

    u32 visOffset;
    u32 isVoxel;
    u32 padding0;
    u32 padding1;
};

struct Meshlet
{
    u32 vertexOffset;
    u32 triangleOffset;
    u32 vertexCount;
    u32 triangleCount;

    float3 center;
    float  radius;
    float3 coneAxis;
    float  coneCutoff;
};

#ifndef __cplusplus
struct Vertex
{
    float3 position;
    float2 uv;
    float3 normal;
    float4 tangent;
};
#endif

struct IndirectCommandData
{
    u32 drawID;

    u32 groupCountX;
    u32 groupCountY;
    u32 groupCountZ;
};

struct TransformData
{
    mat4 mLocalToWorld;
    mat4 mWorldToLocal;
};

struct BoneTransformData
{
    mat4 mBoneTransform;
};

#ifdef __cplusplus
static_assert(sizeof(MeshLOD) == 24);
static_assert(sizeof(MeshData) == 4 + 12 + 4 + 4 + LOD_COUNT * sizeof(MeshLOD));
static_assert(sizeof(Meshlet) == 48);
static_assert(sizeof(Vertex) == 48 && offsetof(Vertex, uv) == 12 && offsetof(Vertex, normal) == 20 && offsetof(Vertex, tangent) == 32);
static_assert(sizeof(InstanceData) == 32);
static_assert(alignof(InstanceData) == 16);
static_assert(sizeof(BoneTransformData) == 64);
#endif
