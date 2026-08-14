#pragma once

#include <include/Instances/Sphere.hpp>
#include <include/Instances/Named.hpp>

// 太陽の反対側に配置される月。角度は Renderer が Sun から自動計算する。
class Moon : public Named<Moon, Sphere> {
public:
    static constexpr const char* ClassName = "Moon";

    Moon();

    std::shared_ptr<Instance> clone() const override;
};
