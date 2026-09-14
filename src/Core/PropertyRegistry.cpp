#include "include/Core/PropertyRegistry.hpp"
#include <Instances/Spatial.hpp>
#include <Instances/Script.hpp>
#include <algorithm>
#include <unordered_set>
#include <Util/Logger.hpp>
#include <cmath>

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

static void pushQuaternionValue(lua_State* L, const Quaternion& value) {
    auto* userdata = static_cast<Quaternion*>(lua_newuserdata(L, sizeof(Quaternion)));
    *userdata = value;
    luaL_getmetatable(L, LuauEngine::RCBN_QUATERNION_METATABLE);
    lua_setmetatable(L, -2);
}

static void pushCFrameValue(lua_State* L, const CFrame& value) {
    auto* userdata = static_cast<CFrame*>(lua_newuserdata(L, sizeof(CFrame)));
    *userdata = value;
    luaL_getmetatable(L, LuauEngine::RCBN_CFRAME_METATABLE);
    lua_setmetatable(L, -2);
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
        case PropType::CFrame:     pushCFrameValue(L, std::get<CFrame>(v)); break;
        case PropType::Quaternion: pushQuaternionValue(L, std::get<Quaternion>(v)); break;
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
        case PropType::CFrame: return *(CFrame*)luaL_checkudata(L, idx, LuauEngine::RCBN_CFRAME_METATABLE);
        case PropType::Quaternion:
            return *(Quaternion*)luaL_checkudata(L, idx, LuauEngine::RCBN_QUATERNION_METATABLE);
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
        case PropType::CFrame: { const CFrame& frame = std::get<CFrame>(v);
            out << YAML::BeginMap;
            out << YAML::Key << "Position" << YAML::Value
                << YAML::Flow << YAML::BeginSeq
                << frame.Position.x << frame.Position.y << frame.Position.z << YAML::EndSeq;
            out << YAML::Key << "Rotation" << YAML::Value
                << YAML::Flow << YAML::BeginSeq
                << frame.Rotation.x << frame.Rotation.y << frame.Rotation.z << frame.Rotation.w << YAML::EndSeq;
            out << YAML::EndMap;
        } break;
        case PropType::Quaternion: { const Quaternion& rotation = std::get<Quaternion>(v);
            out << YAML::Flow << YAML::BeginSeq
                << rotation.x << rotation.y << rotation.z << rotation.w << YAML::EndSeq;
        } break;
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

static bool readFloatSequence(const YAML::Node& node, float* values, int count) {
    if (!node.IsSequence() || node.size() != static_cast<std::size_t>(count)) return false;
    try {
        for (int i = 0; i < count; ++i) values[i] = node[i].as<float>();
    } catch (const YAML::Exception&) {
        return false;
    }
    return true;
}

static PropValue valueFromYaml(const YAML::Node& n, const PropertyDesc& d,
                               const PropValue* currentValue = nullptr) {
    switch (d.type) {
        case PropType::Float:  return n.as<float>();
        case PropType::Int:    return n.as<int>();
        case PropType::Bool:   return n.as<bool>();
        case PropType::String: return n.as<std::string>();
        case PropType::Vec3:   return Vector3(n[0].as<float>(), n[1].as<float>(), n[2].as<float>());
        case PropType::Vec2:   return Vector2(n[0].as<float>(), n[1].as<float>());
        case PropType::Color4: return Color4(n[0].as<float>(), n[1].as<float>(), n[2].as<float>(), n[3].as<float>());
        case PropType::CFrame: {
            CFrame frame = currentValue ? std::get<CFrame>(*currentValue) : CFrame();
            if (!n.IsMap()) return frame;
            float values[4];
            if (readFloatSequence(n["Position"], values, 3))
                frame.Position = Vector3(values[0], values[1], values[2]);
            if (readFloatSequence(n["Rotation"], values, 4)) {
                const float lenSq = values[0] * values[0] + values[1] * values[1] +
                    values[2] * values[2] + values[3] * values[3];
                if (std::isfinite(lenSq) && std::abs(lenSq - 1.0f) > 1e-4f)
                    RCBN_WARN("normalizing CFrame Rotation property from YAML (lengthSquared=" << lenSq << ")");
                Quaternion rotation;
                if (Quaternion::tryFromComponents(values[3], values[0], values[1], values[2], rotation))
                    frame.Rotation = rotation;
            }
            return frame;
        }
        case PropType::Quaternion: {
            float values[4];
            if (!readFloatSequence(n, values, 4))
                return currentValue ? *currentValue : PropValue(Quaternion());
            const float lenSq = values[0] * values[0] + values[1] * values[1] +
                values[2] * values[2] + values[3] * values[3];
            if (std::isfinite(lenSq) && std::abs(lenSq - 1.0f) > 1e-4f)
                RCBN_WARN("normalizing Quaternion property from YAML (lengthSquared=" << lenSq << ")");
            Quaternion rotation;
            if (!Quaternion::tryFromComponents(values[3], values[0], values[1], values[2], rotation))
                return currentValue ? *currentValue : PropValue(Quaternion());
            return rotation;
        }
        case PropType::Enum: {
            if (d.yamlEnumAsString) {
                std::string s = n.as<std::string>();
                for (const auto& [name, val] : d.enumNames) if (name == s) return val;
                RCBN_WARN("Unknown enum value '" << s << "' for property '"
                          << d.name << "'; keeping the current value");
                if (currentValue && std::holds_alternative<int>(*currentValue))
                    return std::get<int>(*currentValue);
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

std::vector<const PropertyDesc*> collectApplicableSchema(Instance* obj) {
    std::vector<const PropertyDesc*> out;
    if (!obj) return out;

    std::unordered_set<const PropertyDesc*> seen;
    auto appendSchema = [&out, &seen](std::string_view className) {
        for (const PropertyDesc* desc : collectSchema(className)) {
            if (desc && seen.insert(desc).second) out.push_back(desc);
        }
    };

    // 最派生型が登録済みなら、その明示的な基底関係を最優先する。
    appendSchema(obj->getClassName());

    // 未登録の派生型（PhysicsConstraint の各具象型など）も、IsA で
    // 一致する登録済み基底スキーマを利用できるようにする。
    std::vector<std::string_view> applicable;
    for (const std::string_view className : registeredClassNames()) {
        if (obj->IsA(std::string(className))) applicable.push_back(className);
    }
    std::sort(applicable.begin(), applicable.end());
    for (const std::string_view className : applicable) appendSchema(className);
    return out;
}

// load/save は基底走査（collectSchema）。SceneLoader.save は最派生クラス名で1回だけ呼ぶ
bool loadProperty(Instance* obj, std::string_view className,
                  const std::string& name, const YAML::Node& value) {
    for (const PropertyDesc* p : collectSchema(className)) {
        if (p->kind == PropKind::Field && p->serialize && p->effYamlKey() == name) {
            if (p->yamlDeserialize && p->yamlDeserialize(obj, value)) return true;
            if (!p->set) continue;
            const PropValue currentValue = p->get ? p->get(obj) : PropValue(0.0f);
            p->set(obj, valueFromYaml(value, *p, p->get ? &currentValue : nullptr));
            return true;
        }
    }
    return false;
}

bool loadApplicableProperty(Instance* obj, const std::string& name,
                            const YAML::Node& value) {
    if (!obj) return false;
    for (const PropertyDesc* p : collectApplicableSchema(obj)) {
        if (!p || p->kind != PropKind::Field || !p->serialize)
            continue;
        if (p->effYamlKey() != name) continue;
        if (p->yamlDeserialize && p->yamlDeserialize(obj, value)) return true;
        if (!p->set) continue;
        const PropValue currentValue = p->get ? p->get(obj) : PropValue(0.0f);
        p->set(obj, valueFromYaml(value, *p, p->get ? &currentValue : nullptr));
        return true;
    }
    return false;
}

void saveProperties(YAML::Emitter& out, const Instance* obj, std::string_view className) {
    for (const PropertyDesc* p : collectSchema(className)) {
        if (p->kind != PropKind::Field || !p->serialize) continue;
        if (p->serializeWhen && !p->serializeWhen(obj)) continue;
        if (p->yamlSerialize) {
            p->yamlSerialize(out, obj, p->effYamlKey());
            continue;
        }
        if (!p->get) continue;
        PropValue v = p->get(const_cast<Instance*>(obj));
        if (p->omitEmptyString && p->type == PropType::String && std::get<std::string>(v).empty())
            continue;  // 空文字は出力しない（既存挙動の保持）
        valueToYaml(out, *p, v);
    }
}

void saveApplicableProperties(YAML::Emitter& out, const Instance* obj) {
    if (!obj) return;
    std::unordered_set<const PropertyDesc*> seen;
    for (const PropertyDesc* p : collectApplicableSchema(const_cast<Instance*>(obj))) {
        if (!p || !seen.insert(p).second) continue;
        if (p->kind != PropKind::Field || !p->serialize) continue;
        if (p->serializeWhen && !p->serializeWhen(obj)) continue;
        if (p->yamlSerialize) {
            p->yamlSerialize(out, obj, p->effYamlKey());
            continue;
        }
        if (!p->get) continue;
        const PropValue value = p->get(const_cast<Instance*>(obj));
        if (p->omitEmptyString && p->type == PropType::String &&
            std::get<std::string>(value).empty())
            continue;
        valueToYaml(out, *p, value);
    }
}

void cloneFields(const Instance* src, Instance* dst, std::string_view className) {
    for (const PropertyDesc* p : collectSchema(className)) {
        if (p->kind == PropKind::Field && p->cloneable && p->get && p->set) {
            p->set(dst, p->get(const_cast<Instance*>(src)));
            if (p->copyState) p->copyState(src, dst);
        }
    }
}

void copyCompatibleProperties(const Instance* src, Instance* dst) {
    if (!src || !dst) return;
    // collectApplicableSchema preserves base-to-derived order and also covers
    // concrete instances whose own class has no registry row.
    const auto source = collectApplicableSchema(const_cast<Instance*>(src));
    const auto target = collectApplicableSchema(dst);
    std::unordered_map<std::string_view, const PropertyDesc*> byName;
    for (const auto* d : source) {
        if (d && d->kind == PropKind::Field && d->cloneable && d->get)
            byName[d->name] = d;
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
        if (d->copyState) d->copyState(src, dst);
    }

}

void applyToDispatch(std::string_view className, GetterMap& getters, SetterMap& setters) {
    // Luau も YAML / clone / editor と同じ基底→派生走査を使う。派生側の
    // descriptor が同名 property を持つ場合は、後に現れる派生定義を優先する。
    for (const PropertyDesc* dp : collectSchema(className)) {
        if (!dp) continue;
        const PropertyDesc& d = *dp;
        if (d.kind == PropKind::Signal) {
            if (d.signalGet) getters[className][d.name] = d.signalGet;
            continue;
        }
        if (d.get && !d.noLuaRead) {
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
