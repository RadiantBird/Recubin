#pragma once
// ==================================================================
//  PropertyRegistry — 実行時プロパティ・スキーマ表
//
//  クラスごとに「プロパティ定義の単一の表」を持たせ、そこから
//    - Luau getter / setter（DispatchTable / SetterTable へ流し込む）
//    - YAML 保存 / 読込
//    - clone（フィールドコピー）
//    - エディターのインスペクタ（PropertiesPanel）
//    - 汎用 Undo コマンド（SetPropertyCommand）
//  をすべて駆動する。1プロパティ = 1行の宣言で各所への手動同期を無くす。
//
//  設計方針: テンプレートは「宣言ビルダー（field/fieldVia/sig）」だけに留め、
//  型別の処理は PropType でディスパッチする中央 switch 関数（.cpp）に集約する。
//  （各操作にテンプレ／if constexpr を撒かない）
//
//  試作として Humanoid のみ移行。未登録クラスは従来の手書き経路と共存する。
// ==================================================================
#include <string>
#include <string_view>
#include <vector>
#include <variant>
#include <functional>
#include <type_traits>
#include <unordered_map>

#include <yaml-cpp/yaml.h>

#include "include/Core/LuauEngine.hpp"      // GetterFunc/SetterFunc, pushSignal
#include "include/Instances/Instance.hpp"
#include "include/Math/Vector3.hpp"
#include "include/Math/Vector2.hpp"
#include "include/Util/Color4.hpp"

// 型タグ
enum class PropType { Float, Int, Bool, String, Vec3, Vec2, Color4 };
enum class PropKind { Field, Signal };

// プロパティ値（Undo の before/after・read/write 用の共通表現）
using PropValue = std::variant<float, int, bool, std::string, Vector3, Vector2, Color4>;

// 1プロパティの定義
struct PropertyDesc {
    std::string_view name;
    PropType type = PropType::Float;
    PropKind kind = PropKind::Field;

    // フィールドへのポインタ（read/write 兼用）。kind==Field のとき有効。
    std::function<void*(Instance*)> ptr;
    // null 以外なら Luau からの書き込みのみこのメソッドを通す（例: Humanoid.Health → setHealth）。
    // YAML 読込 / clone / エディターはフィールド直接代入（従来挙動を保つ）。
    std::function<void(Instance*, double)> numSetter;
    // kind==Signal のとき: シグナルを Lua スタックへ push する getter。
    std::function<int(lua_State*, Instance*)> signalGet;

    // エディター用レンジ（数値ウィジェット）
    float lo = 0.0f, hi = 0.0f, step = 0.1f;
};

namespace PropertyRegistry {

// ─── メンバポインタ M からクラス型/値型を取り出す小さな trait ───
template<auto M> struct member_traits;
template<class C, class V, V C::* M> struct member_traits<M> {
    using Class = C;
    using Value = V;
};

// 型 → PropType（唯一の局所 if constexpr）
template<typename V>
constexpr PropType propTypeOf() {
    if constexpr (std::is_same_v<V, bool>)          return PropType::Bool;
    else if constexpr (std::is_same_v<V, std::string>) return PropType::String;
    else if constexpr (std::is_same_v<V, Vector3>)  return PropType::Vec3;
    else if constexpr (std::is_same_v<V, Vector2>)  return PropType::Vec2;
    else if constexpr (std::is_same_v<V, Color4>)   return PropType::Color4;
    else if constexpr (std::is_enum_v<V>)           return PropType::Int;
    else if constexpr (std::is_integral_v<V>)       return PropType::Int;
    else                                            return PropType::Float;
}

// ─── 宣言ビルダー ───

// 通常のデータフィールド（読み書き・YAML・clone・エディター すべて）
template<auto M>
PropertyDesc field(std::string_view name, float lo = 0.0f, float hi = 0.0f, float step = 0.1f) {
    using C = typename member_traits<M>::Class;
    using V = typename member_traits<M>::Value;
    PropertyDesc d;
    d.name = name;
    d.type = propTypeOf<V>();
    d.kind = PropKind::Field;
    d.ptr  = [](Instance* o) -> void* { return &(static_cast<C*>(o)->*M); };
    d.lo = lo; d.hi = hi; d.step = step;
    return d;
}

// 読みはフィールド、Luau からの書き込みのみセッターメソッド経由（例: Humanoid.Health）。
template<auto Field, auto SetMethod>
PropertyDesc fieldVia(std::string_view name, float lo = 0.0f, float hi = 0.0f, float step = 0.1f) {
    using C = typename member_traits<Field>::Class;
    PropertyDesc d = field<Field>(name, lo, hi, step);
    d.numSetter = [](Instance* o, double v) {
        (static_cast<C*>(o)->*SetMethod)(static_cast<float>(v));
    };
    return d;
}

// シグナル（Luau 読み取りのみ。YAML / clone / エディター対象外）
template<auto M>
PropertyDesc sig(std::string_view name) {
    using C = typename member_traits<M>::Class;
    PropertyDesc d;
    d.name = name;
    d.kind = PropKind::Signal;
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

// クラスのスキーマを取得（未登録なら空 vector への参照）
const std::vector<PropertyDesc>& schemaFor(std::string_view className);

// YAML 読込: 一致する Field を直接代入。表に無ければ false（呼び出し側が基底へ委譲）
bool loadProperty(Instance* obj, std::string_view className,
                  const std::string& name, const YAML::Node& value);

// YAML 保存: クラスの全 Field を出力
void saveProperties(YAML::Emitter& out, const Instance* obj, std::string_view className);

// clone: src の全 Field を dst へコピー
void cloneFields(const Instance* src, Instance* dst, std::string_view className);

// Luau の DispatchTable / SetterTable へ getter/setter を流し込む
void applyToDispatch(std::string_view className, GetterMap& getters, SetterMap& setters);

// 値の読み書き（エディター / 汎用 Undo コマンド用）
PropValue readValue(Instance* obj, const PropertyDesc& d);
void      writeValue(Instance* obj, const PropertyDesc& d, const PropValue& v);

} // namespace PropertyRegistry
