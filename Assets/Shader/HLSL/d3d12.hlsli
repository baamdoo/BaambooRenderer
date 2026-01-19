#ifndef _HLSL_D3D12_HEADER
#define _HLSL_D3D12_HEADER

typedef uint2 D3D12_GPU_VIRTUAL_ADDRESS; // uint64

struct D3D12_DRAW_ARGUMENTS
{
    uint VertexCountPerInstance;
    uint InstanceCount;
    uint StartVertexLocation;
    uint StartInstanceLocation;
};

struct D3D12_DRAW_INDEXED_ARGUMENTS
{
    uint IndexCountPerInstance;
    uint InstanceCount;
    uint StartIndexLocation;
    int  BaseVertexLocation;
    uint StartInstanceLocation;
};

struct D3D12_DISPATCH_ARGUMENTS
{
    uint ThreadGroupCountX;
    uint ThreadGroupCountY;
    uint ThreadGroupCountZ;
};

struct D3D12_DISPATCH_MESH_ARGUMENTS
{
    uint ThreadGroupCountX;
    uint ThreadGroupCountY;
    uint ThreadGroupCountZ;
};

struct D3D12_VERTEX_BUFFER_VIEW
{
    D3D12_GPU_VIRTUAL_ADDRESS BufferLocation;
    uint                      SizeInBytes;
    uint                      StrideInBytes;
};

struct D3D12_INDEX_BUFFER_VIEW
{
    D3D12_GPU_VIRTUAL_ADDRESS BufferLocation;
    uint                      SizeInBytes;
    uint                      Format;
};

#endif //_HLSL_D3D12_HEADER