#pragma once

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "Vector3.hpp"
#include "Util/Logger.hpp"

struct Quaternion {
    float w, x, y, z;

    Quaternion()
        : w(1.0f), x(0.0f), y(0.0f), z(0.0f) {}

private:
    Quaternion(float _w, float _x, float _y, float _z)
        : w(_w), x(_x), y(_y), z(_z) {}

public:

    float lengthSquared() const {
        return w * w + x * x + y * y + z * z;
    }

    // Normalize values arriving from serialization, scripting, or a native
    // physics API.  The raw constructor remains available for aggregate and
    // arithmetic construction, so callers at untrusted boundaries must use
    // this checked entry point.
    bool tryNormalize() {
        if (!std::isfinite(w) || !std::isfinite(x) ||
            !std::isfinite(y) || !std::isfinite(z)) {
            return false;
        }
        const float lenSq = lengthSquared();
        if (!std::isfinite(lenSq) || lenSq <= 1e-12f) return false;
        const float invLen = 1.0f / std::sqrt(lenSq);
        w *= invLen;
        x *= invLen;
        y *= invLen;
        z *= invLen;
        return isNormalized();
    }

    static bool tryFromComponents(float w, float x, float y, float z,
                                  Quaternion& out) {
        Quaternion candidate(w, x, y, z);
        if (!candidate.tryNormalize()) return false;
        out = candidate;
        return true;
    }

    // Native producers that already describe a rotation use the same checked
    // path; this keeps the invariant explicit at call sites.
    static Quaternion fromNormalizedComponents(float w, float x, float y, float z) {
        Quaternion result;
        if (!tryFromComponents(w, x, y, z, result)) return Quaternion();
        return result;
    }

    bool isNormalized(float epsilon = 1e-4f) const {
        const float lenSq = lengthSquared();

        return std::isfinite(lenSq) &&
               std::abs(lenSq - 1.0f) <= epsilon;
    }

    void assertNormalized(
        const char* where,
        float epsilon = 1e-4f
    ) const {
#ifdef RECUBIN_DEBUG
        const float lenSq = lengthSquared();

        if (
            !std::isfinite(lenSq) ||
            std::abs(lenSq - 1.0f) > epsilon
        ) {
            RCBN_ERROR(
                "Invalid Quaternion at " << where
                << " q=("
                << w << ", "
                << x << ", "
                << y << ", "
                << z << ")"
                << " lengthSquared=" << lenSq
            );

            std::abort();
        }
#else
        (void)where;
        (void)epsilon;
#endif
    }

    static Quaternion fromAxisAngle(
        const Vector3& axis,
        float angleDegree
    ) {
        const float axisLength = axis.length();

        RCBN_ASSERT(
            std::isfinite(axisLength) &&
            std::abs(axisLength - 1.0f) <= 1e-4f,
            "Quaternion::fromAxisAngle axis is not normalized"
            << " axis=("
            << axis.x << ", "
            << axis.y << ", "
            << axis.z << ")"
            << " length=" << axisLength
        );

        const float radHalf =
            (angleDegree * 3.14159265f / 180.0f) * 0.5f;

        const float s = std::sin(radHalf);

        Quaternion result(
            std::cos(radHalf),
            axis.x * s,
            axis.y * s,
            axis.z * s
        );

        if (!result.tryNormalize()) return Quaternion();

        result.assertNormalized(
            "Quaternion::fromAxisAngle result"
        );

        return result;
    }

    static Quaternion FromRotationMatrix(
        const float m[16]
    ) {
        Vector3 right(m[0], m[1], m[2]);
        Vector3 up(m[4], m[5], m[6]);
        Vector3 sourceBack(m[8], m[9], m[10]);
        const float rightLength = right.length();
        if (!std::isfinite(rightLength) || rightLength <= 1e-6f) return Quaternion();
        right = right / rightLength;
        up -= right * Vector3::Dot(up, right);
        const float upLength = up.length();
        if (!std::isfinite(upLength) || upLength <= 1e-6f) return Quaternion();
        up = up / upLength;
        Vector3 back = Vector3::Cross(right, up);
        if (Vector3::Dot(back, sourceBack) < 0.0f) back = -back;
        float n[16] = {};
        n[0] = right.x; n[1] = right.y; n[2] = right.z;
        n[4] = up.x; n[5] = up.y; n[6] = up.z;
        n[8] = back.x; n[9] = back.y; n[10] = back.z;

        const float tr =
            n[0] + n[5] + n[10];

        float qw;
        float qx;
        float qy;
        float qz;

        if (tr > 0.0f) {
            const float s =
                std::sqrt(tr + 1.0f) * 2.0f;

            qw = 0.25f * s;
            qx = (n[6] - n[9]) / s;
            qy = (n[8] - n[2]) / s;
            qz = (n[1] - n[4]) / s;
        }
        else if (
            n[0] > n[5] &&
            n[0] > n[10]
        ) {
            const float s =
                std::sqrt(
                    1.0f +
                    n[0] -
                    n[5] -
                    n[10]
                ) * 2.0f;

            qw = (n[6] - n[9]) / s;
            qx = 0.25f * s;
            qy = (n[1] + n[4]) / s;
            qz = (n[2] + n[8]) / s;
        }
        else if (n[5] > n[10]) {
            const float s =
                std::sqrt(
                    1.0f +
                    n[5] -
                    n[0] -
                    n[10]
                ) * 2.0f;

            qw = (n[8] - n[2]) / s;
            qx = (n[1] + n[4]) / s;
            qy = 0.25f * s;
            qz = (n[6] + n[9]) / s;
        }
        else {
            const float s =
                std::sqrt(
                    1.0f +
                    n[10] -
                    n[0] -
                    n[5]
                ) * 2.0f;

            qw = (n[1] - n[4]) / s;
            qx = (n[2] + n[8]) / s;
            qy = (n[6] + n[9]) / s;
            qz = 0.25f * s;
        }

        Quaternion result(
            qw,
            qx,
            qy,
            qz
        );

        if (!result.tryNormalize()) return Quaternion();

        result.assertNormalized(
            "Quaternion::FromRotationMatrix result"
        );

        return result;
    }

    Quaternion operator*(
        const Quaternion& q
    ) const {
        assertNormalized(
            "Quaternion::operator* lhs"
        );

        q.assertNormalized(
            "Quaternion::operator* rhs"
        );

        Quaternion result(
            w * q.w - x * q.x - y * q.y - z * q.z,
            w * q.x + x * q.w + y * q.z - z * q.y,
            w * q.y - x * q.z + y * q.w + z * q.x,
            w * q.z + x * q.y - y * q.x + z * q.w
        );

        if (!result.tryNormalize()) return Quaternion();

        result.assertNormalized(
            "Quaternion::operator* result"
        );

        return result;
    }

    // 単位Quaternionの共役 (= 逆回転)
    Quaternion conjugate() const {
        assertNormalized(
            "Quaternion::conjugate"
        );

        return Quaternion(
            w,
            -x,
            -y,
            -z
        );
    }

    Vector3 rotate(
        const Vector3& v
    ) const {
        assertNormalized(
            "Quaternion::rotate"
        );

        Vector3 qv = {
            x,
            y,
            z
        };

        Vector3 t =
            Vector3::Cross(qv, v) * 2.0f;

        return
            v +
            t * w +
            Vector3::Cross(qv, t);
    }

    Vector3 getRight() const {
        assertNormalized(
            "Quaternion::getRight"
        );

        return Vector3(
            1.0f - 2.0f * (y * y + z * z),
            2.0f * (x * y + z * w),
            2.0f * (x * z - y * w)
        ).normalize();
    }

    Vector3 getUp() const {
        assertNormalized(
            "Quaternion::getUp"
        );

        return Vector3(
            2.0f * (x * y - z * w),
            1.0f - 2.0f * (x * x + z * z),
            2.0f * (y * z + x * w)
        ).normalize();
    }

    Vector3 getForward() const {
        assertNormalized(
            "Quaternion::getForward"
        );

        return Vector3(
            -2.0f * (x * z + y * w),
            -2.0f * (y * z - x * w),
            -(1.0f - 2.0f * (x * x + y * y))
        ).normalize();
    }

    static Quaternion Slerp(
        const Quaternion& a,
        const Quaternion& b,
        float t
    ) {
        a.assertNormalized(
            "Quaternion::Slerp a"
        );

        b.assertNormalized(
            "Quaternion::Slerp b"
        );

        float dot =
            a.w * b.w +
            a.x * b.x +
            a.y * b.y +
            a.z * b.z;

        Quaternion targetB = b;

        if (dot < 0.0f) {
            dot = -dot;

            targetB = Quaternion(
                -b.w,
                -b.x,
                -b.y,
                -b.z
            );
        }

        dot = std::clamp(
            dot,
            -1.0f,
            1.0f
        );

        if (dot > 0.9995f) {
            Quaternion result(
                a.w + t * (targetB.w - a.w),
                a.x + t * (targetB.x - a.x),
                a.y + t * (targetB.y - a.y),
                a.z + t * (targetB.z - a.z)
            );

            const float lenSq =
                result.lengthSquared();

            RCBN_ASSERT(
                std::isfinite(lenSq) &&
                lenSq > 0.000001f,
                "Quaternion::Slerp produced invalid lerp quaternion"
                << " lengthSquared=" << lenSq
            );

            const float invLen =
                1.0f / std::sqrt(lenSq);

            result.w *= invLen;
            result.x *= invLen;
            result.y *= invLen;
            result.z *= invLen;

            result.assertNormalized(
                "Quaternion::Slerp lerp result"
            );

            return result;
        }

        const float theta0 =
            std::acos(dot);

        const float theta =
            theta0 * t;

        const float sinTheta =
            std::sin(theta);

        const float sinTheta0 =
            std::sin(theta0);

        const float s0 =
            std::cos(theta) -
            dot * sinTheta / sinTheta0;

        const float s1 =
            sinTheta / sinTheta0;

        Quaternion result(
            s0 * a.w + s1 * targetB.w,
            s0 * a.x + s1 * targetB.x,
            s0 * a.y + s1 * targetB.y,
            s0 * a.z + s1 * targetB.z
            );

            if (!result.tryNormalize()) return Quaternion();

            result.assertNormalized(
            "Quaternion::Slerp result"
        );

        return result;
    }

    // Quaternion -> Euler角
    // 度数、XYZ内因回転
    Vector3 toEuler() const {
        assertNormalized(
            "Quaternion::toEuler"
        );

        constexpr float RAD2DEG =
            180.0f / 3.14159265f;

        const float sinP =
            2.0f * (w * x + y * z);

        const float cosP =
            1.0f - 2.0f * (x * x + y * y);

        const float pitch =
            std::atan2(
                sinP,
                cosP
            );

        const float sinY =
            2.0f * (w * y - z * x);

        const float yaw =
            (std::abs(sinY) >= 1.0f)
            ? std::copysign(
                3.14159265f * 0.5f,
                sinY
            )
            : std::asin(sinY);

        const float sinR =
            2.0f * (w * z + x * y);

        const float cosR =
            1.0f - 2.0f * (y * y + z * z);

        const float roll =
            std::atan2(
                sinR,
                cosR
            );

        return Vector3(
            pitch * RAD2DEG,
            yaw * RAD2DEG,
            roll * RAD2DEG
        );
    }

    // Euler角 -> Quaternion
    // 度数、XYZ内因回転
    static Quaternion fromEuler(
        const Vector3& degrees
    ) {
        constexpr float DEG2RAD =
            3.14159265f / 180.0f;

        const float cx =
            std::cos(
                degrees.x *
                DEG2RAD *
                0.5f
            );

        const float sx =
            std::sin(
                degrees.x *
                DEG2RAD *
                0.5f
            );

        const float cy =
            std::cos(
                degrees.y *
                DEG2RAD *
                0.5f
            );

        const float sy =
            std::sin(
                degrees.y *
                DEG2RAD *
                0.5f
            );

        const float cz =
            std::cos(
                degrees.z *
                DEG2RAD *
                0.5f
            );

        const float sz =
            std::sin(
                degrees.z *
                DEG2RAD *
                0.5f
            );

        Quaternion result(
            cx * cy * cz + sx * sy * sz,
            sx * cy * cz - cx * sy * sz,
            cx * sy * cz + sx * cy * sz,
            cx * cy * sz - sx * sy * cz
        );

        if (!result.tryNormalize()) return Quaternion();

        result.assertNormalized(
            "Quaternion::fromEuler result"
        );

        return result;
    }

    static Quaternion LookRotation(
        Vector3 forward,
        Vector3 up = Vector3(0, 1, 0)
    ) {
        if (
            forward.length() <
            0.0001f
        ) {
            return Quaternion();
        }

        Vector3 f =
            forward.normalize();

        // -Zを正面とする。
        // ローカル+Z = Back
        Vector3 back =
            f * -1.0f;

        Vector3 right =
            Vector3::Cross(
                up,
                back
            );

        if (
            right.length() <
            0.0001f
        ) {
            right =
                Vector3::Cross(
                    Vector3(0, 0, 1),
                    back
                );

            if (
                right.length() <
                0.0001f
            ) {
                right =
                    Vector3::Cross(
                        Vector3(1, 0, 0),
                        back
                    );
            }
        }

        right =
            right.normalize();

        Vector3 actualUp =
            Vector3::Cross(
                back,
                right
            );

        float m[16] = { 0 };

        m[0] = right.x;
        m[4] = actualUp.x;
        m[8] = back.x;

        m[1] = right.y;
        m[5] = actualUp.y;
        m[9] = back.y;

        m[2] = right.z;
        m[6] = actualUp.z;
        m[10] = back.z;

        m[15] = 1.0f;

        Quaternion result =
            FromRotationMatrix(m);

        result.assertNormalized(
            "Quaternion::LookRotation result"
        );

        return result;
    }
};
