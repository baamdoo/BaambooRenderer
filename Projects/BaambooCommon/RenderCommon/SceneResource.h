#pragma once
#include "RenderResources.h"
#include "SceneRenderView.h"
#include "ShaderShared/Medium.h"
#include "ShaderShared/Mesh.h"
#include "ShaderShared/Scene.h"

#include <array>
#include <string>
#include <unordered_map>
#include <vector>

namespace render
{

class CommandContext;
class RenderDevice;

//-------------------------------------------------------------------------
// Scene Resource
//-------------------------------------------------------------------------
class BAAMBOO_API SceneResource
{
public:
    struct MediumBuffers
    {
        std::vector< MediumData > media;
        std::vector< u32 > densityWords;
    };
    static MediumBuffers BuildMediumBuffers(const SceneRenderView& renderView);

    explicit SceneResource(RenderDevice& rd);
    ~SceneResource() = default;

    void UpdateSceneResources(const SceneRenderView& sceneView, CommandContext& context);

    [[nodiscard]]
    Arc< TopLevelAccelerationStructure > GetTLAS() const { return m_pTLAS; }

    [[nodiscard]]
    Arc< Buffer > GetMeshDataBuffer() const { return m_MeshData.pBuffer; }

    [[nodiscard]]
    const Arc< Buffer >& GetSceneGlobals() const { return m_SceneGlobalsBuffer[m_SceneGlobalsIndex]; }

    [[nodiscard]]
    u32 NumInstances() const { return m_SceneGlobalsData.buffers.instanceCount; }

    [[nodiscard]]
    u32 NumMeshletVisibilitySlots() const { return m_NumMeshletVisibilitySlots; }

private:
    using ComponentRevisions = std::array< u64, NumComponents >;

    static constexpr ComponentRevisions NeverBuilt()
    {
        ComponentRevisions revisions = {};
        revisions.fill(~0ull);
        return revisions;
    }

    struct BufferHandle
    {
        u32 offset = 0;
        u32 count  = 0;
    };

    struct LodHandles
    {
        BufferHandle indices;
        BufferHandle meshlets;
        BufferHandle meshletVertices;
        BufferHandle meshletTriangles;
    };

    struct GeometryPool
    {
        const char* name;
        u64         stride;
        RenderFlags usage;

        Arc< Buffer > pBuffer;
        u32           capacity = 0;
        u32           used     = 0;

        void Reserve(RenderDevice& rd, u32 end);
        BufferHandle Append(RenderDevice& rd, const void* pData, u32 count);
    };

    struct SceneDataBuffer
    {
        const char* name;
        u64         stride;
        RenderFlags usage;

        Arc< Buffer > pBuffer;
        u32           capacity = 0;

        ComponentRevisions builtRevisions = NeverBuilt();

        void Write(RenderDevice& rd, CommandContext& context, const void* pData, u32 count);
    };

    void ReserveGeometry(const SceneRenderView& sceneView);
    MeshData GetOrUploadMesh(const StaticMeshRenderView& meshView);
    Arc< BottomLevelAccelerationStructure > GetOrCreateBLAS(const StaticMeshRenderView& meshView);
    Arc< Texture > GetOrLoadTexture(const std::string& filepath, eTextureColorSpace colorSpace);
    void WriteTransforms(const SceneRenderView& sceneView, CommandContext& context);
    void WriteMaterials(const SceneRenderView& sceneView, CommandContext& context);
    void WriteMeshes(const SceneRenderView& sceneView, CommandContext& context);
    void WriteInstances(const SceneRenderView& sceneView, CommandContext& context);
    void BuildTLAS(const SceneRenderView& sceneView);
    void WriteMedia(const SceneRenderView& sceneView, CommandContext& context);
    void UpdateSceneGlobals(const SceneRenderView& sceneView, CommandContext& context);

    RenderDevice& m_RenderDevice;

    GeometryPool m_Vertices;
    GeometryPool m_Indices;
    GeometryPool m_Meshlets;
    GeometryPool m_MeshletVertices;
    GeometryPool m_MeshletTriangles;

    SceneDataBuffer m_MeshData;
    SceneDataBuffer m_Instances;
    SceneDataBuffer m_Transforms;
    SceneDataBuffer m_Materials;
    SceneDataBuffer m_MaterialTextures;
    SceneDataBuffer m_Media;
    SceneDataBuffer m_MediumDensity;
    SceneDataBuffer m_Light;

    std::vector< MeshData > m_MeshRows;

    u64 m_LastSceneRevision         = ~0ull;
    u32 m_NumMeshletVisibilitySlots = 0;

    SceneGlobals                                    m_SceneGlobalsData = {};
    std::array< Arc< Buffer >, kMaxFramesInFlight > m_SceneGlobalsBuffer;
    u32                                             m_SceneGlobalsIndex = 0;

    std::unordered_map< std::string, BufferHandle >   m_VertexCache;
    std::unordered_map< std::string, LodHandles >     m_LodCache;
    std::unordered_map< std::string, Arc< Texture > > m_TextureCache;

    Arc< TopLevelAccelerationStructure >                                       m_pTLAS;
    ComponentRevisions                                                         m_TLASBuiltRevisions = NeverBuilt();
    std::unordered_map< std::string, Arc< BottomLevelAccelerationStructure > > m_BLASCache;
    std::vector< BottomLevelAccelerationStructure* >                           m_PendingBLASBuilds;
};

} // namespace render
