#pragma once
#include "Vector3.hpp"
#include "Quaternion.hpp"
#include "Matrix4.hpp"

struct CFrame {
    Vector3 position;
    Quaternion rotation;

    CFrame() : position(0, 0, 0), rotation() {}
    CFrame(const Vector3& pos, const Quaternion& rot) : position(pos), rotation(rot) {}
    CFrame(const Vector3& pos) : position(pos), rotation() {}

    // --- 変換関数セット ---

    // CFrameからVector3にする関数
    Vector3 toVector3() const {
        return position;
    }

    // CFrameからQuaternionにする関数
    Quaternion toQuaternion() const {
        return rotation;
    }

    Matrix4 toMatrix4() const {
        Matrix4 rotMat = Matrix4::FromQuaternion(rotation);
        Matrix4 transMat = Matrix4::Translate(position.x, position.y, position.z);
        return transMat * rotMat;
    }

    static CFrame FromMatrix4(const Matrix4& m) {
        // 行列から位置を抽出 (m12, m13, m14)
        Vector3 pos(m.m[12], m.m[13], m.m[14]);
        // 行列から回転を抽出
        Quaternion rot = Quaternion::FromRotationMatrix(m.m);
        return CFrame(pos, rot);
    }

    // --- ユーティリティ ---

    CFrame operator*(const CFrame& other) const {
        return CFrame(
            position + rotation.rotate(other.position),
            rotation * other.rotation
        );
    }

    Vector3 pointToWorld(const Vector3& localPoint) const {
        return position + rotation.rotate(localPoint);
    }
};