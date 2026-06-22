#pragma once
#include <include/Instances/Instance.hpp>

// Derived::ClassName を getClassName() に自動反映するCRTP。
// 多段継承で override を書き忘れて親のクラス名が返る誤認バグを防ぐ。
template <typename Derived, typename Base>
class Named : public Base {
    public:
        using Base::Base;
        std::string getClassName() override { return Derived::ClassName; }
};
