#pragma once

#include <include/Instances/Cube.hpp>
#include <include/Instances/Named.hpp>

// はしご。見た目・衝突形状はCubeと同じ(Box)で、Humanoidが接触中にW/Sで
// 垂直移動、A/Dで水平ストレイフできるようにするための識別タグクラス。
class Truss : public Named<Truss, Cube> {
public:
    static constexpr const char* ClassName = "Truss";

    using Named<Truss, Cube>::Named;

    std::shared_ptr<Instance> clone() const override;
};
