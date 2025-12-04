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

struct MeshData
{
	std::string name;
	BoundingBox aabb;

	// geometry
	eVertexFormat vertexFormat = eVertexFormat::P3U2N3T3;

	std::vector< Vertex >          vertices;
	std::vector< VertexP3U2N3T3S > skinnedVertices;
	std::vector< Index >           indices;

	// material
	u32 materialIndex = 0;

	// skinning
	std::vector< u32 >    boneIndices;
	std::vector< float4 > boneWeights;
	bool bHasSkinnedData = false;

	std::vector< Meshlet > meshlets;
	std::vector< u32 >     meshletVertices;
	std::vector< u8 >      meshletTriangles;

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

	float3 ambient   = float3(0.2f);
	float3 diffuse   = float3(0.8f);
	float3 specular  = float3(0.1f);
	float  shininess = 32.0f;
	float  metallic  = 0.0f;
	float  roughness = 0.5f;
	float  ao        = 1.0f;

	std::string albedoPath;
	std::string normalPath;
	std::string metallicPath;
	std::string roughnessPath;
	std::string aoPath;
	std::string emissivePath;
};

struct MeshDescriptor
{
	eRendererAPI rendererAPI;

	fs::path rootPath;

	float scale = 1.0f;

	bool bLoadAnimations   = false;
	bool bOptimize         = false;
	bool bWindingCW        = false;
	bool bGenerateMeshlets = false;
};

struct ModelNode
{
	~ModelNode();

	ModelNode*                pParent = nullptr;
	std::vector< ModelNode* > pChilds;

	std::vector< MeshData > meshes;
	std::vector< u32 >      materialIndices;
	BoundingBox             aabb = {};

	std::string name;
	mat4 mTransform;
	std::vector< u32 > meshIndices;
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

	void GenerateMeshlets(MeshData& meshData, bool bOptimizeVertexCache);

	std::string GetTextureFilename(aiMaterial* mat, aiTextureType type);

private:
	ModelNode* m_pRootNode = nullptr;

	std::vector< MeshData >     m_Meshes;
	std::vector< MaterialData > m_Materials;
	AnimationData               m_AnimationData;

	std::unordered_map< std::string, u32 > m_BoneMap;

	u32 m_BoneCount = 0;
};

} // namespace baamboo