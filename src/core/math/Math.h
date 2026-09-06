#pragma once

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>

namespace engine::math {

using Vec2 = glm::vec2;
using Vec3 = glm::vec3;
using Vec4 = glm::vec4;
using Mat33 = glm::mat3;
using Mat44 = glm::mat4;
using Quat = glm::quat;

inline constexpr float kPi = glm::pi<float>();
inline constexpr float kEpsilon = 1.0e-6F;

[[nodiscard]] inline constexpr float radians(float degrees) { return glm::radians(degrees); }
[[nodiscard]] inline constexpr float degrees(float radiansValue) {
    return glm::degrees(radiansValue);
}

template <typename Vector>
[[nodiscard]] inline float length(const Vector& value) { return glm::length(value); }

template <typename Vector>
[[nodiscard]] inline float lengthSquared(const Vector& value) { return glm::dot(value, value); }

template <typename Vector>
[[nodiscard]] inline float dot(const Vector& left, const Vector& right) {
    return glm::dot(left, right);
}

[[nodiscard]] inline Vec3 cross(const Vec3& left, const Vec3& right) {
    return glm::cross(left, right);
}

template <typename Vector>
[[nodiscard]] inline Vector normalize(const Vector& value,
                                      const Vector& fallback = Vector{0.0F}) {
    const float squared = lengthSquared(value);
    return squared > kEpsilon * kEpsilon ? value / std::sqrt(squared) : fallback;
}

template <typename Value>
[[nodiscard]] inline Value lerp(const Value& from, const Value& to, float factor) {
    return glm::mix(from, to, factor);
}

template <typename Value>
[[nodiscard]] inline Value min(const Value& left, const Value& right) {
    return glm::min(left, right);
}

template <typename Value>
[[nodiscard]] inline Value max(const Value& left, const Value& right) {
    return glm::max(left, right);
}

template <typename Value>
[[nodiscard]] inline Value clamp(const Value& value, const Value& minimum,
                                 const Value& maximum) {
    return glm::clamp(value, minimum, maximum);
}

template <typename Vector>
[[nodiscard]] inline float distance(const Vector& left, const Vector& right) {
    return glm::distance(left, right);
}

template <typename Vector>
[[nodiscard]] inline float distanceSquared(const Vector& left, const Vector& right) {
    return lengthSquared(left - right);
}

[[nodiscard]] inline Vec3 reflect(const Vec3& direction, const Vec3& normal) {
    return glm::reflect(direction, normal);
}

[[nodiscard]] inline Vec3 project(const Vec3& value, const Vec3& onto) {
    const float denominator = dot(onto, onto);
    return denominator > kEpsilon ? onto * (dot(value, onto) / denominator) : Vec3{0.0F};
}

[[nodiscard]] inline Quat angleAxis(float radiansValue, const Vec3& axis) {
    return glm::angleAxis(radiansValue, normalize(axis, Vec3{0.0F, 1.0F, 0.0F}));
}

[[nodiscard]] inline Quat fromEuler(const Vec3& radiansValue) { return Quat{radiansValue}; }
[[nodiscard]] inline Vec3 toEuler(const Quat& rotation) { return glm::eulerAngles(rotation); }
[[nodiscard]] inline Quat normalize(const Quat& rotation) { return glm::normalize(rotation); }
[[nodiscard]] inline Quat conjugate(const Quat& rotation) { return glm::conjugate(rotation); }
[[nodiscard]] inline Quat inverse(const Quat& rotation) { return glm::inverse(rotation); }
[[nodiscard]] inline Quat slerp(const Quat& from, const Quat& to, float factor) {
    return glm::slerp(from, to, factor);
}
[[nodiscard]] inline Vec3 rotate(const Quat& rotation, const Vec3& vector) {
    return rotation * vector;
}

[[nodiscard]] inline Mat44 translation(const Vec3& value) {
    return glm::translate(Mat44{1.0F}, value);
}
[[nodiscard]] inline Mat44 rotation(const Quat& value) { return glm::mat4_cast(value); }
[[nodiscard]] inline Mat44 scaling(const Vec3& value) {
    return glm::scale(Mat44{1.0F}, value);
}
[[nodiscard]] inline Mat44 trs(const Vec3& position, const Quat& orientation,
                               const Vec3& scale) {
    return translation(position) * rotation(orientation) * scaling(scale);
}

[[nodiscard]] inline Mat33 normalMatrix(const Mat44& model) {
    return glm::inverseTranspose(Mat33{model});
}
[[nodiscard]] inline Mat44 transpose(const Mat44& matrix) { return glm::transpose(matrix); }
[[nodiscard]] inline Mat44 inverse(const Mat44& matrix) { return glm::inverse(matrix); }

[[nodiscard]] inline Vec3 transformPoint(const Mat44& matrix, const Vec3& point) {
    const Vec4 transformed = matrix * Vec4{point, 1.0F};
    return Vec3{transformed} / transformed.w;
}
[[nodiscard]] inline Vec3 transformVector(const Mat44& matrix, const Vec3& vector) {
    return Vec3{matrix * Vec4{vector, 0.0F}};
}

[[nodiscard]] inline Mat44 lookAt(const Vec3& eye, const Vec3& target,
                                  const Vec3& up = Vec3{0.0F, 1.0F, 0.0F}) {
    return glm::lookAtRH(eye, target, up);
}

[[nodiscard]] inline Mat44 perspective(float verticalFovRadians, float aspect,
                                       float nearPlane, float farPlane) {
    Mat44 projection = glm::perspectiveRH_ZO(verticalFovRadians, aspect, nearPlane, farPlane);
    projection[1][1] *= -1.0F;
    return projection;
}

[[nodiscard]] inline Mat44 orthographic(float left, float right, float bottom, float top,
                                        float nearPlane, float farPlane) {
    Mat44 projection = glm::orthoRH_ZO(left, right, bottom, top, nearPlane, farPlane);
    projection[1][1] *= -1.0F;
    return projection;
}

[[nodiscard]] inline bool nearlyEqual(float left, float right, float epsilon = kEpsilon) {
    return std::abs(left - right) <= epsilon;
}

} // namespace engine::math
