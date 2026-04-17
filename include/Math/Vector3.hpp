#pragma once
#include <cmath>
#include <string>

const float pi = 3.14159265f;

struct Vector3 {
    float x, y, z;

    Vector3 operator+(const Vector3& v) const { return {x + v.x, y + v.y, z + v.z}; }
    Vector3 operator-(const Vector3& v) const { return {x - v.x, y - v.y, z - v.z}; }
    Vector3 operator*(float s) const { return {x * s, y * s, z * s}; }

    // ベクトルの長さ（距離）を計算
    float length() const { return std::sqrt(x * x + y * y + z * z); }

    // 正規化（長さを1にする：方向だけが欲しいとき）
    Vector3 normalize() const {
        float l = length();
        return (l > 0) ? (*this * (1.0f / l)) : Vector3{0, 0, 0};
    }

    static Vector3 Cross(const Vector3& a, const Vector3& b) {
        return Vector3(
            a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x
        );
    }
    
    // Vector3クラス内に追加
    static float Dot(const Vector3& a, const Vector3& b) {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }

    Vector3() : x(0.0f), y(0.0f), z(0.0f) {}

    Vector3(float x, float y, float z) {
        this->x = x;
        this->y = y;
        this->z = z;
    }

    std::string toString() const {
        return std::to_string(x) + ", " + std::to_string(y) + ", " + std::to_string(z);
    }
};
