#include <Core/RCBNScriptSignal.hpp>

RCBNScriptSignal::~RCBNScriptSignal() {
    if (!m_mainL) return;
    for (auto& l : m_listeners) {
        if (l.luaRef != LUA_NOREF)
            lua_unref(m_mainL, l.luaRef);
    }
    m_listeners.clear();
}

int RCBNScriptSignal::connect(lua_State* L, int luaRef, bool once) {
    if (!m_mainL) m_mainL = L;
    int id = m_nextId++;
    m_listeners.push_back({ luaRef, once, id });
    return id;
}

void RCBNScriptSignal::disconnect(int id) {
    for (auto it = m_listeners.begin(); it != m_listeners.end(); ++it) {
        if (it->id == id) {
            if (m_mainL && it->luaRef != LUA_NOREF)
                lua_unref(m_mainL, it->luaRef);
            m_listeners.erase(it);
            return;
        }
    }
}

void RCBNScriptSignal::fire(lua_State* L, std::function<int(lua_State*)> pushArgs) {
    // コピーしてイテレート（コールバック内で disconnect されても安全）
    auto copy = m_listeners;
    for (auto& l : copy) {
        if (l.luaRef == LUA_NOREF) continue;
        lua_rawgeti(L, LUA_REGISTRYINDEX, l.luaRef);
        int nargs = pushArgs ? pushArgs(L) : 0;
        if (lua_pcall(L, nargs, 0, 0) != 0) {
            const char* err = lua_tostring(L, -1);
            if (err) printf("[Signal error] %s\n", err);
            lua_pop(L, 1);
        }
        if (l.once) disconnect(l.id);
    }
}

void RCBNScriptSignal::fire(std::function<int(lua_State*)> pushArgs) {
    if (m_mainL) {
        fire(m_mainL, pushArgs);
    }
}

void RCBNScriptConnection::disconnect() {
    if (auto sig = m_signal.lock()) {
        sig->disconnect(m_id);
        m_signal.reset();
    }
}
