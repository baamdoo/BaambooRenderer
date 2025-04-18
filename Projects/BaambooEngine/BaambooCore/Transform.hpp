#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

struct Transform
{
public:
    bool operator==(const Transform& other) const;
    bool operator!=(const Transform& other) const;

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
    inline float Depth() const { return position.z; }

    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 rotation = glm::vec3(0.0f);
    glm::vec3 scale = glm::vec3(1.0f);

private:
    glm::quat orientation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
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