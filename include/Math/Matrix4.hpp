#pragma once
#include "Vector3.hpp"
#include "Quaternion.hpp"

struct Matrix4 {
    float m[16];

    Matrix4() {
        for (int i = 0; i < 16; i++) m[i] = 0.0f;
        m[0] = m[5] = m[10] = m[15] = 1.0f;
    }

    Matrix4 operator*(const Matrix4& b) const {
        Matrix4 res;
        for (int col = 0; col < 4; col++) {
            for (int row = 0; row < 4; row++) {
                res.m[col * 4 + row] =
                    m[0 * 4 + row] * b.m[col * 4 + 0] +
                    m[1 * 4 + row] * b.m[col * 4 + 1] +
                    m[2 * 4 + row] * b.m[col * 4 + 2] +
                    m[3 * 4 + row] * b.m[col * 4 + 3];
            }
        }
        return res;
    }

    static Matrix4 Translate(float x, float y, float z) {
        Matrix4 res;
        res.m[12] = x; res.m[13] = y; res.m[14] = z;
        return res;
    }

    static Matrix4 Scale(float x, float y, float z) {
        Matrix4 res;
        res.m[0] = x; res.m[5] = y; res.m[10] = z;
        return res;
    }

    static Matrix4 RotateX(float degree) {
        Matrix4 res;
        float r = degree * 3.14159265f / 180.0f;
        res.m[5] = cos(r);  res.m[6] = sin(r);
        res.m[9] = -sin(r); res.m[10] = cos(r);
        return res;
    }

    static Matrix4 RotateY(float degree) {
        Matrix4 res;
        float r = degree * 3.14159265f / 180.0f;
        res.m[0] = cos(r);  res.m[2] = -sin(r);
        res.m[8] = sin(r);  res.m[10] = cos(r);
        return res;
    }

    static Matrix4 RotateZ(float degree) {
        Matrix4 res;
        float r = degree * 3.14159265f / 180.0f;
        res.m[0] = cos(r);  res.m[1] = sin(r);
        res.m[4] = -sin(r); res.m[5] = cos(r);
        return res;
    }

    static Matrix4 Perspective(float fovDeg, float aspect, float zNear, float zFar) {
        Matrix4 res;
        for(int i=0; i<16; i++) res.m[i] = 0.0f;
        float f = 1.0f / tan(fovDeg * 3.14159265f / 360.0f);
        res.m[0] = f / aspect;
        res.m[5] = f;
        res.m[10] = (zFar + zNear) / (zNear - zFar);
        res.m[11] = -1.0f;
        res.m[14] = (2.0f * zFar * zNear) / (zNear - zFar);
        return res;
    }

    static Matrix4 LookAt(Vector3 eye, Vector3 target, Vector3 up) {
        Vector3 f = (target - eye).normalize();
        Vector3 r = Vector3::Cross(f, up).normalize();
        Vector3 u = Vector3::Cross(r, f);
        Matrix4 res;
        res.m[0] = r.x;  res.m[4] = r.y;  res.m[8] = r.z;
        res.m[1] = u.x;  res.m[5] = u.y;  res.m[9] = u.z;
        res.m[2] = -f.x; res.m[6] = -f.y; res.m[10] = -f.z;
        res.m[12] = -Vector3::Dot(r, eye);
        res.m[13] = -Vector3::Dot(u, eye);
        res.m[14] = Vector3::Dot(f, eye);
        return res;
    }

    static Matrix4 Ortho(float l, float r, float b, float t, float n, float f) {
        Matrix4 res;
        for (int i = 0; i < 16; i++) res.m[i] = 0.0f;
        res.m[0]  = 2.0f / (r - l);
        res.m[5]  = 2.0f / (t - b);
        res.m[10] = -2.0f / (f - n);
        res.m[12] = -(r + l) / (r - l);
        res.m[13] = -(t + b) / (t - b);
        res.m[14] = -(f + n) / (f - n);
        res.m[15] = 1.0f;
        return res;
    }

    static Matrix4 FromQuaternion(const Quaternion& q) {
        Matrix4 res;
        float xx = q.x * q.x; float yy = q.y * q.y; float zz = q.z * q.z;
        float xy = q.x * q.y; float xz = q.x * q.z; float yz = q.y * q.z;
        float wx = q.w * q.x; float wy = q.w * q.y; float wz = q.w * q.z;
        res.m[0] = 1.0f - 2.0f * (yy + zz);
        res.m[1] = 2.0f * (xy + wz);
        res.m[2] = 2.0f * (xz - wy);
        res.m[4] = 2.0f * (xy - wz);
        res.m[5] = 1.0f - 2.0f * (xx + zz);
        res.m[6] = 2.0f * (yz + wx);
        res.m[8] = 2.0f * (xz + wy);
        res.m[9] = 2.0f * (yz - wx);
        res.m[10] = 1.0f - 2.0f * (xx + yy);
        return res;
    }
};