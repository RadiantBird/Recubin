#include <Math/Vector3.hpp>

struct Matrix4 {
    float m[16];

    // 1. デフォルト：単位行列 (Identity)
    Matrix4() {
        for (int i = 0; i < 16; i++) m[i] = 0.0f;
        m[0] = m[5] = m[10] = m[15] = 1.0f;
    }

    // 2. 行列の掛け算 (A * B) - これがすべての基本
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

    // 3. 移動 (Translation)
    static Matrix4 Translate(float x, float y, float z) {
        Matrix4 res; // 単位行列で初期化される
        res.m[12] = x;
        res.m[13] = y;
        res.m[14] = z;
        return res;
    }

    // 4. 拡大縮小 (Scale)
    static Matrix4 Scale(float x, float y, float z) {
        Matrix4 res;
        res.m[0] = x;
        res.m[5] = y;
        res.m[10] = z;
        return res;
    }

    // 5. 回転 (Rotation) - 各軸
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

    // 6. 投影 (Perspective) - 遠近感
    static Matrix4 Perspective(float fovDeg, float aspect, float zNear, float zFar) {
        Matrix4 res;
        for(int i=0; i<16; i++) res.m[i] = 0.0f; // 一旦クリア
        float f = 1.0f / tan(fovDeg * 3.14159265f / 360.0f);
        res.m[0] = f / aspect;
        res.m[5] = f;
        res.m[10] = (zFar + zNear) / (zNear - zFar);
        res.m[11] = -1.0f;
        res.m[14] = (2.0f * zFar * zNear) / (zNear - zFar);
        return res;
    }

    static Matrix4 LookAt(Vector3 eye, Vector3 target, Vector3 up) {
        Vector3 f = (target - eye).normalize(); // 前
        Vector3 r = Vector3::Cross(f, up).normalize(); // 右
        Vector3 u = Vector3::Cross(r, f); // 上

        Matrix4 res;
        res.m[0] = r.x;  res.m[4] = r.y;  res.m[8] = r.z;
        res.m[1] = u.x;  res.m[5] = u.y;  res.m[9] = u.z;
        res.m[2] = -f.x; res.m[6] = -f.y; res.m[10] = -f.z;
        res.m[12] = -Vector3::Dot(r, eye);
        res.m[13] = -Vector3::Dot(u, eye);
        res.m[14] = Vector3::Dot(f, eye);
        return res;
    }
};