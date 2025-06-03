#pragma once
#include "Boundings.h"
#include "Utils/FileIO.hpp"

struct aiScene;
struct aiNode;
struct aiMesh;
struct aiMaterial;
struct aiString;

enum aiTextureType : i32;
enum class eRendererAPI;

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

struct MeshDescriptor
{
	eRendererAPI rendererAPI;

	bool bOptimize = true;
	bool bWindingCW = false;
};

namespace baamboo
{

class ModelLoader
{
public:
	struct Node
	{
		~Node();

		Node*                pParent = nullptr;
		std::vector< Node* > pChilds;

		std::vector< MeshData > meshes;
		std::vector< u32 >      materialIndices;
		BoundingBox             aabb = {};
	};

public:
	ModelLoader(fs::path filepath, MeshDescriptor descriptor);
	~ModelLoader();

	Node*    pRoot = nullptr;
	fs::path filepath;

private:
	void ProcessNode(aiNode* node, const aiScene* scene, Node* currentNode, MeshDescriptor descriptor);
	void ProcessMesh(aiMesh* mesh, const aiScene* scene, Node* currentNode, MeshDescriptor descriptor);

	std::string GetTextureFilename(aiMaterial* mat, aiTextureType type);
};

} // namespace baamboo