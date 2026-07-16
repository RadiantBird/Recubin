#pragma once
#include <Instances/GuiContent.hpp>
#include <include/Core/PropertyRegistry.hpp>

// コンポーネントメンバ（例: &TextLabel::m_text）から PropertyDesc を生成するヘルパ。
// field<> はネストメンバのポインタを取れないため custom() ベースで単一ソース化する。
namespace GuiContentProps {

template<auto M>  // M: TextContent 型メンバへのポインタ
PropertyDesc text() {
    using C = typename PropertyRegistry::member_traits<M>::Class;
    return PropertyRegistry::custom("Text", PropType::String,
        [](Instance* o) { return PropValue((static_cast<C*>(o)->*M).Text); },
        [](Instance* o, const PropValue& v) { (static_cast<C*>(o)->*M).Text = std::get<std::string>(v); });
}

template<auto M>
PropertyDesc textColor() {
    using C = typename PropertyRegistry::member_traits<M>::Class;
    return PropertyRegistry::custom("TextColor", PropType::Color4,
        [](Instance* o) { return PropValue((static_cast<C*>(o)->*M).TextColor); },
        [](Instance* o, const PropValue& v) { (static_cast<C*>(o)->*M).TextColor = std::get<Color4>(v); });
}

template<auto M>  // M: ImageContent 型メンバへのポインタ
PropertyDesc image() {
    using C = typename PropertyRegistry::member_traits<M>::Class;
    return PropertyRegistry::custom("Image", PropType::String,
        [](Instance* o) { return PropValue((static_cast<C*>(o)->*M).getImage()); },
        [](Instance* o, const PropValue& v) { (static_cast<C*>(o)->*M).setImage(std::get<std::string>(v)); });
}

} // namespace GuiContentProps
