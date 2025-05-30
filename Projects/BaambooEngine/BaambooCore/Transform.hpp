#pragma once
#define GLM_FORCE_LEFT_HANDED
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/euler_angles.hpp>

struct Transform
{
public:
    bool operator==(const Transform& other) const;
    bool operator!=(const Transform& other) const;

    [[nodiscard]]
    glm::quat Orientation() const { return m_Orientation; }

    // apply euler-based rotation to quaternion-based orientation
    void Update()
    {
        glm::vec3 radians = glm::radians(rotation);
        glm::mat4 mRotation = glm::eulerAngleYXZ(radians.y, radians.x, radians.z);
        m_Orientation = glm::quat_cast(mRotation);
        m_Orientation = glm::normalize(m_Orientation);
    }

    glm::mat4 Rotate(float dYaw, float dPitch, float dRoll)
    {
        UNUSED(dRoll);

        glm::quat qYaw = glm::angleAxis(dYaw, glm::vec3(0, 1, 0));
        glm::quat qPitch = glm::angleAxis(dPitch, glm::vec3(1, 0, 0));
        m_Orientation = qYaw * m_Orientation * qPitch;
        m_Orientation = glm::normalize(m_Orientation);

        float y, x, z;
        glm::mat4 mRotation = glm::mat4_cast(m_Orientation);
        glm::extractEulerAngleYXZ(mRotation, y, x, z);
        rotation = glm::degrees(glm::vec3(x, y, z));

        return mRotation;
    }

    void SetOrientation(const glm::mat4& mRotation)
    {
        m_Orientation = glm::quat_cast(mRotation);

        float y, x, z;
        glm::extractEulerAngleYXZ(mRotation, y, x, z);
        rotation = glm::degrees(glm::vec3(x, y, z));
    }

    [[nodiscard]]
    inline glm::mat4 Matrix() const
    {
        const glm::mat4 mS = glm::scale(glm::mat4(1.0f), scale);
        const glm::mat4 mR = glm::mat4_cast(m_Orientation);
        const glm::mat4 mT = glm::translate(glm::mat4(1.0f), position);

        return mT * mR * mS;
    }

    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 rotation = glm::vec3(0.0f);
    glm::vec3 scale = glm::vec3(1.0f);

private:
    glm::quat m_Orientation;
};

inline bool Transform::operator==(const Transform& other) const
{
    return position == other.position
        && rotation == other.rotation
        && scale == other.scale;
}

inline bool Transform::operator!=(const Transform& other) const
{
    return !(*this == other);
}