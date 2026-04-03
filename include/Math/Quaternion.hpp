#pragma once
#include <cmath>
#include "include/Math/Vector3.hpp"

struct Quaternion {
    float w, x, y, z;

    // デフォルトコンストラクタ（回転なし：単位クォータニオン）
    Quaternion() : w(1.0f), x(0.0f), y(0.0f), z(0.0f) {}

    // 【これが必要！】4つの値を直接セットするコンストラクタ
    Quaternion(float _w, float _x, float _y, float _z) 
        : w(_w), x(_x), y(_y), z(_z) {}

    static Quaternion fromAxisAngle(const Vector3& axis, float angleDegree) {
        // 度数法をラジアンの半分に変換 (q = [cos(theta/2), v*sin(theta/2)])
        float radHalf = (angleDegree * 3.14159265f / 180.0f) * 0.5f;
        float s = std::sin(radHalf);
        return Quaternion(std::cos(radHalf), axis.x * s, axis.y * s, axis.z * s);
    }

    Quaternion operator*(const Quaternion& q) const {
        // ここで Quaternion(w, x, y, z) が呼ばれるようになる
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
        return Vector3(
            1.0f - 2.0f * (y * y + z * z),
            2.0f * (x * y + z * w),
            2.0f * (x * z - y * w)
        ).normalize();
    }

    Vector3 getUp() const {
        return Vector3(
            2.0f * (x * y - z * w),
            1.0f - 2.0f * (x * x + z * z),
            2.0f * (y * z + x * w)
        ).normalize();
    }

    Vector3 getForward() const {
        // 慣習として前を(0,0,-1)とする場合
        return Vector3(
            -2.0f * (x * z + y * w),
            -2.0f * (y * z - x * w),
            -(1.0f - 2.0f * (x * x + y * y))
        ).normalize();
    }
};