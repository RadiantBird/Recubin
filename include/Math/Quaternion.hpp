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

    // 単位クォータニオンの共役 (= 逆回転)
    Quaternion conjugate() const { return Quaternion(w, -x, -y, -z); }

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

    static Quaternion Slerp(const Quaternion& a, const Quaternion& b, float t) {
        float dot = a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z;
        Quaternion targetB = b;
        if (dot < 0.0f) {
            dot = -dot;
            targetB = Quaternion(-b.w, -b.x, -b.y, -b.z);
        }
        if (dot > 0.9995f) {
            return Quaternion(a.w + t * (targetB.w - a.w), a.x + t * (targetB.x - a.x), a.y + t * (targetB.y - a.y), a.z + t * (targetB.z - a.z));
        }
        float theta_0 = std::acos(dot);
        float theta = theta_0 * t;
        float sin_theta = std::sin(theta);
        float sin_theta_0 = std::sin(theta_0);
        float s0 = std::cos(theta) - dot * sin_theta / sin_theta_0;
        float s1 = sin_theta / sin_theta_0;
        return Quaternion((s0 * a.w) + (s1 * targetB.w), (s0 * a.x) + (s1 * targetB.x), (s0 * a.y) + (s1 * targetB.y), (s0 * a.z) + (s1 * targetB.z));
    }

    // Quaternion → Euler 角 (度数, XYZ 内因回転)
    Vector3 toEuler() const {
        const float RAD2DEG = 180.0f / 3.14159265f;
        float sinP = 2.0f * (w * x + y * z);
        float cosP = 1.0f - 2.0f * (x * x + y * y);
        float pitch = std::atan2(sinP, cosP);
        float sinY = 2.0f * (w * y - z * x);
        float yaw   = (std::abs(sinY) >= 1.0f) ? std::copysign(3.14159265f * 0.5f, sinY) : std::asin(sinY);
        float sinR = 2.0f * (w * z + x * y);
        float cosR = 1.0f - 2.0f * (y * y + z * z);
        float roll  = std::atan2(sinR, cosR);
        return Vector3(pitch * RAD2DEG, yaw * RAD2DEG, roll * RAD2DEG);
    }

    // Euler 角 (度数, XYZ 内因回転) → Quaternion
    static Quaternion fromEuler(const Vector3& degrees) {
        const float DEG2RAD = 3.14159265f / 180.0f;
        float cx = std::cos(degrees.x * DEG2RAD * 0.5f), sx = std::sin(degrees.x * DEG2RAD * 0.5f);
        float cy = std::cos(degrees.y * DEG2RAD * 0.5f), sy = std::sin(degrees.y * DEG2RAD * 0.5f);
        float cz = std::cos(degrees.z * DEG2RAD * 0.5f), sz = std::sin(degrees.z * DEG2RAD * 0.5f);
        return Quaternion(
            cx*cy*cz + sx*sy*sz,
            sx*cy*cz - cx*sy*sz,
            cx*sy*cz + sx*cy*sz,
            cx*cy*sz - sx*sy*cz
        );
    }

    static Quaternion LookRotation(Vector3 forward, Vector3 up = Vector3(0, 1, 0)) {
        // ゼロベクトルによるエラーを回避
        if (forward.length() < 0.0001f) return Quaternion();

        Vector3 f = forward.normalize();
        
        // -Z を正面とするため、ローカルの +Z 方向（Back）を基準に基底を構築します
        Vector3 back = f * -1.0f;
        Vector3 right = Vector3::Cross(up, back);
        
        // forward と up が平行に近い場合の処理
        if (right.length() < 0.0001f) {
            // 代わりの軸で右方向を計算
            right = Vector3::Cross(Vector3(0, 0, 1), back);
            if (right.length() < 0.0001f) {
                right = Vector3::Cross(Vector3(1, 0, 0), back);
            }
        }
        right = right.normalize();
        Vector3 actualUp = Vector3::Cross(back, right);

        // FromRotationMatrix は列優先（Column-major）のインデックスを参照しているため、
        // それに合わせた 4x4 行列を作成します
        float m[16] = { 0 };
        m[0] = right.x;    m[4] = actualUp.x; m[8] = back.x;
        m[1] = right.y;    m[5] = actualUp.y; m[9] = back.y;
        m[2] = right.z;    m[6] = actualUp.z; m[10] = back.z;
        m[15] = 1.0f;

        return FromRotationMatrix(m);
    }
};