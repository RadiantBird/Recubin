#pragma once
// ==================================================================
//  PropertyRegistry — 実行時プロパティ・スキーマ表（PropValue ベース）
//
//  クラスごとに「プロパティ定義の単一の表」を持たせ、そこから
//    Luau get/set・YAML 読込/保存・clone・エディター・汎用Undo
//  をすべて駆動する。1プロパティ = 1行の宣言で各所への手動同期を無くす。
//
//  設計方針: テンプレートは宣言ビルダー（field/fieldVia/method_prop/enumProp/sig）
//  だけに留める。値は PropValue(variant) に正規化し、型別の処理は PropType で
//  ディスパッチする中央 switch 関数（.cpp）に集約する。
// ==================================================================
#include <string>
#include <string_view>
#include <vector>
#include <variant>
#include <functional>
#include <type_traits>
#include <unordered_map>
#include <utility>

#include <yaml-cpp/yaml.h>

#include "include/Core/LuauEngine.hpp"      // GetterFunc/SetterFunc, pushSignal, metatables
#include "include/Instances/Instance.hpp"
#include "include/Math/Vector3.hpp"
#include "include/Math/Vector2.hpp"
#include "include/Util/Color4.hpp"

enum class PropType { Float, Int, Bool, String, Vec3, Vec2, Color4, Enum };
enum class PropKind { Field, Signal };

// 値の正規表現（Enum は int として格納）
using PropValue = std::variant<float, int, bool, std::string, Vector3, Vector2, Color4>;

struct PropertyDesc {
    std::string_view name;
    PropType type = PropType::Float;
    PropKind kind = PropKind::Field;

    std::function<PropValue(Instance*)>              get;        // 値取得
    std::function<void(Instance*, const PropValue&)> set;        // 既定の値設定（YAML/clone/editor/Lua）
    std::function<void(Instance*, const PropValue&)> luaSet;     // null以外: Lua書込のみ別経路（副作用setter）
    std::function<void(void*, const PropValue&)>     liveSet;    // 空なら set を使う。ドラッグ中の毎フレーム反映用の軽量セッター
                                                                   // （MassDensity/摩擦系のように「確定時のみ actor 再生成」したい
                                                                   //   プロパティで、ドラッグ中はフィールド書き込みだけ行うために使う）
    std::function<int(lua_State*, Instance*)>        signalGet;  // kind==Signal

    std::vector<std::pair<std::string_view, int>>    enumNames;  // type==Enum の文字列↔値
    bool yamlEnumAsString = false;                               // enum の YAML 表現

    std::string_view yamlKey;        // 空でなければ YAML のキー名に使う（Lua/エディター名と別名にできる）
    bool serialize = true, cloneable = true, editable = true;    // 各概念への参加
    bool omitEmptyString = false;    // 空文字の string は YAML へ出力しない
    bool clampOnLuaWrite = false;    // Lua 書込時に lo/hi へクランプする（数値プロパティのみ）
    bool noLuaWrite = false;         // Lua からは読取専用（YAML/clone は読み書き可のまま）
    float lo = 0.0f, hi = 0.0f, step = 0.1f;                     // エディター用レンジ
    std::string_view separator{};     // 空でなければ、このプロパティの直前に ImGui::SeparatorText を描画する（"Appearance"等）

    std::string_view effYamlKey() const { return yamlKey.empty() ? name : yamlKey; }

    // 概念フラグの後置き修飾（宣言を1行に保つ）
    PropertyDesc& readOnly()  { set = nullptr; luaSet = nullptr; return *this; }
    PropertyDesc& noYaml()    { serialize = false; return *this; }
    PropertyDesc& noClone()   { cloneable = false; return *this; }
    PropertyDesc& noEditor()  { editable = false; return *this; }
    PropertyDesc& omitEmpty() { omitEmptyString = true; return *this; }
    PropertyDesc& yaml(std::string_view key) { yamlKey = key; return *this; }
    // 不正値が困る数値は Lua 書込時に lo/hi へクランプ（lo<hi のときのみ有効）
    PropertyDesc& clampLua()  { clampOnLuaWrite = true; return *this; }
    // Lua からは読取専用にする（YAML 読込/保存・clone は通常通り）
    PropertyDesc& luaReadOnly() { noLuaWrite = true; return *this; }
    // ドラッグ中の毎フレーム反映用セッターを別途指定する（確定時は set が呼ばれる）
    PropertyDesc& live(std::function<void(void*, const PropValue&)> fn) { liveSet = std::move(fn); return *this; }
    // エディターでこのプロパティの直前にセクション見出しを描画する
    PropertyDesc& group(std::string_view groupName) { separator = groupName; return *this; }
};

namespace PropertyRegistry {

// ─── メンバポインタ/メンバ関数ポインタの trait ───
template<auto M> struct member_traits;
template<class C, class V, V C::* M> struct member_traits<M> {
    using Class = C; using Value = V;
};
template<auto F> struct fn_traits;
template<class C, class R, class... A, R(C::*F)(A...)> struct fn_traits<F> {
    using Class = C; using Ret = R;
};
template<class C, class R, class... A, R(C::*F)(A...) const> struct fn_traits<F> {
    using Class = C; using Ret = R;
};

template<typename V>
constexpr PropType propTypeOf() {
    if constexpr (std::is_same_v<V, bool>)             return PropType::Bool;
    else if constexpr (std::is_same_v<V, std::string>) return PropType::String;
    else if constexpr (std::is_same_v<V, Vector3>)     return PropType::Vec3;
    else if constexpr (std::is_same_v<V, Vector2>)     return PropType::Vec2;
    else if constexpr (std::is_same_v<V, Color4>)      return PropType::Color4;
    else if constexpr (std::is_enum_v<V>)              return PropType::Int;  // 数値表現の enum
    else if constexpr (std::is_integral_v<V>)          return PropType::Int;
    else                                               return PropType::Float;
}

// ─── PropValue ⇄ 実型 変換（局所ヘルパ。これと propTypeOf だけが型知識を持つ） ───
namespace detail {
template<typename V> PropValue toPV(const V& v) {
    if constexpr (std::is_same_v<V, bool>)             return v;
    else if constexpr (std::is_same_v<V, std::string>) return v;
    else if constexpr (std::is_same_v<V, Vector3>)     return v;
    else if constexpr (std::is_same_v<V, Vector2>)     return v;
    else if constexpr (std::is_same_v<V, Color4>)      return v;
    else if constexpr (std::is_enum_v<V>)              return static_cast<int>(v);
    else if constexpr (std::is_integral_v<V>)          return static_cast<int>(v);
    else                                               return static_cast<float>(v);
}
template<typename V> V fromPV(const PropValue& pv) {
    if constexpr (std::is_same_v<V, bool>)             return std::get<bool>(pv);
    else if constexpr (std::is_same_v<V, std::string>) return std::get<std::string>(pv);
    else if constexpr (std::is_same_v<V, Vector3>)     return std::get<Vector3>(pv);
    else if constexpr (std::is_same_v<V, Vector2>)     return std::get<Vector2>(pv);
    else if constexpr (std::is_same_v<V, Color4>)      return std::get<Color4>(pv);
    else if constexpr (std::is_enum_v<V>)              return static_cast<V>(std::get<int>(pv));
    else if constexpr (std::is_integral_v<V>)          return static_cast<V>(std::get<int>(pv));
    else                                               return static_cast<V>(std::get<float>(pv));
}
} // namespace detail

// ─── 宣言ビルダー ───

// 通常フィールド（読み書き・YAML・clone・エディター）
template<auto M>
PropertyDesc field(std::string_view name, float lo = 0.0f, float hi = 0.0f, float step = 0.1f) {
    using C = typename member_traits<M>::Class;
    using V = typename member_traits<M>::Value;
    PropertyDesc d;
    d.name = name; d.type = propTypeOf<V>(); d.kind = PropKind::Field;
    d.get = [](Instance* o) { return detail::toPV<V>(static_cast<C*>(o)->*M); };
    d.set = [](Instance* o, const PropValue& v) { static_cast<C*>(o)->*M = detail::fromPV<V>(v); };
    d.lo = lo; d.hi = hi; d.step = step;
    return d;
}

// 読みはフィールド、Luau 書込のみセッターメソッド経由（YAML/clone/editor はフィールド直）
template<auto Field, auto SetMethod>
PropertyDesc fieldVia(std::string_view name, float lo = 0.0f, float hi = 0.0f, float step = 0.1f) {
    using C = typename member_traits<Field>::Class;
    PropertyDesc d = field<Field>(name, lo, hi, step);
    d.luaSet = [](Instance* o, const PropValue& v) {
        (static_cast<C*>(o)->*SetMethod)(static_cast<float>(std::get<float>(v)));
    };
    return d;
}

// メソッド経由 get/set（例: Transparency, Sound.Volume）
template<auto Getter, auto Setter>
PropertyDesc method_prop(std::string_view name, float lo = 0.0f, float hi = 0.0f, float step = 0.1f) {
    using C = typename fn_traits<Getter>::Class;
    using R = typename fn_traits<Getter>::Ret;
    PropertyDesc d;
    d.name = name; d.type = propTypeOf<R>(); d.kind = PropKind::Field;
    d.get = [](Instance* o) { return detail::toPV<R>((static_cast<C*>(o)->*Getter)()); };
    d.set = [](Instance* o, const PropValue& v) { (static_cast<C*>(o)->*Setter)(detail::fromPV<R>(v)); };
    d.lo = lo; d.hi = hi; d.step = step;
    return d;
}

// enum 文字列（Norm/Face/BillboardMode）
template<auto M>
PropertyDesc enumProp(std::string_view name,
                      std::vector<std::pair<std::string_view, int>> names,
                      bool yamlAsString = false) {
    using C = typename member_traits<M>::Class;
    using V = typename member_traits<M>::Value;
    PropertyDesc d;
    d.name = name; d.type = PropType::Enum; d.kind = PropKind::Field;
    d.enumNames = std::move(names); d.yamlEnumAsString = yamlAsString;
    d.get = [](Instance* o) { return PropValue(static_cast<int>(static_cast<C*>(o)->*M)); };
    d.set = [](Instance* o, const PropValue& v) { static_cast<C*>(o)->*M = static_cast<V>(std::get<int>(v)); };
    return d;
}

// 明示的な get/set（物理同期など特殊なアクセスが必要なプロパティ用）
inline PropertyDesc custom(std::string_view name, PropType type,
                           std::function<PropValue(Instance*)> get,
                           std::function<void(Instance*, const PropValue&)> set) {
    PropertyDesc d;
    d.name = name; d.type = type; d.kind = PropKind::Field;
    d.get = std::move(get); d.set = std::move(set);
    return d;
}

// シグナル（Luau 読み取りのみ）
template<auto M>
PropertyDesc sig(std::string_view name) {
    using C = typename member_traits<M>::Class;
    PropertyDesc d;
    d.name = name; d.kind = PropKind::Signal;
    d.signalGet = [](lua_State* L, Instance* obj) -> int {
        LuauEngine::pushSignal(L, static_cast<C*>(obj)->*M);
        return 1;
    };
    return d;
}

// ─── 登録 / 利用 ───
using GetterMap = std::unordered_map<std::string_view,
                    std::unordered_map<std::string_view, LuauEngine::GetterFunc>>;
using SetterMap = std::unordered_map<std::string_view,
                    std::unordered_map<std::string_view, LuauEngine::SetterFunc>>;

void registerClass(std::string_view className, std::vector<PropertyDesc> props);
void registerClass(std::string_view className, std::string_view baseClassName,
                   std::vector<PropertyDesc> props);

// registerClass 済みの全クラス名（H-2: applyToDispatch 配線漏れ検出用）
std::vector<std::string_view> registeredClassNames();

// 自クラスのみ（Lua dispatch 登録用。基底解決は instance_index が担う）
const std::vector<PropertyDesc>& schemaFor(std::string_view className);
// 基底→派生の順に集約（YAML/clone/editor 用）
std::vector<const PropertyDesc*> collectSchema(std::string_view className);

bool loadProperty(Instance* obj, std::string_view className,
                  const std::string& name, const YAML::Node& value);
void saveProperties(YAML::Emitter& out, const Instance* obj, std::string_view className);
void cloneFields(const Instance* src, Instance* dst, std::string_view className);
void applyToDispatch(std::string_view className, GetterMap& getters, SetterMap& setters);

PropValue readValue(Instance* obj, const PropertyDesc& d);
void      writeValue(Instance* obj, const PropertyDesc& d, const PropValue& v);

} // namespace PropertyRegistry
