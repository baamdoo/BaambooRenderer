#pragma once
#include "Boundings.h"
#include "AnimationTypes.h"
#include "Utils/FileIO.hpp"

struct aiScene;
struct aiNode;
struct aiMesh;
struct aiMaterial;
struct aiAnimation;
struct aiString;

enum aiTextureType : i32;
enum class eRendererAPI;

namespace baamboo
{

struct MeshLODData
{
	std::vector< Index >   indices;
	std::vector< Meshlet > meshlets;
	std::vector< u32 >     meshletVertices;
	std::vector< u32 >     meshletTriangles; // three u8s packed in u32

	float simplifyError = 0.0f;
};

struct MeshData
{
	std::string name;
	BoundingBox aabb;

	// geometry
	eVertexFormat vertexFormat = eVertexFormat::P3U2N3T3;

	std::vector< Vertex >          vertices;
	std::vector< VertexP3U2N3T3S > skinnedVertices;

	std::vector< MeshLODData > lods;

	// material
	u32 materialIndex = 0;

	// skinning
	std::vector< u32 >    boneIndices;
	std::vector< float4 > boneWeights;
	bool bHasSkinnedData = false;

	inline u32 GetVertexCount() const
	{
		return bHasSkinnedData ? static_cast<u32>(skinnedVertices.size()) : static_cast<u32>(vertices.size());
	}

	inline const void* GetVertexData() const
	{
		return bHasSkinnedData ? (void*)(skinnedVertices.data()) : (void*)(vertices.data());
	}

	inline u32 VertexSize() const
	{
		return GetVertexSize(vertexFormat);
	}
};

struct MaterialData
{
	std::string name;

	float3 diffuse          = float3(1.0f);
	float3 specularColor    = float3(1.0f);
	float  specularStrength = 1.0f;

	float shininess = 32.0f;
	float metallic  = 0.0f;
	float roughness = 0.5f;

	float ior                = 1.0f;
	float clearcoat          = 0.0f;
	float clearcoatRoughness = 0.0f;
	float alphaCutoff        = 0.0f;

	float anisotropy         = 0.0f;
	float anisotropyRotation = 0.0f;
	float transmission       = 0.0f;

	float3 sheenColor     = float3(0.0f);
	float  sheenRoughness = 0.0f;

	std::string albedoPath;
	std::string normalPath;
	std::string metallicPath;
	std::string roughnessPath;
	std::string aoPath;
	std::string emissivePath;
	std::string clearcoatPath;
	std::string sheenPath;
	std::string anisotropyPath;
	std::string transmissionPath;
};

struct MeshDescriptor
{
	eRendererAPI rendererAPI;

	fs::path rootPath;

	float scale = 1.0f;

	bool bLoadAnimations   = false;
	bool bOptimize         = false;
	bool bWindingCW        = false;
	bool bConvertToLeftHanded = true;
	bool bGenerateMeshlets = false;
	
	u8 numLODs = 1;
};

struct ModelNode
{
	~ModelNode();

	std::string name;

	ModelNode*                pParent = nullptr;
	std::vector< ModelNode* > pChilds;

	std::vector< MeshData > meshes;
	std::vector< u32 >      meshIndices;
	std::vector< u32 >      materialIndices;
	BoundingBox             aabb = {};

	mat4 mTransform;
	i32 parentIndex = -1;
};

class ModelLoader
{
public:
	ModelLoader(fs::path filepath, MeshDescriptor descriptor);
	~ModelLoader();

	ModelNode* GetRootNode() const { return m_pRootNode; }

	const std::vector< MeshData >& GetMeshes() const { return m_Meshes; }
	const std::vector< MaterialData >& GetMaterials() const { return m_Materials; }

	bool HasAnimations() const { return m_AnimationData.bHasAnimation; }
	const AnimationData& GetAnimationData() const { return m_AnimationData; }

private:
	void ProcessNode(aiNode* node, const aiScene* scene, ModelNode* currentNode, MeshDescriptor descriptor);
	void ProcessMesh(aiMesh* mesh, const aiScene* scene, ModelNode* currentNode, MeshDescriptor descriptor);

	void ProcessAnimations(const aiScene* scene);
	void ProcessBoneHierarchy(aiNode* node, const aiScene* scene, i32 parentIndex = -1);
	void ProcessBoneWeights(aiMesh* mesh, MeshData& meshData);
	AnimationClip ProcessAnimationClip(aiAnimation* animation);

	// Reference: https://github.com/zeux/meshoptimizer
	void GenerateMeshlets(MeshData& meshData, u8 lodLevel);

	std::string GetTextureFilename(aiMaterial* mat, aiTextureType type);

private:
	ModelNode* m_pRootNode = nullptr;

	std::vector< MeshData >     m_Meshes;
	std::vector< MaterialData > m_Materials;
	AnimationData               m_AnimationData;

	static u64 ms_GlobalMeshletVertexOffset;
	static u64 ms_GlobalMeshletTriangleOffset;

	u32 m_BoneCount = 0;
	std::unordered_map< std::string, u32 > m_BoneMap;
};

} // namespace baamboo
