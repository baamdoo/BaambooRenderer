#pragma once
#include "Camera.h"
#include "Atmosphere.h"

struct SceneBufferIndices
{
    u32 vertices;
    u32 indices;
    u32 meshlets;
    u32 meshletVertices;

    u32 meshletTriangles;
    u32 meshes;
    u32 instances;
    u32 transforms;

    u32 materials;
    u32 materialTextures;
    u32 media;
    u32 mediumDensity;

    u32 lights;
    u32 instanceCount;
    u32 mediumCount;
    u32 voxelMeshBase;

    u32 tlas;
    u32 padding0;
    u32 padding1;
    u32 padding2;
};

struct SceneGlobals
{
    CameraData           camera;
    FrozenCameraData     frozenCamera;
    CullData             cull;
    SceneEnvironmentData environment;
    SceneBufferIndices   buffers;
};
