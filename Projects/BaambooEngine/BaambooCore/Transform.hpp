#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

struct Transform
{
public:
    bool operator==(const Transform& other) const;
    bool operator!=(const Transform& other) const;

    void Rotate(float dx, float dy, float dz);
    void Rotate(glm::vec3 axis, float dt);
    void Translate(float dx, float dy, float dz);

    void SetPosition(float px, float py, float pz);
    void SetOrientation(float tx, float ty, float tz);
    void SetScale(float sx, float sy, float sz);

    void SetDirection(float vx, float vy, float vz);
    void SetTransform(const glm::mat4& mTransform);

    [[nodiscard]]
    inline glm::mat4 Matrix() const
    {
        const glm::mat4 mS = glm::scale(glm::mat4(1.0f), scale);
        const glm::mat4 mR = glm::mat4_cast(orientation);
        const glm::mat4 mT = glm::translate(glm::mat4(1.0f), position);

        return mS * mR * mT;
    }

    [[nodiscard]]
    inline glm::vec4 Right() const { return orientation * glm::vec4(1.0f, 0.0f, 0.0f, 0.0f); }
    [[nodiscard]]
    inline glm::vec4 Up() const { return orientation * glm::vec4(0.0f, 1.0f, 0.0f, 0.0f); }
    [[nodiscard]]
    inline glm::vec4 Forward() const { return orientation * glm::vec4(0.0f, 0.0f, 1.0f, 0.0f); };

    [[nodiscard]]
    inline f32 Depth() const { return position.x; }

private:
    glm::vec3 position = glm::vec3(0.0f);
    glm::quat orientation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    glm::vec3 scale = glm::vec3(1.0f);
};

inline bool Transform::operator==(const Transform& other) const
{
    return position == other.position
        && orientation == other.orientation
        && scale == other.scale;
}

inline bool Transform::operator!=(const Transform& other) const
{
    return !(*this == other);
}

inline void Transform::Rotate(float dx, float dy, float dz)
{
    glm::quat qPitch = glm::angleAxis(glm::radians(dx), glm::vec3(1.0f, 0.0f, 0.0f));
    glm::quat qYaw = glm::angleAxis(glm::radians(dy), glm::vec3(0.0f, 1.0f, 0.0f));
    glm::quat qRoll = glm::angleAxis(glm::radians(dz), glm::vec3(0.0f, 0.0f, 1.0f));

    orientation *= qPitch * qYaw * qRoll;
}

inline void Transform::Rotate(glm::vec3 axis, float dt)
{
    axis = glm::normalize(axis);
    glm::quat qRotation = glm::angleAxis(dt, axis);

    glm::mat4 mRotation = glm::mat4_cast(qRotation);
    position = glm::vec3(mRotation * glm::vec4(position, 1.0f));
}

inline void Transform::Translate(float dx, float dy, float dz)
{
    glm::vec3 look = glm::vec3(dx, dy, dz);
    glm::mat4 mRotation = glm::mat4_cast(orientation);
    look = glm::vec3(mRotation * glm::vec4(look, 0.0f));
    position += look;
}

inline void Transform::SetPosition(float px, float py, float pz)
{
    position.x = px;
    position.y = py;
    position.z = pz;
}

inline void Transform::SetOrientation(float tx, float ty, float tz)
{
    // Note: GLM does not directly support yaw-pitch-roll creation.
    // You might need to convert from Euler angles to a quaternion.
    glm::quat qOrientation = glm::quat(glm::vec3(glm::radians(tx), glm::radians(ty), glm::radians(tz)));
    orientation = qOrientation;
}

inline void Transform::SetScale(float sx, float sy, float sz)
{
    scale.x = sx;
    scale.y = sy;
    scale.z = sz;
}

inline void Transform::SetDirection(float vx, float vy, float vz)
{
    glm::vec3 dir(vx, vy, vz);
    dir = glm::normalize(dir);

    glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 right = glm::cross(dir, up);
    right = glm::normalize(right);
    up = glm::cross(dir, right);

    glm::mat4 view = glm::lookAt(position, position + dir, up);
    glm::mat4 viewInv = glm::inverse(view);
    SetTransform(viewInv);
}

inline void Transform::SetTransform(const glm::mat4& mTransform)
{
    position = glm::vec3(mTransform[3]);
    orientation = glm::quat_cast(mTransform);
    scale = glm::vec3(glm::length(glm::vec3(mTransform[0])), glm::length(glm::vec3(mTransform[1])), glm::length(glm::vec3(mTransform[2])));
}