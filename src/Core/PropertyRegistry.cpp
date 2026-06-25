#include "include/Core/PropertyRegistry.hpp"

namespace PropertyRegistry {

// 静的初期化順序に依存しないよう関数ローカル静的で保持する
static std::unordered_map<std::string_view, std::vector<PropertyDesc>>& registry() {
    static std::unordered_map<std::string_view, std::vector<PropertyDesc>> s_registry;
    return s_registry;
}

// ─── 中央 switch（型知識はここだけ） ──────────────────────────────

// フィールド(void*) → Lua スタックへ push
static int valueToLua(lua_State* L, PropType t, void* p) {
    switch (t) {
        case PropType::Float:  lua_pushnumber (L, static_cast<lua_Number>(*static_cast<float*>(p))); break;
        case PropType::Int:    lua_pushinteger(L, static_cast<lua_Integer>(*static_cast<int*>(p)));  break;
        case PropType::Bool:   lua_pushboolean(L, *static_cast<bool*>(p) ? 1 : 0);                   break;
        case PropType::String: lua_pushstring (L, static_cast<std::string*>(p)->c_str());            break;
        case PropType::Vec3: {
            Vector3* v = (Vector3*)lua_newuserdata(L, sizeof(Vector3)); *v = *static_cast<Vector3*>(p);
            luaL_getmetatable(L, LuauEngine::RCBN_VEC3_METATABLE); lua_setmetatable(L, -2);
        } break;
        case PropType::Vec2:   LuauEngine::pushVector2(L, *static_cast<Vector2*>(p)); break;
        case PropType::Color4: {
            Color4* c = (Color4*)lua_newuserdata(L, sizeof(Color4)); *c = *static_cast<Color4*>(p);
            luaL_getmetatable(L, LuauEngine::RCBN_COLOR4_METATABLE); lua_setmetatable(L, -2);
        } break;
    }
    return 1;
}

// Lua スタック(idx) → フィールド(void*) へ書き込み
static void valueFromLuaInto(lua_State* L, int idx, PropType t, void* p) {
    switch (t) {
        case PropType::Float:  *static_cast<float*>(p)       = static_cast<float>(luaL_checknumber(L, idx)); break;
        case PropType::Int:    *static_cast<int*>(p)         = static_cast<int>(luaL_checkinteger(L, idx));  break;
        case PropType::Bool:   *static_cast<bool*>(p)        = lua_toboolean(L, idx) != 0;                   break;
        case PropType::String: *static_cast<std::string*>(p) = luaL_checkstring(L, idx);                     break;
        case PropType::Vec3:   *static_cast<Vector3*>(p) = *(Vector3*)luaL_checkudata(L, idx, LuauEngine::RCBN_VEC3_METATABLE); break;
        case PropType::Vec2:   *static_cast<Vector2*>(p) = *(Vector2*)luaL_checkudata(L, idx, LuauEngine::RCBN_VEC2_METATABLE); break;
        case PropType::Color4: *static_cast<Color4*>(p)  = *(Color4*)luaL_checkudata(L, idx, LuauEngine::RCBN_COLOR4_METATABLE); break;
    }
}

// フィールド(void*) → YAML
static void valueToYaml(YAML::Emitter& out, std::string_view name, PropType t, void* p) {
    out << YAML::Key << std::string(name) << YAML::Value;
    switch (t) {
        case PropType::Float:  out << *static_cast<float*>(p);  break;
        case PropType::Int:    out << *static_cast<int*>(p);    break;
        case PropType::Bool:   out << *static_cast<bool*>(p);   break;
        case PropType::String: out << *static_cast<std::string*>(p); break;
        case PropType::Vec3: { Vector3* v = static_cast<Vector3*>(p);
            out << YAML::Flow << YAML::BeginSeq << v->x << v->y << v->z << YAML::EndSeq; } break;
        case PropType::Vec2: { Vector2* v = static_cast<Vector2*>(p);
            out << YAML::Flow << YAML::BeginSeq << v->x << v->y << YAML::EndSeq; } break;
        case PropType::Color4: { Color4* c = static_cast<Color4*>(p);
            out << YAML::Flow << YAML::BeginSeq << c->r << c->g << c->b << c->a << YAML::EndSeq; } break;
    }
}

// YAML → フィールド(void*)
static void valueFromYamlInto(const YAML::Node& n, PropType t, void* p) {
    switch (t) {
        case PropType::Float:  *static_cast<float*>(p)       = n.as<float>();       break;
        case PropType::Int:    *static_cast<int*>(p)         = n.as<int>();         break;
        case PropType::Bool:   *static_cast<bool*>(p)        = n.as<bool>();        break;
        case PropType::String: *static_cast<std::string*>(p) = n.as<std::string>(); break;
        case PropType::Vec3:   *static_cast<Vector3*>(p) = Vector3(n[0].as<float>(), n[1].as<float>(), n[2].as<float>()); break;
        case PropType::Vec2:   *static_cast<Vector2*>(p) = Vector2(n[0].as<float>(), n[1].as<float>()); break;
        case PropType::Color4: *static_cast<Color4*>(p)  = Color4(n[0].as<float>(), n[1].as<float>(), n[2].as<float>(), n[3].as<float>()); break;
    }
}

// PropValue → フィールド(void*)
static void propValueInto(const PropValue& val, PropType t, void* p) {
    switch (t) {
        case PropType::Float:  *static_cast<float*>(p)       = std::get<float>(val);       break;
        case PropType::Int:    *static_cast<int*>(p)         = std::get<int>(val);         break;
        case PropType::Bool:   *static_cast<bool*>(p)        = std::get<bool>(val);        break;
        case PropType::String: *static_cast<std::string*>(p) = std::get<std::string>(val); break;
        case PropType::Vec3:   *static_cast<Vector3*>(p)     = std::get<Vector3>(val);     break;
        case PropType::Vec2:   *static_cast<Vector2*>(p)     = std::get<Vector2>(val);     break;
        case PropType::Color4: *static_cast<Color4*>(p)      = std::get<Color4>(val);      break;
    }
}

// フィールド(void*) → PropValue
static PropValue propValueFrom(PropType t, void* p) {
    switch (t) {
        case PropType::Float:  return *static_cast<float*>(p);
        case PropType::Int:    return *static_cast<int*>(p);
        case PropType::Bool:   return *static_cast<bool*>(p);
        case PropType::String: return *static_cast<std::string*>(p);
        case PropType::Vec3:   return *static_cast<Vector3*>(p);
        case PropType::Vec2:   return *static_cast<Vector2*>(p);
        case PropType::Color4: return *static_cast<Color4*>(p);
    }
    return 0.0f;
}

// ─── 登録 / 利用 ──────────────────────────────────────────────────

void registerClass(std::string_view className, std::vector<PropertyDesc> props) {
    registry()[className] = std::move(props);  // 再登録は上書き（冪等）
}

const std::vector<PropertyDesc>& schemaFor(std::string_view className) {
    static const std::vector<PropertyDesc> empty;
    auto it = registry().find(className);
    return it == registry().end() ? empty : it->second;
}

bool loadProperty(Instance* obj, std::string_view className,
                  const std::string& name, const YAML::Node& value) {
    for (const auto& p : schemaFor(className)) {
        if (p.kind == PropKind::Field && p.ptr && p.name == name) {
            valueFromYamlInto(value, p.type, p.ptr(obj));
            return true;
        }
    }
    return false;
}

void saveProperties(YAML::Emitter& out, const Instance* obj, std::string_view className) {
    for (const auto& p : schemaFor(className))
        if (p.kind == PropKind::Field && p.ptr)
            valueToYaml(out, p.name, p.type, p.ptr(const_cast<Instance*>(obj)));
}

void cloneFields(const Instance* src, Instance* dst, std::string_view className) {
    for (const auto& p : schemaFor(className))
        if (p.kind == PropKind::Field && p.ptr)
            propValueInto(propValueFrom(p.type, p.ptr(const_cast<Instance*>(src))), p.type, p.ptr(dst));
}

void applyToDispatch(std::string_view className, GetterMap& getters, SetterMap& setters) {
    for (const auto& p : schemaFor(className)) {
        if (p.kind == PropKind::Signal) {
            if (p.signalGet) {
                auto fn = p.signalGet;
                getters[className][p.name] = [fn](lua_State* L, Instance* obj) { return fn(L, obj); };
            }
            continue;
        }
        // Field
        if (p.ptr) {
            PropType t = p.type;
            auto getPtr = p.ptr;
            getters[className][p.name] = [t, getPtr](lua_State* L, Instance* obj) {
                return valueToLua(L, t, getPtr(obj));
            };
            if (p.numSetter) {
                auto setM = p.numSetter;
                setters[className][p.name] = [setM](lua_State* L, Instance* obj) {
                    setM(obj, luaL_checknumber(L, 3));
                    return 0;
                };
            } else {
                setters[className][p.name] = [t, getPtr](lua_State* L, Instance* obj) {
                    valueFromLuaInto(L, 3, t, getPtr(obj));
                    return 0;
                };
            }
        }
    }
}

PropValue readValue(Instance* obj, const PropertyDesc& d) {
    return propValueFrom(d.type, d.ptr(obj));
}

void writeValue(Instance* obj, const PropertyDesc& d, const PropValue& v) {
    propValueInto(v, d.type, d.ptr(obj));
}

} // namespace PropertyRegistry
