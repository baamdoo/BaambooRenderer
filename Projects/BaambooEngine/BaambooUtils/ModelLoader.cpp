#include "BaambooPch.h"
#include "ModelLoader.h"

#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/Importer.hpp>

namespace baamboo
{

ModelLoader::ModelLoader(fs::path filepath_, MeshDescriptor descriptor)
	: filepath(filepath_)
{
	i32 importFlags = 
		aiProcess_CalcTangentSpace |
		aiProcess_Triangulate |
		aiProcess_SortByPType |
		aiProcess_PreTransformVertices |
		aiProcess_GenSmoothNormals |
		//aiProcess_GenUVCoords |
		aiProcess_RemoveRedundantMaterials |
		//aiProcess_FindDegenerates |
		aiProcess_GenBoundingBoxes |
		aiProcess_ValidateDataStructure |
		aiProcess_JoinIdenticalVertices |
		aiProcess_ConvertToLeftHanded;
	importFlags |= (descriptor.bOptimize ? aiProcess_OptimizeMeshes | aiProcess_ImproveCacheLocality : 0);
	//importFlags |= (descriptor.bWindingCW ? aiProcess_FlipWindingOrder : 0);
	//importFlags |= ((descriptor.rendererAPI == eRendererAPI::D3D11 || descriptor.rendererAPI == eRendererAPI::D3D12) ? aiProcess_ConvertToLeftHanded : 0);

	Assimp::Importer importer;
	const auto aiScene = importer.ReadFile(filepath.string(), importFlags);
	if (!aiScene || aiScene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !aiScene->mRootNode) 
	{
		printf("%s\n", importer.GetErrorString());
		return;
	}

	pRoot = new Node();
	pRoot->aabb = BoundingBox(float3(std::numeric_limits<float>::max()), float3(std::numeric_limits<float>::min()));
	ProcessNode(aiScene->mRootNode, aiScene, pRoot, descriptor);
}

void ModelLoader::ProcessNode(aiNode* node, const aiScene* scene, Node* currentNode, MeshDescriptor descriptor)
{
	for (u32 i = 0; i < node->mNumMeshes; i++) 
	{
		aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
		ProcessMesh(mesh, scene, currentNode, descriptor);
	}

	currentNode->aabb = BoundingBox(float3(std::numeric_limits<float>::max()), float3(std::numeric_limits<float>::min()));
	for (auto mesh : currentNode->meshes)
	{
		currentNode->aabb = BoundingBox::Union(currentNode->aabb, mesh.aabb);
	}

	currentNode->pChilds.reserve(node->mNumChildren);
	for (u32 i = 0; i < node->mNumChildren; i++) 
	{
		Node* child = new Node();
		currentNode->pChilds.push_back(child);

		ProcessNode(node->mChildren[i], scene, child, descriptor);
	}
}

void ModelLoader::ProcessMesh(aiMesh* mesh, const aiScene* scene, Node* currentNode, MeshDescriptor descriptor)
{
	assert(mesh->HasPositions());
	assert(mesh->HasNormals());

	MeshData meshData = {};
	meshData.name = mesh->mName.C_Str();
	meshData.aabb = BoundingBox(*(float3*)(&mesh->mAABB.mMin), *(float3*)(&mesh->mAABB.mMax));
	currentNode->aabb = BoundingBox::Union(currentNode->aabb, meshData.aabb);

	// **
	// Process geometries
	// **
	for (uint32_t i = 0; i < mesh->mNumVertices; i++)
	{
		Vertex vertex = {};

		vertex.position.x = mesh->mVertices[i].x;
		vertex.position.y = mesh->mVertices[i].y;
		vertex.position.z = mesh->mVertices[i].z;

		vertex.normal.x = mesh->mNormals[i].x;
		vertex.normal.y = mesh->mNormals[i].y;
		vertex.normal.z = mesh->mNormals[i].z;

		if (mesh->HasTextureCoords(0)) 
		{
			vertex.uv.x = mesh->mTextureCoords[0][i].x;
			vertex.uv.y = mesh->mTextureCoords[0][i].y;
		}

		if (mesh->HasTangentsAndBitangents())
		{
			vertex.tangent.x = mesh->mTangents[i].x;
			vertex.tangent.y = mesh->mTangents[i].y;
			vertex.tangent.z = mesh->mTangents[i].z;
			// vertex.bitangent.x = mesh->mBitangents[i].x;
			// vertex.bitangent.y = mesh->mBitangents[i].y;
			// vertex.bitangent.z = mesh->mBitangents[i].z;
		}

		meshData.vertices.push_back(std::move(vertex));
	}

	for (uint32_t i = 0; i < mesh->mNumFaces; i++)
	{
		aiFace face = mesh->mFaces[i];
		for (uint32_t j = 0; j < face.mNumIndices; j++)
			meshData.indices.push_back(face.mIndices[j]);
	}


	// **
	// Process materials
	// **
	aiMaterial* material = scene->mMaterials[mesh->mMaterialIndex];

	meshData.albedoTextureFilename = GetTextureFilename(material, aiTextureType_BASE_COLOR);
	if (meshData.albedoTextureFilename.empty())
		meshData.albedoTextureFilename = GetTextureFilename(material, aiTextureType_DIFFUSE);

	meshData.specularTextureFilename = GetTextureFilename(material, aiTextureType_SPECULAR);
	meshData.emissiveTextureFilename = GetTextureFilename(material, aiTextureType_EMISSIVE);

	meshData.normalTextureFilename = GetTextureFilename(material, aiTextureType_NORMALS);
	if (meshData.normalTextureFilename.empty())
		meshData.normalTextureFilename = GetTextureFilename(material, aiTextureType_HEIGHT);

	meshData.metallicTextureFilename = GetTextureFilename(material, aiTextureType_METALNESS);
	meshData.roughnessTextureFilename = GetTextureFilename(material, aiTextureType_DIFFUSE_ROUGHNESS);
	meshData.aoTextureFilename = GetTextureFilename(material, aiTextureType_AMBIENT_OCCLUSION);
	if (meshData.aoTextureFilename.empty())
		meshData.aoTextureFilename = GetTextureFilename(material, aiTextureType_LIGHTMAP);

	currentNode->meshes.push_back(meshData);
}

std::string ModelLoader::GetTextureFilename(aiMaterial* mat, aiTextureType type)
{
	if (mat->GetTextureCount(type) > 0) {
		aiString filepath_;
		mat->GetTexture(type, 0, &filepath_);

		return std::string(filepath_.C_Str());
	}
	else {
		return "";
	}
}

} // namespace baamboo