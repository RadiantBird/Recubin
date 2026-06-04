#pragma once
#include "Vector3.hpp"
#include "Quaternion.hpp"
#include "Matrix4.hpp"

struct CFrame {
    Vector3 Position;
    Quaternion Rotation;

    CFrame() : Position(0, 0, 0), Rotation() {}
    CFrame(const Vector3& pos, const Quaternion& rot) : Position(pos), Rotation(rot) {}
    CFrame(const Vector3& pos) : Position(pos), Rotation() {}
    CFrame(float x, float y, float z) : Position(x, y, z), Rotation() {}

    static CFrame fromAxisAngle(Vector3 axis, float angle) {
        return CFrame(Vector3(0,0,0), Quaternion::fromAxisAngle(axis, angle));
    }

    // --- 変換関数セット ---

    Matrix4 toMatrix4() const {
        Matrix4 rotMat = Matrix4::FromQuaternion(Rotation);
        Matrix4 transMat = Matrix4::Translate(Position.x, Position.y, Position.z);
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
            Position + Rotation.rotate(other.Position),
            Rotation * other.Rotation
        );
    }

    Vector3 pointToWorld(const Vector3& localPoint) const {
        return Position + Rotation.rotate(localPoint);
    }

    CFrame inverse() const {
        Quaternion invRot = Rotation.conjugate();
        return CFrame(invRot.rotate(-Position), invRot);
    }
};