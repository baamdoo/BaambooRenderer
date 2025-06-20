#include "BaambooPch.h"
#include "AnimationTypes.h"

//-------------------------------------------------------------------------
// AnimationChannel
//-------------------------------------------------------------------------
float3 AnimationChannel::InterpolatePosition(float time) const
{
    if (positionKeys.empty()) 
        return float3(0.0f);

    if (positionKeys.size() == 1) 
        return positionKeys[0].position;

    // Find the two keys to interpolate between
    size_t index = 0;
    for (size_t i = 0; i < positionKeys.size() - 1; ++i)
    {
        if (time < positionKeys[i + 1].timestamp)
        {
            index = i;
            break;
        }
    }

    if (index >= positionKeys.size() - 1)
        return positionKeys.back().position;

    const auto& key1 = positionKeys[index];
    const auto& key2 = positionKeys[index + 1];

    float t = (time - key1.timestamp) / (key2.timestamp - key1.timestamp);
    t = glm::clamp(t, 0.0f, 1.0f);

    return glm::mix(key1.position, key2.position, t);
}

quat AnimationChannel::InterpolateRotation(float time) const
{
    if (rotationKeys.empty()) 
        return quat(1.0f, 0.0f, 0.0f, 0.0f);

    if (rotationKeys.size() == 1) 
        return rotationKeys[0].qRotation;

    u64 index = 0;
    for (u64 i = 0; i < rotationKeys.size() - 1; ++i)
    {
        if (time < rotationKeys[i + 1].timestamp)
        {
            index = i;
            break;
        }
    }

    if (index >= rotationKeys.size() - 1)
        return rotationKeys.back().qRotation;

    const auto& key1 = rotationKeys[index];
    const auto& key2 = rotationKeys[index + 1];

    float t = (time - key1.timestamp) / (key2.timestamp - key1.timestamp);
    t = glm::clamp(t, 0.0f, 1.0f);

    return glm::slerp(key1.qRotation, key2.qRotation, t);
}

float3 AnimationChannel::InterpolateScale(float time) const
{
    if (scaleKeys.empty()) 
        return float3(1.0f);

    if (scaleKeys.size() == 1) 
        return scaleKeys[0].scale;

    u64 index = 0;
    for (u64 i = 0; i < scaleKeys.size() - 1; ++i)
    {
        if (time < scaleKeys[i + 1].timestamp)
        {
            index = i;
            break;
        }
    }

    if (index >= scaleKeys.size() - 1)
        return scaleKeys.back().scale;

    const auto& key1 = scaleKeys[index];
    const auto& key2 = scaleKeys[index + 1];

    float t = (time - key1.timestamp) / (key2.timestamp - key1.timestamp);
    t = glm::clamp(t, 0.0f, 1.0f);

    return glm::mix(key1.scale, key2.scale, t);
}


//-------------------------------------------------------------------------
// Skeleton
//-------------------------------------------------------------------------
u32 Skeleton::GetBoneIndex(const std::string& name) const
{
    auto it = boneNameToIndex.find(name);
    return (it != boneNameToIndex.end()) ? it->second : INVALID_INDEX;
}

bool Skeleton::HasBone(const std::string& name) const
{
    return boneNameToIndex.contains(name);
}


//-------------------------------------------------------------------------
// BoneTransform
//-------------------------------------------------------------------------
mat4 BoneTransform::ToMatrix() const
{
    mat4 T = glm::translate(mat4(1.0f), position);
    mat4 R = glm::toMat4(qRotation);
    mat4 S = glm::scale(mat4(1.0f), scale);
    return T * R * S;
}

//-------------------------------------------------------------------------
// AnimationPose
//-------------------------------------------------------------------------
void AnimationPose::CalculateBoneMatrices(const Skeleton& skeleton)
{
    u64 numBones = skeleton.bones.size();
    mBones.resize(numBones);

    std::vector< mat4 > mWorlds(numBones);
    for (size_t i = 0; i < numBones; ++i)
    {
        const Bone& bone = skeleton.bones[i];
        mat4 mModel = boneTransforms[i].ToMatrix();

        if (bone.parentIndex == INVALID_INDEX)
        {
            // Root bone
            mWorlds[i] = mModel;
        }
        else
        {
            // Child bone - multiply with parent transform
            mWorlds[i] = mWorlds[bone.parentIndex] * mModel;
        }

        // Final bone matrix = globalInverse * worldTransform * inverseBindPose
        mBones[i] = skeleton.mWorldInv * mWorlds[i] * bone.mModelToBone;
    }
}
