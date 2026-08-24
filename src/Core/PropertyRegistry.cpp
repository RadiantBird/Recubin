#include "include/Core/PropertyRegistry.hpp"
#include <Instances/Spatial.hpp>
#include <Instances/Script.hpp>
#include <algorithm>

namespace PropertyRegistry {

struct ClassSchema {
    std::string_view base;
    std::vector<PropertyDesc> props;
};

// 静的初期化順序に依存しないよう関数ローカル静的で保持する
static std::unordered_map<std::string_view, ClassSchema>& registry() {
    static std::unordered_map<std::string_view, ClassSchema> s_registry;
    return s_registry;
}

// ─── 中央 switch（型知識はここだけ。enum 名表は desc から参照） ───

static int valueToLua(lua_State* L, const PropertyDesc& d, const PropValue& v) {
    switch (d.type) {
        case PropType::Float:  lua_pushnumber (L, static_cast<lua_Number>(std::get<float>(v))); break;
        case PropType::Int:    lua_pushinteger(L, static_cast<lua_Integer>(std::get<int>(v)));  break;
        case PropType::Bool:   lua_pushboolean(L, std::get<bool>(v) ? 1 : 0);                    break;
        case PropType::String: lua_pushstring (L, std::get<std::string>(v).c_str());             break;
        case PropType::Vec3: {
            Vector3* p = (Vector3*)lua_newuserdata(L, sizeof(Vector3)); *p = std::get<Vector3>(v);
            luaL_getmetatable(L, LuauEngine::RCBN_VEC3_METATABLE); lua_setmetatable(L, -2);
        } break;
        case PropType::Vec2:   LuauEngine::pushVector2(L, std::get<Vector2>(v)); break;
        case PropType::Color4: {
            Color4* p = (Color4*)lua_newuserdata(L, sizeof(Color4)); *p = std::get<Color4>(v);
            luaL_getmetatable(L, LuauEngine::RCBN_COLOR4_METATABLE); lua_setmetatable(L, -2);
        } break;
        case PropType::Enum: {
            int iv = std::get<int>(v);
            const char* name = "";
            for (const auto& [n, val] : d.enumNames) if (val == iv) { name = n.data(); break; }
            lua_pushstring(L, name);
        } break;
    }
    return 1;
}

static PropValue valueFromLua(lua_State* L, int idx, const PropertyDesc& d) {
    switch (d.type) {
        case PropType::Float:  return static_cast<float>(luaL_checknumber(L, idx));
        case PropType::Int:    return static_cast<int>(luaL_checkinteger(L, idx));
        case PropType::Bool:   return lua_toboolean(L, idx) != 0;
        case PropType::String: return std::string(luaL_checkstring(L, idx));
        case PropType::Vec3:   return *(Vector3*)luaL_checkudata(L, idx, LuauEngine::RCBN_VEC3_METATABLE);
        case PropType::Vec2:   return *(Vector2*)luaL_checkudata(L, idx, LuauEngine::RCBN_VEC2_METATABLE);
        case PropType::Color4: return *(Color4*)luaL_checkudata(L, idx, LuauEngine::RCBN_COLOR4_METATABLE);
        case PropType::Enum: {
            std::string_view s = luaL_checkstring(L, idx);
            for (const auto& [n, val] : d.enumNames) if (n == s) return val;
            return 0;
        }
    }
    return 0;
}

static void valueToYaml(YAML::Emitter& out, const PropertyDesc& d, const PropValue& v) {
    out << YAML::Key << std::string(d.effYamlKey()) << YAML::Value;
    switch (d.type) {
        case PropType::Float:  out << std::get<float>(v);  break;
        case PropType::Int:    out << std::get<int>(v);    break;
        case PropType::Bool:   out << std::get<bool>(v);   break;
        case PropType::String: out << std::get<std::string>(v); break;
        case PropType::Vec3: { const Vector3& a = std::get<Vector3>(v);
            out << YAML::Flow << YAML::BeginSeq << a.x << a.y << a.z << YAML::EndSeq; } break;
        case PropType::Vec2: { const Vector2& a = std::get<Vector2>(v);
            out << YAML::Flow << YAML::BeginSeq << a.x << a.y << YAML::EndSeq; } break;
        case PropType::Color4: { const Color4& c = std::get<Color4>(v);
            out << YAML::Flow << YAML::BeginSeq << c.r << c.g << c.b << c.a << YAML::EndSeq; } break;
        case PropType::Enum: {
            int iv = std::get<int>(v);
            if (d.yamlEnumAsString) {
                const char* name = "";
                for (const auto& [n, val] : d.enumNames) if (val == iv) { name = n.data(); break; }
                out << name;
            } else {
                out << iv;
            }
        } break;
    }
}

static PropValue valueFromYaml(const YAML::Node& n, const PropertyDesc& d) {
    switch (d.type) {
        case PropType::Float:  return n.as<float>();
        case PropType::Int:    return n.as<int>();
        case PropType::Bool:   return n.as<bool>();
        case PropType::String: return n.as<std::string>();
        case PropType::Vec3:   return Vector3(n[0].as<float>(), n[1].as<float>(), n[2].as<float>());
        case PropType::Vec2:   return Vector2(n[0].as<float>(), n[1].as<float>());
        case PropType::Color4: return Color4(n[0].as<float>(), n[1].as<float>(), n[2].as<float>(), n[3].as<float>());
        case PropType::Enum: {
            if (d.yamlEnumAsString) {
                std::string s = n.as<std::string>();
                for (const auto& [name, val] : d.enumNames) if (name == s) return val;
                return 0;
            }
            return n.as<int>();
        }
    }
    return 0;
}

// ─── 登録 / 利用 ───

void registerClass(std::string_view className, std::vector<PropertyDesc> props) {
    registry()[className] = ClassSchema{ std::string_view{}, std::move(props) };
}
void registerClass(std::string_view className, std::string_view baseClassName,
                   std::vector<PropertyDesc> props) {
    registry()[className] = ClassSchema{ baseClassName, std::move(props) };
}

std::vector<std::string_view> registeredClassNames() {
    std::vector<std::string_view> out;
    out.reserve(registry().size());
    for (const auto& [name, schema] : registry()) out.push_back(name);
    return out;
}

const std::vector<PropertyDesc>& schemaFor(std::string_view className) {
    static const std::vector<PropertyDesc> empty;
    auto it = registry().find(className);
    return it == registry().end() ? empty : it->second.props;
}

static void collectInto(std::string_view className, std::vector<const PropertyDesc*>& out) {
    auto it = registry().find(className);
    if (it == registry().end()) return;
    if (!it->second.base.empty()) collectInto(it->second.base, out);  // 基底を先に
    for (const auto& p : it->second.props) out.push_back(&p);
}
std::vector<const PropertyDesc*> collectSchema(std::string_view className) {
    std::vector<const PropertyDesc*> out;
    collectInto(className, out);
    return out;
}

// load/save は基底走査（collectSchema）。SceneLoader.save は最派生クラス名で1回だけ呼ぶ
bool loadProperty(Instance* obj, std::string_view className,
                  const std::string& name, const YAML::Node& value) {
    for (const PropertyDesc* p : collectSchema(className)) {
        if (p->kind == PropKind::Field && p->serialize && p->set && p->effYamlKey() == name) {
            p->set(obj, valueFromYaml(value, *p));
            return true;
        }
    }
    return false;
}

void saveProperties(YAML::Emitter& out, const Instance* obj, std::string_view className) {
    for (const PropertyDesc* p : collectSchema(className)) {
        if (p->kind != PropKind::Field || !p->serialize || !p->get) continue;
        PropValue v = p->get(const_cast<Instance*>(obj));
        if (p->omitEmptyString && p->type == PropType::String && std::get<std::string>(v).empty())
            continue;  // 空文字は出力しない（既存挙動の保持）
        valueToYaml(out, *p, v);
    }
}

void cloneFields(const Instance* src, Instance* dst, std::string_view className) {
    for (const PropertyDesc* p : collectSchema(className))
        if (p->kind == PropKind::Field && p->cloneable && p->get && p->set)
            p->set(dst, p->get(const_cast<Instance*>(src)));
}

void copyCompatibleProperties(const Instance* src, Instance* dst) {
    if (!src || !dst) return;
    // Some concrete scene classes (Cube/Sphere and other BaseCube-derived
    // types) intentionally have no own registry row. Gather each side's
    // complete schema independently; unrelated replacement classes can still
    // share editor properties without sharing an inheritance relationship.
    std::vector<const PropertyDesc*> source;
    std::vector<const PropertyDesc*> target;
    for (const auto className : registeredClassNames()) {
        if (const_cast<Instance*>(src)->IsA(std::string(className))) {
            const auto schema = collectSchema(className);
            source.insert(source.end(), schema.begin(), schema.end());
        }
        if (dst->IsA(std::string(className))) {
            const auto schema = collectSchema(className);
            target.insert(target.end(), schema.begin(), schema.end());
        }
    }
    std::unordered_map<std::string_view, const PropertyDesc*> byName;
    for (const auto* d : source) {
        if (d && d->kind == PropKind::Field && d->cloneable && d->get)
            byName.emplace(d->name, d);
    }
    for (const auto* d : target) {
        if (!d || d->kind != PropKind::Field || !d->cloneable || !d->set) continue;
        const auto it = byName.find(d->name);
        if (it == byName.end() || !it->second || !it->second->get) continue;
        const PropValue value = it->second->get(const_cast<Instance*>(src));
        // PropValue is deliberately type-tagged.  Do not coerce values during
        // replacement: a property is compatible only when its schema type is.
        if (it->second->type != d->type) continue;
        d->set(dst, value);
    }

    // Spatial and Script fields are intentionally hand-written in their
    // classes (rather than registered schemas), but remain serialized editor
    // state and must survive class replacement when the destination supports
    // the same family.
    if (const auto* sourceSpatial = dynamic_cast<const Spatial*>(src)) {
        if (auto* targetSpatial = dynamic_cast<Spatial*>(dst)) {
            targetSpatial->cframe = sourceSpatial->cframe;
            targetSpatial->Size = sourceSpatial->Size;
        }
    }
    if (const auto* sourceScript = dynamic_cast<const Script*>(src)) {
        if (auto* targetScript = dynamic_cast<Script*>(dst)) {
            targetScript->Source = sourceScript->Source;
            targetScript->Path = sourceScript->Path;
            targetScript->Enabled = sourceScript->Enabled;
        }
    }
}

void applyToDispatch(std::string_view className, GetterMap& getters, SetterMap& setters) {
    for (const auto& d : schemaFor(className)) {  // 自クラスのみ
        const PropertyDesc* dp = &d;
        if (d.kind == PropKind::Signal) {
            if (d.signalGet) getters[className][d.name] = d.signalGet;
            continue;
        }
        if (d.get) {
            getters[className][d.name] = [dp](lua_State* L, Instance* o) {
                return valueToLua(L, *dp, dp->get(o));
            };
        }
        if ((d.set || d.luaSet) && !d.noLuaWrite) {  // noLuaWrite: Lua からは読取専用
            setters[className][d.name] = [dp](lua_State* L, Instance* o) {
                PropValue v = valueFromLua(L, 3, *dp);
                if (dp->clampOnLuaWrite && dp->lo < dp->hi) {  // 不正値が困る数値をクランプ
                    if (dp->type == PropType::Float)
                        v = std::clamp(std::get<float>(v), dp->lo, dp->hi);
                    else if (dp->type == PropType::Int)
                        v = std::clamp(std::get<int>(v), static_cast<int>(dp->lo), static_cast<int>(dp->hi));
                }
                if (dp->luaSet) dp->luaSet(o, v);
                else if (dp->set) dp->set(o, v);
                return 0;
            };
        }
    }
}

PropValue readValue(Instance* obj, const PropertyDesc& d) { return d.get(obj); }
void writeValue(Instance* obj, const PropertyDesc& d, const PropValue& v) { if (d.set) d.set(obj, v); }

} // namespace PropertyRegistry
