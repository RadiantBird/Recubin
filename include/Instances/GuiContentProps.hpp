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
        [](Instance* o, const PropValue& v) { (static_cast<C*>(o)->*M).Text = std::get<std::string>(v); })
        .multiline();
}

template<auto M>
PropertyDesc textColor() {
    using C = typename PropertyRegistry::member_traits<M>::Class;
    return PropertyRegistry::custom("TextColor", PropType::Color4,
        [](Instance* o) { return PropValue((static_cast<C*>(o)->*M).TextColor); },
        [](Instance* o, const PropValue& v) { (static_cast<C*>(o)->*M).TextColor = std::get<Color4>(v); });
}

// 揃え・TextScaled は既定値のときYAMLへ出さない（既存シーンの差分を増やさない）
template<auto M>
PropertyDesc textXAlignment() {
    using C = typename PropertyRegistry::member_traits<M>::Class;
    PropertyDesc d = PropertyRegistry::custom("TextXAlignment", PropType::Enum,
        [](Instance* o) { return PropValue(static_cast<int>((static_cast<C*>(o)->*M).XAlignment)); },
        [](Instance* o, const PropValue& v) {
            (static_cast<C*>(o)->*M).XAlignment = static_cast<TextXAlignment>(std::get<int>(v));
        });
    d.enumNames = {{"Left", 0}, {"Center", 1}, {"Right", 2}};
    d.yamlEnumAsString = true;
    d.serializeIf([](const Instance* o) {
        return (static_cast<const C*>(o)->*M).XAlignment != TextXAlignment::Left;
    });
    return d;
}

template<auto M>
PropertyDesc textYAlignment() {
    using C = typename PropertyRegistry::member_traits<M>::Class;
    PropertyDesc d = PropertyRegistry::custom("TextYAlignment", PropType::Enum,
        [](Instance* o) { return PropValue(static_cast<int>((static_cast<C*>(o)->*M).YAlignment)); },
        [](Instance* o, const PropValue& v) {
            (static_cast<C*>(o)->*M).YAlignment = static_cast<TextYAlignment>(std::get<int>(v));
        });
    d.enumNames = {{"Top", 0}, {"Center", 1}, {"Bottom", 2}};
    d.yamlEnumAsString = true;
    d.serializeIf([](const Instance* o) {
        return (static_cast<const C*>(o)->*M).YAlignment != TextYAlignment::Center;
    });
    return d;
}

template<auto M>
PropertyDesc textScaled() {
    using C = typename PropertyRegistry::member_traits<M>::Class;
    PropertyDesc d = PropertyRegistry::custom("TextScaled", PropType::Bool,
        [](Instance* o) { return PropValue((static_cast<C*>(o)->*M).TextScaled); },
        [](Instance* o, const PropValue& v) { (static_cast<C*>(o)->*M).TextScaled = std::get<bool>(v); });
    d.serializeIf([](const Instance* o) { return (static_cast<const C*>(o)->*M).TextScaled; });
    return d;
}

template<auto M>  // M: ImageContent 型メンバへのポインタ
PropertyDesc image() {
    using C = typename PropertyRegistry::member_traits<M>::Class;
    return PropertyRegistry::custom("Image", PropType::String,
        [](Instance* o) { return PropValue((static_cast<C*>(o)->*M).getImage()); },
        [](Instance* o, const PropValue& v) { (static_cast<C*>(o)->*M).setImage(std::get<std::string>(v)); })
        .filePath("Image (*.png;*.jpg;*.bmp;*.tga)", "*.png;*.jpg;*.bmp;*.tga");
}

} // namespace GuiContentProps
