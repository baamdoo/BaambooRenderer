#pragma once
#include "BaambooCore/Common.h"
#include "BaambooCore/Boundings.h"
#include "FileIO.hpp"

struct aiScene;
struct aiNode;
struct aiMesh;
struct aiMaterial;
struct aiString;
enum aiTextureType : u32;

struct MeshData
{
	std::string name;
	BoundingBox aabb;

	std::vector< Vertex > vertices;
	std::vector< Index >  indices;

	std::string albedoTextureFilename;
	std::string normalTextureFilename;
	std::string specularTextureFilename;
	std::string aoTextureFilename;
	std::string metallicTextureFilename;
	std::string roughnessTextureFilename;
	std::string emissiveTextureFilename;
};

struct DrawData
{
	u32 indexCount;
	u32 instanceCount;
	u32 firstIndex;
	u32 vertexOffset;
	u32 firstInstance;

	u32	materialIndex;
	u32 transformIndex;
	u32 transformCount;
};

struct MeshDescriptor
{
	bool bFlipY = false;
	bool bOptimize = false;
	bool bWindingCW = false;
};

namespace baamboo
{

class ModelLoader
{
public:
	struct Node
	{
		~Node()
		{
			for (auto pChild : pChilds)
				RELEASE(pChild);
		}

		Node* pParent;
		std::vector< Node* > pChilds;

		std::vector< MeshData >  meshes;
		std::vector< u32 >       materialIndices;
		std::vector< DrawData >  draws;
		BoundingBox              aabb;
	};

public:
	ModelLoader(fs::path filepath, MeshDescriptor descriptor);
	~ModelLoader()
	{
		RELEASE(pRoot);
	}

	Node*    pRoot;
	fs::path filepath;

private:
	void ProcessNode(aiNode* node, const aiScene* scene, Node* currentNode, MeshDescriptor descriptor);
	void ProcessMesh(aiMesh* mesh, const aiScene* scene, Node* currentNode, MeshDescriptor descriptor);

	std::string GetTextureFilename(aiMaterial* mat, aiTextureType type);
};

} // namespace baamboo