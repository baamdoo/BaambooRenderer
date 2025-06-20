#pragma once
#include "MathTypes.h"

//-------------------------------------------------------------------------
// Animation Data Structures
//-------------------------------------------------------------------------
struct Bone
{
    std::string name;

    u32  parentIndex  = INVALID_INDEX;
    mat4 mBoneToModel = mat4(1.0f);
    mat4 mModelToBone = mat4(1.0f);
};

struct KeyPosition
{
    float3 position;
    float  timestamp;
};

struct KeyRotation
{
    quat  qRotation;
    float timestamp;
};

struct KeyScale
{
    float3 scale;
    float  timestamp;
};

struct AnimationChannel
{
    std::string boneName;

    u32 boneIndex = INVALID_INDEX;

    std::vector< KeyPosition > positionKeys;
    std::vector< KeyRotation > rotationKeys;
    std::vector< KeyScale >    scaleKeys;

    float3 InterpolatePosition(float time) const;
    quat InterpolateRotation(float time) const;
    float3 InterpolateScale(float time) const;
};

struct AnimationClip
{
    std::string name;

    float duration = 0.0f;
    float ticksPerSecond = 25.0f;

    std::vector< AnimationChannel > channels;

    inline float GetDurationInSeconds() const { return duration / ticksPerSecond; }
};

struct Skeleton
{
    std::vector< Bone >                    bones;
    std::unordered_map< std::string, u32 > boneNameToIndex;

    mat4 mWorldInv = mat4(1.0f);

    u32 GetBoneIndex(const std::string& name) const;
    bool HasBone(const std::string& name) const;
};

struct BoneTransform
{
    float3 position  = float3(0.0f);
    quat   qRotation = quat(1.0f, 0.0f, 0.0f, 0.0f);
    float3 scale     = float3(1.0f);

    mat4 ToMatrix() const;
};

struct AnimationPose
{
    std::vector< BoneTransform > boneTransforms;
    std::vector< mat4 >          mBones;

    void CalculateBoneMatrices(const Skeleton& skeleton);
};

// Animation import data
struct AnimationData
{
    Skeleton skeleton;

    std::vector< AnimationClip > clips;

    bool bHasAnimation = false;
};
