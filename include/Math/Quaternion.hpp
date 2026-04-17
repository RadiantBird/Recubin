#pragma once
#include <cmath>
#include "Vector3.hpp"

struct Quaternion {
    float w, x, y, z;

    Quaternion() : w(1.0f), x(0.0f), y(0.0f), z(0.0f) {}
    Quaternion(float _w, float _x, float _y, float _z) : w(_w), x(_x), y(_y), z(_z) {}

    static Quaternion fromAxisAngle(const Vector3& axis, float angleDegree) {
        float radHalf = (angleDegree * 3.14159265f / 180.0f) * 0.5f;
        float s = std::sin(radHalf);
        return Quaternion(std::cos(radHalf), axis.x * s, axis.y * s, axis.z * s);
    }

    static Quaternion FromRotationMatrix(const float m[16]) {
        float tr = m[0] + m[5] + m[10];
        float qw, qx, qy, qz;
        if (tr > 0.0f) {
            float s = std::sqrt(tr + 1.0f) * 2.0f;
            qw = 0.25f * s;
            qx = (m[6] - m[9]) / s;
            qy = (m[8] - m[2]) / s;
            qz = (m[1] - m[4]) / s;
        } else if ((m[0] > m[5]) && (m[0] > m[10])) {
            float s = std::sqrt(1.0f + m[0] - m[5] - m[10]) * 2.0f;
            qw = (m[6] - m[9]) / s;
            qx = 0.25f * s;
            qy = (m[1] + m[4]) / s;
            qz = (m[2] + m[8]) / s;
        } else if (m[5] > m[10]) {
            float s = std::sqrt(1.0f + m[5] - m[0] - m[10]) * 2.0f;
            qw = (m[8] - m[2]) / s;
            qx = (m[1] + m[4]) / s;
            qy = 0.25f * s;
            qz = (m[6] + m[9]) / s;
        } else {
            float s = std::sqrt(1.0f + m[10] - m[0] - m[5]) * 2.0f;
            qw = (m[1] - m[4]) / s;
            qx = (m[2] + m[8]) / s;
            qy = (m[6] + m[9]) / s;
            qz = 0.25f * s;
        }
        return Quaternion(qw, qx, qy, qz);
    }

    Quaternion operator*(const Quaternion& q) const {
        return {
            w * q.w - x * q.x - y * q.y - z * q.z,
            w * q.x + x * q.w + y * q.z - z * q.y,
            w * q.y - x * q.z + y * q.w + z * q.x,
            w * q.z + x * q.y - y * q.x + z * q.w
        };
    }

    Vector3 rotate(const Vector3& v) const {
        Vector3 qv = {x, y, z};
        Vector3 t = Vector3::Cross(qv, v) * 2.0f;
        return v + t * w + Vector3::Cross(qv, t);
    }

    Vector3 getRight() const {
        return Vector3(1.0f - 2.0f * (y * y + z * z), 2.0f * (x * y + z * w), 2.0f * (x * z - y * w)).normalize();
    }

    Vector3 getUp() const {
        return Vector3(2.0f * (x * y - z * w), 1.0f - 2.0f * (x * x + z * z), 2.0f * (y * z + x * w)).normalize();
    }

    Vector3 getForward() const {
        return Vector3(-2.0f * (x * z + y * w), -2.0f * (y * z - x * w), -(1.0f - 2.0f * (x * x + y * y))).normalize();
    }
};