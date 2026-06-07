#pragma once
#include <Core/User.hpp>
#include <Instances/Instance.hpp>
#include <Instances/Spatial.hpp>
#include <cmath>
#include <algorithm>

// カメラ前方へのOBBレイキャストでスポーン位置を決定する。
// ヒットした面の法線方向に 0.5 ずらすので物体が埋まらない。
// 何もなければカメラ前方 10 ユニットを返す。
inline Vector3 computeSpawnPos(User* user, Instance* workspace) {
    if (!user || !workspace) return Vector3(0, 5, 0);
    const Vector3 ori = user->cpos;
    const Vector3 dir = user->forward;

    float   nearestT = 1e30f;
    Vector3 hitWorldNormal(0, 1, 0);

    auto visit = [&](auto& self, Instance* inst) -> void {
        if (!inst) return;
        if (inst->getClassName() == "Skybox") return;
        if (inst->IsA("BaseCube")) {
            Spatial* sp = static_cast<Spatial*>(inst);
            CFrame wCF = sp->getWorldCFrame();

            // レイをオブジェクトのローカル空間へ変換
            Quaternion invRot = wCF.Rotation.conjugate();
            Vector3 lo = invRot.rotate(ori - wCF.Position);
            Vector3 ld = invRot.rotate(dir);
            float ld3[3] = { ld.x, ld.y, ld.z };
            float lo3[3] = { lo.x, lo.y, lo.z };
            float hs[3]  = { sp->Size.x * 0.5f, sp->Size.y * 0.5f, sp->Size.z * 0.5f };

            float tmin = -1e30f, tmax = 1e30f;
            bool  hit  = true;
            int   hitAxis = 1;
            float hitSign = 1.0f;

            for (int i = 0; i < 3 && hit; ++i) {
                if (std::abs(ld3[i]) < 1e-8f) {
                    if (lo3[i] < -hs[i] || lo3[i] > hs[i]) hit = false;
                } else {
                    float t1 = (-hs[i] - lo3[i]) / ld3[i];
                    float t2 = ( hs[i] - lo3[i]) / ld3[i];
                    bool sw = (t1 > t2);
                    if (sw) std::swap(t1, t2);
                    if (t1 > tmin) {
                        tmin    = t1;
                        hitAxis = i;
                        hitSign = sw ? 1.0f : -1.0f;
                    }
                    tmax = (std::min)(tmax, t2);
                    if (tmax < tmin) hit = false;
                }
            }

            // tmin >= 0: カメラ外部からヒット
            if (hit && tmin >= 0.0f && tmin < nearestT) {
                nearestT = tmin;
                Vector3 localNormal(0, 0, 0);
                if      (hitAxis == 0) localNormal.x = hitSign;
                else if (hitAxis == 1) localNormal.y = hitSign;
                else                   localNormal.z = hitSign;
                hitWorldNormal = wCF.Rotation.rotate(localNormal);
            }
        }
        for (auto const& [_, child] : inst->getChildren())
            self(self, child.get());
    };
    visit(visit, workspace);

    if (nearestT < 1e29f) {
        Vector3 hitPos = ori + dir * nearestT;
        return hitPos + hitWorldNormal * 0.5f;
    }
    return ori + dir * 10.0f;
}
