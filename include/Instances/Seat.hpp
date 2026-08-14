#pragma once

#include <include/Instances/Cube.hpp>
#include <include/Instances/Named.hpp>
#include <memory>

class Humanoid; // 前方宣言のみ(occupant追跡はweak_ptrで十分)

// 座席。RobloxのVehicleSeat相当。Humanoidが接触すると自動で着席し、
// 毎フレームSteer(A:-1,D:1,Null:0)/Throttle(W:1,S:-1,Null:0)を更新する。
// 実際の駆動(車輪を回す等)はLuauスクリプトがこの2値を読んで行う想定。
class Seat : public Named<Seat, Cube> {
public:
    static constexpr const char* ClassName = "Seat";

    float Steer    = 0.0f; // Lua読取専用。エンジンが着席中のHumanoidから毎フレーム書き込む
    float Throttle = 0.0f; // 同上

    using Named<Seat, Cube>::Named;

    bool isOccupied() const { return !m_occupant.expired(); }
    void setOccupant(std::shared_ptr<Humanoid> h) { m_occupant = h; }
    void clearOccupant() { m_occupant.reset(); }

    std::shared_ptr<Instance> clone() const override;

private:
    std::weak_ptr<Humanoid> m_occupant; // Lua非公開。同時着席防止のガード用
};
