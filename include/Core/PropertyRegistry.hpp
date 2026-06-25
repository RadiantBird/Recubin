#pragma once
// ==================================================================
//  PropertyRegistry
//
//  クラスごとに「プロパティ定義の単一の表」を持たせ、そこから
//    - Luau getter / setter（DispatchTable / SetterTable へ流し込む）
//    - YAML 保存 / 読込
//    - clone（フィールドコピー）
//  を一括生成する。1プロパティ = 1行の宣言で、各所への手動同期を無くす。
//
//  試作として Humanoid のみ移行（Humanoid.cpp で registerClass する）。
//  未登録クラスは従来どおりの手書き経路を使う（共存）。
// ==================================================================
#include <string>
#include <string_view>
#include <vector>
#include <functional>
#include <unordered_map>
#include <type_traits>

#include <yaml-cpp/yaml.h>

#include "include/Core/LuauEngine.hpp"      // GetterFunc/SetterFunc, メタテーブル名, pushSignal/pushVector2
#include "include/Instances/Instance.hpp"
#include "include/Math/Vector3.hpp"
#include "include/Math/Vector2.hpp"
#include "include/Util/Color4.hpp"

// 1プロパティの全操作（持たない操作は null）
struct PropertyDesc {
    std::string_view name;
    LuauEngine::GetterFunc luaGet;                              // null = Luau 読み取り不可
    LuauEngine::SetterFunc luaSet;                              // null = Luau 書き込み不可
    std::function<void(YAML::Emitter&, const Instance*)> save;  // null = 非永続
    std::function<void(Instance*, const YAML::Node&)>    load;  // null = 非永続
    std::function<void(const Instance*, Instance*)>      copy;  // null = clone 対象外
};

namespace PropertyRegistry {

// ─── メンバ型 V に応じた変換ヘルパ（既存 getter_*/setter_* と同じ挙動） ───
namespace detail {

template<typename V>
inline void luaPush(lua_State* L, const V& v) {
    if constexpr (std::is_same_v<V, bool>)
        lua_pushboolean(L, v ? 1 : 0);
    else if constexpr (std::is_same_v<V, std::string>)
        lua_pushstring(L, v.c_str());
    else if constexpr (std::is_same_v<V, Vector3>) {
        Vector3* p = (Vector3*)lua_newuserdata(L, sizeof(Vector3)); *p = v;
        luaL_getmetatable(L, LuauEngine::RCBN_VEC3_METATABLE); lua_setmetatable(L, -2);
    } else if constexpr (std::is_same_v<V, Vector2>)
        LuauEngine::pushVector2(L, v);
    else if constexpr (std::is_same_v<V, Color4>) {
        Color4* p = (Color4*)lua_newuserdata(L, sizeof(Color4)); *p = v;
        luaL_getmetatable(L, LuauEngine::RCBN_COLOR4_METATABLE); lua_setmetatable(L, -2);
    } else if constexpr (std::is_enum_v<V>)
        lua_pushnumber(L, static_cast<lua_Number>(static_cast<std::underlying_type_t<V>>(v)));
    else
        lua_pushnumber(L, static_cast<lua_Number>(v));  // 算術型
}

template<typename V>
inline V luaRead(lua_State* L, int idx) {
    if constexpr (std::is_same_v<V, bool>)
        return lua_toboolean(L, idx) != 0;
    else if constexpr (std::is_same_v<V, std::string>)
        return std::string(luaL_checkstring(L, idx));
    else if constexpr (std::is_same_v<V, Vector3>)
        return *(Vector3*)luaL_checkudata(L, idx, LuauEngine::RCBN_VEC3_METATABLE);
    else if constexpr (std::is_same_v<V, Vector2>)
        return *(Vector2*)luaL_checkudata(L, idx, LuauEngine::RCBN_VEC2_METATABLE);
    else if constexpr (std::is_same_v<V, Color4>)
        return *(Color4*)luaL_checkudata(L, idx, LuauEngine::RCBN_COLOR4_METATABLE);
    else if constexpr (std::is_enum_v<V>)
        return static_cast<V>(static_cast<int>(luaL_checknumber(L, idx)));
    else
        return static_cast<V>(luaL_checknumber(L, idx));  // 算術型
}

template<typename V>
inline void yamlSave(YAML::Emitter& out, std::string_view name, const V& v) {
    out << YAML::Key << std::string(name) << YAML::Value;
    if constexpr (std::is_same_v<V, Vector3>)
        out << YAML::Flow << YAML::BeginSeq << v.x << v.y << v.z << YAML::EndSeq;
    else if constexpr (std::is_same_v<V, Vector2>)
        out << YAML::Flow << YAML::BeginSeq << v.x << v.y << YAML::EndSeq;
    else if constexpr (std::is_same_v<V, Color4>)
        out << YAML::Flow << YAML::BeginSeq << v.r << v.g << v.b << v.a << YAML::EndSeq;
    else if constexpr (std::is_enum_v<V>)
        out << static_cast<int>(v);
    else
        out << v;  // bool/string/算術型
}

template<typename V>
inline V yamlLoad(const YAML::Node& n) {
    if constexpr (std::is_same_v<V, Vector3>)
        return Vector3(n[0].as<float>(), n[1].as<float>(), n[2].as<float>());
    else if constexpr (std::is_same_v<V, Vector2>)
        return Vector2(n[0].as<float>(), n[1].as<float>());
    else if constexpr (std::is_same_v<V, Color4>)
        return Color4(n[0].as<float>(), n[1].as<float>(), n[2].as<float>(), n[3].as<float>());
    else if constexpr (std::is_enum_v<V>)
        return static_cast<V>(n.as<int>());
    else
        return n.as<V>();  // bool/string/算術型
}

} // namespace detail

// ─── ビルダー ───

// 通常のデータフィールド（読み書き・YAML・clone すべて）
template<typename T, auto M>
PropertyDesc field(std::string_view name) {
    using V = std::remove_reference_t<decltype(std::declval<T&>().*M)>;
    PropertyDesc d;
    d.name = name;
    d.luaGet = [](lua_State* L, Instance* obj) -> int {
        detail::luaPush<V>(L, static_cast<T*>(obj)->*M); return 1;
    };
    d.luaSet = [](lua_State* L, Instance* obj) -> int {
        static_cast<T*>(obj)->*M = detail::luaRead<V>(L, 3); return 0;
    };
    d.save = [name](YAML::Emitter& out, const Instance* obj) {
        detail::yamlSave<V>(out, name, static_cast<const T*>(obj)->*M);
    };
    d.load = [](Instance* obj, const YAML::Node& n) {
        static_cast<T*>(obj)->*M = detail::yamlLoad<V>(n);
    };
    d.copy = [](const Instance* src, Instance* dst) {
        static_cast<T*>(dst)->*M = static_cast<const T*>(src)->*M;
    };
    return d;
}

// 読みはフィールド、Luau からの書き込みのみセッターメソッド経由
// （例: Humanoid.Health は setHealth で死亡判定を通す）。
// YAML 読込 / clone はフィールド直接代入（従来の setProperty / clone と同じ＝
// シーン読込や複製で Died を誤発火させない）。
template<typename T, auto Field, auto SetMethod>
PropertyDesc fieldVia(std::string_view name) {
    PropertyDesc d = field<T, Field>(name);
    d.luaSet = [](lua_State* L, Instance* obj) -> int {
        (static_cast<T*>(obj)->*SetMethod)(static_cast<float>(luaL_checknumber(L, 3)));
        return 0;
    };
    return d;
}

// シグナル（Luau 読み取りのみ。YAML / clone 対象外）
template<typename T, auto M>
PropertyDesc signal(std::string_view name) {
    PropertyDesc d;
    d.name = name;
    d.luaGet = [](lua_State* L, Instance* obj) -> int {
        LuauEngine::pushSignal(L, static_cast<T*>(obj)->*M); return 1;
    };
    return d;
}

// メソッド（self を upvalue にしたクロージャを返す。Luau 読み取りのみ）
inline PropertyDesc method(std::string_view name, lua_CFunction fn) {
    PropertyDesc d;
    d.name = name;
    std::string fname(name);
    d.luaGet = [fn, fname](lua_State* L, Instance* obj) -> int {
        auto* ud = (std::weak_ptr<Instance>*)lua_newuserdata(L, sizeof(std::weak_ptr<Instance>));
        new (ud) std::weak_ptr<Instance>(obj->shared_from_this());
        luaL_getmetatable(L, LuauEngine::RCBN_INST_METATABLE);
        lua_setmetatable(L, -2);
        lua_pushcclosure(L, fn, fname.c_str(), 1);
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

// YAML 読込: 一致する load を呼ぶ。表に無ければ false（呼び出し側が基底へ委譲）
bool loadProperty(Instance* obj, std::string_view className,
                  const std::string& name, const YAML::Node& value);

// YAML 保存: クラスの全プロパティを出力する
void saveProperties(YAML::Emitter& out, const Instance* obj, std::string_view className);

// clone: src の全フィールドを dst へコピーする
void cloneProperties(const Instance* src, Instance* dst, std::string_view className);

// Luau の DispatchTable / SetterTable へ getter/setter を流し込む
void applyToDispatch(std::string_view className, GetterMap& getters, SetterMap& setters);

} // namespace PropertyRegistry
