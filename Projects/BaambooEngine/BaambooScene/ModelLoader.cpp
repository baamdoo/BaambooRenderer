#include "BaambooPch.h"
#include "ModelLoader.h"

#include <meshoptimizer.h>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/Importer.hpp>

namespace baamboo
{

mat4 ConvertMatrix(const aiMatrix4x4& aiMat)
{
	mat4 mResult;

	// Assimp uses row-major, GLM uses column-major
	mResult[0][0] = aiMat.a1; mResult[1][0] = aiMat.a2; mResult[2][0] = aiMat.a3; mResult[3][0] = aiMat.a4;
	mResult[0][1] = aiMat.b1; mResult[1][1] = aiMat.b2; mResult[2][1] = aiMat.b3; mResult[3][1] = aiMat.b4;
	mResult[0][2] = aiMat.c1; mResult[1][2] = aiMat.c2; mResult[2][2] = aiMat.c3; mResult[3][2] = aiMat.c4;
	mResult[0][3] = aiMat.d1; mResult[1][3] = aiMat.d2; mResult[2][3] = aiMat.d3; mResult[3][3] = aiMat.d4;
	return mResult;
}


ModelNode::~ModelNode()
{
    for (auto pChild : pChilds)
		RELEASE(pChild);
}

ModelLoader::ModelLoader(fs::path filepath, MeshDescriptor descriptor)
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
	importer.SetPropertyFloat(AI_CONFIG_GLOBAL_SCALE_FACTOR_KEY, descriptor.scale);

	const auto aiScene = importer.ReadFile(filepath.string(), importFlags);
	if (!aiScene || aiScene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !aiScene->mRootNode) 
	{
		assert(false && importer.GetErrorString());
		return;
	}

	if (descriptor.bLoadAnimations && aiScene->HasAnimations())
	{
		ProcessAnimations(aiScene);
	}

	m_pRootNode       = new ModelNode();
	m_pRootNode->aabb = BoundingBox(float3(std::numeric_limits<float>::max()), float3(std::numeric_limits<float>::min()));
	ProcessNode(aiScene->mRootNode, aiScene, m_pRootNode, descriptor);
}

ModelLoader::~ModelLoader()
{
    RELEASE(m_pRootNode);
}

void ModelLoader::ProcessNode(aiNode* node, const aiScene* scene, ModelNode* currentNode, MeshDescriptor descriptor)
{
	currentNode->name       = node->mName.C_Str();
	currentNode->mTransform = ConvertMatrix(node->mTransformation);

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
		currentNode->pChilds.push_back(new ModelNode());
		ProcessNode(node->mChildren[i], scene, currentNode->pChilds.back(), descriptor);
	}
}

void ModelLoader::ProcessMesh(aiMesh* mesh, const aiScene* scene, ModelNode* currentNode, MeshDescriptor descriptor)
{
	assert(mesh->HasPositions());
	assert(mesh->HasNormals());

	MeshData meshData = {};
	meshData.name     = mesh->mName.C_Str();
	meshData.aabb     = BoundingBox(*(float3*)(&mesh->mAABB.mMin), *(float3*)(&mesh->mAABB.mMax));
	currentNode->aabb = BoundingBox::Union(currentNode->aabb, meshData.aabb);

    // **
    // Process geometries
    // **
    bool bHasBones = mesh->HasBones() && descriptor.bLoadAnimations;
    if (bHasBones)
    {
        meshData.bHasSkinnedData = true;
        meshData.vertexFormat    = eVertexFormat::P3U2N3T3S;

        // Process bone weights first
        meshData.boneIndices.resize(mesh->mNumVertices, 0);
        meshData.boneWeights.resize(mesh->mNumVertices, float4(0.0f));
        ProcessBoneWeights(mesh, meshData);

        meshData.skinnedVertices.reserve(mesh->mNumVertices);
        for (u32 i = 0; i < mesh->mNumVertices; ++i)
        {
            VertexP3U2N3T3S vertex{};

            // Position
            vertex.position.x = mesh->mVertices[i].x;
            vertex.position.y = mesh->mVertices[i].y;
            vertex.position.z = mesh->mVertices[i].z;

            // UV
            if (mesh->HasTextureCoords(0))
            {
                vertex.uv.x = mesh->mTextureCoords[0][i].x;
                vertex.uv.y = mesh->mTextureCoords[0][i].y;
            }

            // Normal
            if (mesh->HasNormals())
            {
                vertex.normal.x = mesh->mNormals[i].x;
                vertex.normal.y = mesh->mNormals[i].y;
                vertex.normal.z = mesh->mNormals[i].z;
            }

            // Tangent
            if (mesh->HasTangentsAndBitangents())
            {
                vertex.tangent.x = mesh->mTangents[i].x;
                vertex.tangent.y = mesh->mTangents[i].y;
                vertex.tangent.z = mesh->mTangents[i].z;
            }

            // Skinning
            vertex.boneIndices = meshData.boneIndices[i];
            vertex.boneWeights = meshData.boneWeights[i];

            meshData.skinnedVertices.push_back(vertex);
        }
    }
    else
    {
        meshData.vertices.reserve(mesh->mNumVertices);
        for (u32 i = 0; i < mesh->mNumVertices; i++)
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

            meshData.vertices.push_back(vertex);
        }
    }

    // indices
	meshData.indices.reserve(mesh->mNumFaces * 3);
	for (u32 i = 0; i < mesh->mNumFaces; i++)
	{
		aiFace face = mesh->mFaces[i];
		for (u32 j = 0; j < face.mNumIndices; j++)
			meshData.indices.push_back(face.mIndices[j]);
	}

	// **
	// Process materials
	// **
    if (mesh->mMaterialIndex >= 0 && mesh->mMaterialIndex < scene->mNumMaterials)
    {
        meshData.materialIndex = mesh->mMaterialIndex;

        // process material only if not already processed
        if (meshData.materialIndex >= m_Materials.size())
        {
            aiMaterial* aiMat = scene->mMaterials[mesh->mMaterialIndex];

            MaterialData material = {};
            material.name = aiMat->GetName().C_Str();

            aiColor3D color;
            if (aiMat->Get(AI_MATKEY_COLOR_AMBIENT, color) == AI_SUCCESS)
                material.ambient = float3(color.r, color.g, color.b);
            if (aiMat->Get(AI_MATKEY_COLOR_DIFFUSE, color) == AI_SUCCESS)
                material.diffuse = float3(color.r, color.g, color.b);
            if (aiMat->Get(AI_MATKEY_COLOR_SPECULAR, color) == AI_SUCCESS)
                material.specular = float3(color.r, color.g, color.b);

            float value;
            if (aiMat->Get(AI_MATKEY_SHININESS, value) == AI_SUCCESS)
                material.shininess = value;

            if (aiMat->Get(AI_MATKEY_METALLIC_FACTOR, value) == AI_SUCCESS)
                material.metallic = value;
            if (aiMat->Get(AI_MATKEY_ROUGHNESS_FACTOR, value) == AI_SUCCESS)
                material.roughness = value;

            material.albedoPath     = GetTextureFilename(aiMat, aiTextureType_DIFFUSE);
            if (material.albedoPath.empty())
                material.albedoPath = GetTextureFilename(aiMat, aiTextureType_DIFFUSE);

            material.normalPath     = GetTextureFilename(aiMat, aiTextureType_NORMALS);
            if (material.normalPath.empty())
                material.normalPath = GetTextureFilename(aiMat, aiTextureType_HEIGHT);

            material.metallicPath   = GetTextureFilename(aiMat, aiTextureType_METALNESS);
            material.roughnessPath  = GetTextureFilename(aiMat, aiTextureType_DIFFUSE_ROUGHNESS);
            material.aoPath         = GetTextureFilename(aiMat, aiTextureType_AMBIENT_OCCLUSION);
            if (material.aoPath.empty())
                material.aoPath     = GetTextureFilename(aiMat, aiTextureType_LIGHTMAP);

            material.emissivePath   = GetTextureFilename(aiMat, aiTextureType_EMISSIVE);

            m_Materials.push_back(material);
        }
    }

    if (descriptor.bGenerateMeshlets)
    {
        GenerateMeshlets(meshData, descriptor.bOptimize);
    }

	currentNode->meshIndices.push_back(static_cast<u32>(m_Meshes.size()));
	m_Meshes.push_back(std::move(meshData));
}

void ModelLoader::ProcessAnimations(const aiScene* scene)
{
	if (!scene->HasAnimations())
		return;

	// First, build the bone hierarchy
	ProcessBoneHierarchy(scene->mRootNode, scene);

	// Store global inverse transform
	m_AnimationData.skeleton.mWorldInv = glm::inverse(ConvertMatrix(scene->mRootNode->mTransformation));

	// Process each animation clip
	for (u32 i = 0; i < scene->mNumAnimations; ++i)
	{
		AnimationClip clip = ProcessAnimationClip(scene->mAnimations[i]);
		m_AnimationData.clips.push_back(std::move(clip));
	}

	m_AnimationData.bHasAnimation = true;
}

void ModelLoader::ProcessBoneHierarchy(aiNode* node, const aiScene* scene, i32 parentIndex)
{
    std::string nodeName = node->mName.C_Str();

    // Check if this node is a bone by looking through all meshes
    bool bIsBone = false;
    for (u32 i = 0; i < scene->mNumMeshes; ++i)
    {
        aiMesh* mesh = scene->mMeshes[i];
        for (u32 j = 0; j < mesh->mNumBones; ++j)
        {
            if (nodeName == mesh->mBones[j]->mName.C_Str())
            {
                bIsBone = true;
                break;
            }
        }

        if (bIsBone) 
            break;
    }

    // If this node is a bone or has bone children, add it to skeleton
    u32 currentBoneIndex = INVALID_INDEX;
    if (bIsBone || parentIndex != -1)
    {
        Bone bone;
        bone.name = nodeName;
        bone.parentIndex = (parentIndex >= 0) ? static_cast<u32>(parentIndex) : INVALID_INDEX;
        bone.mBoneToModel = ConvertMatrix(node->mTransformation);

        // Find inverse bind pose from mesh bones
        for (u32 i = 0; i < scene->mNumMeshes; ++i)
        {
            aiMesh* mesh = scene->mMeshes[i];
            for (u32 j = 0; j < mesh->mNumBones; ++j)
            {
                if (nodeName == mesh->mBones[j]->mName.C_Str())
                {
                    bone.mModelToBone = ConvertMatrix(mesh->mBones[j]->mOffsetMatrix);
                    break;
                }
            }
        }

        currentBoneIndex = static_cast<u32>(m_AnimationData.skeleton.bones.size());
        m_AnimationData.skeleton.bones.push_back(bone);
        m_AnimationData.skeleton.boneNameToIndex[nodeName] = currentBoneIndex;
        m_BoneMap[nodeName] = currentBoneIndex;
        m_BoneCount++;
    }

    // Process children
    for (u32 i = 0; i < node->mNumChildren; ++i)
    {
        ProcessBoneHierarchy(node->mChildren[i], scene,
            currentBoneIndex != INVALID_INDEX ? static_cast<i32>(currentBoneIndex) : parentIndex);
    }
}

void ModelLoader::ProcessBoneWeights(aiMesh* mesh, MeshData& meshData)
{
    meshData.boneIndices.resize(mesh->mNumVertices, 0);
    meshData.boneWeights.resize(mesh->mNumVertices, float4(0.0f));
    meshData.bHasSkinnedData = true;

    // Process each bone
    for (u32 boneIdx = 0; boneIdx < mesh->mNumBones; ++boneIdx)
    {
        aiBone* bone = mesh->mBones[boneIdx];
        std::string boneName = bone->mName.C_Str();

        // Get bone index in skeleton
        u32 boneIndex = INVALID_INDEX;
        auto it = m_BoneMap.find(boneName);
        if (it != m_BoneMap.end())
        {
            boneIndex = it->second;
        }
        else
        {
            // This bone wasn't in hierarchy, add it
            Bone newBone;
            newBone.name = boneName;
            newBone.parentIndex = INVALID_INDEX; // Will be fixed later
            newBone.mModelToBone = ConvertMatrix(bone->mOffsetMatrix);

            boneIndex = static_cast<u32>(m_AnimationData.skeleton.bones.size());
            m_AnimationData.skeleton.bones.push_back(newBone);
            m_AnimationData.skeleton.boneNameToIndex[boneName] = boneIndex;
            m_BoneMap[boneName] = boneIndex;
            m_BoneCount++;
        }

        // Assign weights to vertices
        for (u32 weightIdx = 0; weightIdx < bone->mNumWeights; ++weightIdx)
        {
            u32 vertexId = bone->mWeights[weightIdx].mVertexId;
            float weight = bone->mWeights[weightIdx].mWeight;

            // Find empty slot for this weight (max 4 bones per vertex)
            for (u32 i = 0; i < MAX_BONES_PER_VERTEX; ++i)
            {
                if (meshData.boneWeights[vertexId][i] == 0.0f)
                {
                    // Pack bone index into appropriate byte
                    u32 shift = i * 8;
                    meshData.boneIndices[vertexId] |= (boneIndex & 0xFF) << shift;
                    meshData.boneWeights[vertexId][i] = weight;
                    break;
                }
            }
        }
    }

    // Normalize weights
    for (u32 i = 0; i < mesh->mNumVertices; ++i)
    {
        float4& weights = meshData.boneWeights[i];
        float sum = weights.x + weights.y + weights.z + weights.w;
        if (sum > 0.0f)
        {
            weights /= sum;
        }
        else
        {
            // If no weights, bind to root bone
            weights.x = 1.0f;
        }
    }
}

AnimationClip ModelLoader::ProcessAnimationClip(aiAnimation* animation)
{
    AnimationClip clip  = {};
    clip.name           = animation->mName.C_Str();
    clip.duration       = static_cast<float>(animation->mDuration);
    clip.ticksPerSecond = static_cast<float>(animation->mTicksPerSecond != 0.0 ? animation->mTicksPerSecond : 25.0);

    // Process each channel (bone animation)
    for (u32 i = 0; i < animation->mNumChannels; ++i)
    {
        aiNodeAnim* nodeAnim = animation->mChannels[i];
        AnimationChannel channel;
        channel.boneName = nodeAnim->mNodeName.C_Str();

        // Get bone index
        auto it = m_BoneMap.find(channel.boneName);
        if (it != m_BoneMap.end())
        {
            channel.boneIndex = it->second;
        }
        else
        {
            channel.boneIndex = INVALID_INDEX;
        }

        // Position keys
        channel.positionKeys.reserve(nodeAnim->mNumPositionKeys);
        for (u32 j = 0; j < nodeAnim->mNumPositionKeys; ++j)
        {
            KeyPosition key;
            key.timestamp = static_cast<float>(nodeAnim->mPositionKeys[j].mTime);
            key.position = float3(
                nodeAnim->mPositionKeys[j].mValue.x,
                nodeAnim->mPositionKeys[j].mValue.y,
                nodeAnim->mPositionKeys[j].mValue.z
            );
            channel.positionKeys.push_back(key);
        }

        // Rotation keys
        channel.rotationKeys.reserve(nodeAnim->mNumRotationKeys);
        for (u32 j = 0; j < nodeAnim->mNumRotationKeys; ++j)
        {
            KeyRotation key;
            key.timestamp = static_cast<float>(nodeAnim->mRotationKeys[j].mTime);
            aiQuaternion q = nodeAnim->mRotationKeys[j].mValue;
            key.qRotation = quat(q.w, q.x, q.y, q.z);
            channel.rotationKeys.push_back(key);
        }

        // Scale keys
        channel.scaleKeys.reserve(nodeAnim->mNumScalingKeys);
        for (u32 j = 0; j < nodeAnim->mNumScalingKeys; ++j)
        {
            KeyScale key;
            key.timestamp = static_cast<float>(nodeAnim->mScalingKeys[j].mTime);
            key.scale = float3(
                nodeAnim->mScalingKeys[j].mValue.x,
                nodeAnim->mScalingKeys[j].mValue.y,
                nodeAnim->mScalingKeys[j].mValue.z
            );
            channel.scaleKeys.push_back(key);
        }

        clip.channels.push_back(std::move(channel));
    }

    return clip;
}

// Reference: https://github.com/zeux/meshoptimizer
void ModelLoader::GenerateMeshlets(MeshData& meshData, bool bOptimizeVertexCache)
{
    const size_t maxVertices  = 64;
    const size_t maxTriangles = 126;
    const float  coneWeight   = 0.0f;

    size_t vertexCount = meshData.GetVertexCount();
    size_t indexCount  = meshData.indices.size();

    if (bOptimizeVertexCache)
		meshopt_optimizeVertexCache(meshData.indices.data(), meshData.indices.data(), indexCount, vertexCount);

    size_t maxMeshlets = meshopt_buildMeshletsBound(meshData.indices.size(), maxVertices, maxTriangles);
    std::vector< meshopt_Meshlet > meshlets(maxMeshlets);

    meshData.meshletVertices.resize(maxMeshlets * maxVertices);
    meshData.meshletTriangles.resize(maxMeshlets * maxTriangles * 3);

    size_t numMeshlets = meshopt_buildMeshlets(
        meshlets.data(),
        meshData.meshletVertices.data(),
        meshData.meshletTriangles.data(),
        meshData.indices.data(),
        indexCount,
        static_cast<const float*>(meshData.GetVertexData()),
        vertexCount,
        meshData.VertexSize(),
        maxVertices,
        maxTriangles,
        coneWeight
    );

    const meshopt_Meshlet& last = meshlets[numMeshlets - 1];

    meshData.meshletVertices.resize(last.vertex_offset + last.vertex_count);
    meshData.meshletVertices.resize(last.triangle_offset + last.triangle_count * 3);

    meshData.meshlets.reserve(numMeshlets);
    for (size_t i = 0; i < numMeshlets; ++i)
    {
        const meshopt_Meshlet& m = meshlets[i];

        meshopt_Bounds bounds = meshopt_computeMeshletBounds(
            &meshData.meshletVertices[m.vertex_offset],
            &meshData.meshletTriangles[m.triangle_offset],
            m.triangle_count,
            static_cast<const float*>(meshData.GetVertexData()),
            vertexCount,
            meshData.VertexSize()
        );

        Meshlet newMeshlet = {};
        newMeshlet.vertexOffset   = m.vertex_offset;
        newMeshlet.triangleOffset = m.triangle_offset;
        newMeshlet.vertexCount    = m.vertex_count;
        newMeshlet.triangleCount  = m.triangle_count;

        newMeshlet.center     = float3(bounds.center[0], bounds.center[1], bounds.center[2]);
        newMeshlet.radius     = bounds.radius;
        newMeshlet.coneAxis   = float3(bounds.cone_axis[0], bounds.cone_axis[1], bounds.cone_axis[2]);
        newMeshlet.coneCutoff = bounds.cone_cutoff;

        meshData.meshlets.push_back(newMeshlet);
    }
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
